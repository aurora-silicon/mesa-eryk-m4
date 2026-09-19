/* SPDX-License-Identifier: MIT */
#include "hk_descriptor_prefetch.h"
#include "nir_builder.h"
#include "util/format/u_format.h"
#include <assert.h>
#include <stdio.h>

static nir_intrinsic_instr *
load(nir_builder *b, unsigned base, nir_def *index, unsigned shift,
     unsigned components, unsigned bits)
{
   nir_def *value = nir_load_constant_agx(b, components, bits,
      nir_load_preamble(b, 1, 64, .base = base), index,
      .format = bits == 32 ? PIPE_FORMAT_R32_UINT : PIPE_FORMAT_R16_UINT,
      .base = shift, .sign_extend = true, .access = ACCESS_CAN_SPECULATE);
   nir_store_global(b, value, nir_imm_int64(b, 0x1000), .align_mul = 4);
   return nir_instr_as_intrinsic(nir_builder_last_instr(b));
}

static bool allow_ubo(void *data, unsigned set, unsigned offset)
{
   return *(bool *)data && set == 0 && offset == 64;
}

static void test_ubo(bool early, bool direct, unsigned access, bool eligible)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL,
                                                 "bounded UBO prefetch");
   nir_def *table = early ? nir_load_descriptor_set_agx(&b, .desc_set = 0) :
                           nir_load_preamble(&b, 1, 64, .base = 24);
   nir_def *desc = nir_load_global_constant(&b, 4, 32,
                         nir_iadd_imm(&b, table, 64), .align_mul = 16);
   nir_def *pointer = nir_pack_64_2x32_split(&b, nir_channel(&b, desc, 0),
                                               nir_channel(&b, desc, 1));
   nir_def *valid = nir_ult(&b, nir_imm_int(&b, 43), nir_channel(&b, desc, 2));
   nir_def *base = nir_bcsel(&b, valid, pointer, nir_imm_int64(&b, 1ull << 32));
   nir_def *value = nir_load_global(&b, 1, 32, nir_iadd_imm(&b, base, 40),
                     .align_mul = 4, .access = access);
   nir_store_global(&b, value, nir_imm_int64(&b, 0x1000), .align_mul = 4);
   nir_intrinsic_instr *store = nir_instr_as_intrinsic(nir_builder_last_instr(&b));
   /* A second scalar in a conditional block shares the immutable descriptor.
    * Grouping must produce one entry decision and valid dominating phis. */
   nir_push_if(&b, nir_ine_imm(&b, nir_load_sample_id(&b), 0));
   nir_def *valid2 = nir_ult(&b, nir_imm_int(&b, 47), nir_channel(&b, desc, 2));
   nir_def *base2 = nir_bcsel(&b, valid2, pointer, nir_imm_int64(&b, 1ull << 32));
   nir_def *value2 = nir_load_global(&b, 1, 32, nir_iadd_imm(&b, base2, 44),
                     .align_mul = 4, .access = access);
   nir_store_global(&b, value2, nir_imm_int64(&b, 0x1004), .align_mul = 4);
   nir_pop_if(&b, NULL);
   struct hk_ubo_prefetch loads[HK_UBO_PREFETCH_MAX] = {0};
   unsigned end = 28, count = 0;
   bool allowed = false;
   assert(!hk_nir_prefetch_ubos(b.shader, 24, 1, &end, loads, &count,
                               allow_ubo, &allowed));
   assert(count == 0 && end == 28);
   allowed = true;
   if (!eligible) {
      assert(!hk_nir_prefetch_ubos(b.shader, 24, 1, &end, loads, &count,
                                   allow_ubo, &allowed));
      assert(count == 0 && end == 28);
      nir_validate_shader(b.shader, "non-speculatable UBO unchanged");
      ralloc_free(b.shader);
      return;
   }
   assert(hk_nir_prefetch_ubos(b.shader, 24, 1, &end, loads, &count,
                              allow_ubo, &allowed));
   assert(count == 2 && end == 34 && loads[0].set == 0);
   assert(loads[1].direct_half == loads[0].direct_half);
   assert(loads[1].offset_B == 44 && loads[1].uniform_half == 32);
   assert(loads[0].descriptor_offset_B == 64 && loads[0].offset_B == 40);
   assert(loads[0].size_B == 4 && loads[0].uniform_half == 28);
   assert(nir_def_instr(store->src[0].ssa)->type == nir_instr_type_phi);
   nir_validate_shader(b.shader, "bounded UBO prefetch");
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic) continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic == nir_intrinsic_load_preamble &&
             nir_intrinsic_base(intr) == 30) {
            b.cursor = nir_before_instr(instr);
            nir_def_replace(&intr->def, nir_imm_int(&b, direct));
         }
      }
   }
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, b.shader, nir_opt_constant_folding);
      NIR_PASS(progress, b.shader, nir_opt_dead_cf);
      NIR_PASS(progress, b.shader, nir_opt_dce);
   } while (progress);
   unsigned fallback_loads = 0;
   nir_foreach_block(block, b.impl)
      nir_foreach_instr(instr, block)
         fallback_loads += instr->type == nir_instr_type_intrinsic &&
            nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_global;
   assert(fallback_loads == 2 * !direct);
   nir_validate_shader(b.shader, "direct and sparse UBO branches");
   ralloc_free(b.shader);
}

static void test_plain_sampler(void)
{
   static const nir_shader_compiler_options options = {0};
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, &options,
                                                 "plain sampler predicates");
   nir_def *table = nir_load_descriptor_set_agx(&b, .desc_set = 1);
   nir_def *state = nir_load_global_constant(&b, 2, 32,
      nir_iadd_imm(&b, table, 128 + 32), .align_mul = 4);
   nir_def *mode = nir_ubitfield_extract_imm(&b, nir_channel(&b, state, 1), 22, 2);
   nir_store_global(&b, mode, nir_imm_int64(&b, 0x1000), .align_mul = 4);
   nir_intrinsic_instr *out = nir_instr_as_intrinsic(nir_builder_last_instr(&b));
   nir_def *min = nir_load_global_constant(&b, 1, 16,
      nir_iadd_imm(&b, table, 128 + 46), .align_mul = 2);
   nir_store_global(&b, min, nir_imm_int64(&b, 0x1004), .align_mul = 2);
   nir_intrinsic_instr *min_out = nir_instr_as_intrinsic(nir_builder_last_instr(&b));
   nir_def *border = nir_load_global_constant(&b, 1, 16,
      nir_iadd_imm(&b, table, 128 + 42), .align_mul = 2);
   nir_store_global(&b, border, nir_imm_int64(&b, 0x1006), .align_mul = 2);
   nir_intrinsic_instr *border_out = nir_instr_as_intrinsic(nir_builder_last_instr(&b));
   struct hk_plain_sampler samplers[HK_PLAIN_SAMPLER_MAX] = {
      {.set=0,.descriptor_offset_B=128}, {.set=1,.descriptor_offset_B=128}};
   unsigned count = 2;
   assert(hk_nir_specialize_plain_samplers(b.shader, 24, 2, samplers, &count));
   assert(count == 1 && samplers[0].set == 1);
   nir_validate_shader(b.shader, "plain sampler specialization");
   /* The predicate must fold without knowing unrelated descriptor fields. */
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, b.shader, nir_opt_constant_folding);
      NIR_PASS(progress, b.shader, nir_opt_copy_prop);
      NIR_PASS(progress, b.shader, nir_opt_algebraic);
      NIR_PASS(progress, b.shader, nir_opt_dce);
   } while (progress);
   assert(nir_src_is_const(out->src[0]) && nir_src_as_uint(out->src[0]) == 0);
   assert(nir_src_is_const(min_out->src[0]) && nir_src_as_uint(min_out->src[0]) == 0);
   assert(nir_src_is_const(border_out->src[0]) && nir_src_as_uint(border_out->src[0]) == 0xffff);
   nir_validate_shader(b.shader, "plain sampler predicate folding");
   ralloc_free(b.shader);
}

/* The scaled-format load comes through an image handle, not the sampler
 * descriptor address. Exercise exact matching and leave dynamic/other views
 * untouched so their format is still interpreted on the GPU. */
static void test_plain_image(void)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL,
                                                 "plain image predicates");
   nir_intrinsic_instr *out[4];
   for (unsigned i = 0; i < 4; ++i) {
      nir_def *offset = i == 1 ? nir_undef(&b, 1, 32) :
                                nir_imm_int(&b, i == 2 ? 256 : 128);
      nir_def *handle = nir_bindless_image_agx(&b, offset,
                                              .desc_set = i == 3 ? 0 : 1);
      nir_def *address = nir_load_from_texture_handle_agx(&b, handle);
      nir_def *kind = nir_load_global_constant(&b, 1, 16,
                            nir_iadd_imm(&b, address, 50), .align_mul = 2);
      nir_store_global(&b, kind, nir_imm_int64(&b, 0x1000 + 2 * i), .align_mul = 2);
      out[i] = nir_instr_as_intrinsic(nir_builder_last_instr(&b));
   }
   struct hk_plain_sampler samplers[HK_PLAIN_SAMPLER_MAX] = {
      {.set = 1, .descriptor_offset_B = 128}};
   unsigned count = 1;
   assert(hk_nir_specialize_plain_samplers(b.shader, 24, 2, samplers, &count));
   assert(count == 1);
   assert(nir_src_is_const(out[0]->src[0]) && nir_src_as_uint(out[0]->src[0]) == 0);
   for (unsigned i = 1; i < 4; ++i)
      assert(!nir_src_is_const(out[i]->src[0]));
   nir_validate_shader(b.shader, "plain image specialization");
   ralloc_free(b.shader);
}

int main(void)
{
   test_plain_sampler();
   test_plain_image();
   unsigned access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER;
   test_ubo(false, false, access, true); test_ubo(false, true, access, true);
   test_ubo(true, false, access, true); test_ubo(true, true, access, true);
   test_ubo(true, false, ACCESS_NON_WRITEABLE, false);
   test_ubo(true, false, access | ACCESS_VOLATILE, false);
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL, "descriptor prefetch");
   nir_intrinsic_instr *ubo = load(&b, 24, nir_imm_int(&b, 4), 2, 4, 32);
   nir_intrinsic_instr *sampler = load(&b, 32, nir_imm_int(&b, 5), 2, 2, 16);
   nir_intrinsic_instr *texture = load(&b, 32, nir_imm_int(&b, 17), 0, 1, 32);
   nir_intrinsic_instr *dynamic = load(&b, 24, nir_undef(&b, 1, 32), 0, 1, 32);
   nir_intrinsic_instr *root = load(&b, 20, nir_imm_int(&b, 0), 0, 1, 32);
   struct hk_descriptor_prefetch spans[3] = {0};
   unsigned end = 36;
   assert(hk_nir_prefetch_descriptors(b.shader, 24, 3, &end, spans));
   assert(spans[0].offset_B == 64 && spans[0].size_B == 16 && spans[0].uniform_half == 36);
   assert(spans[1].size_B == 0);
   assert(spans[2].offset_B == 40 && spans[2].size_B == 32 && spans[2].uniform_half == 44);
   assert(end == 60);
   nir_intrinsic_instr *u = nir_src_as_intrinsic(ubo->src[0]);
   nir_intrinsic_instr *s = nir_src_as_intrinsic(sampler->src[0]);
   nir_intrinsic_instr *t = nir_src_as_intrinsic(texture->src[0]);
   assert(u->intrinsic == nir_intrinsic_load_preamble && nir_intrinsic_base(u) == 36);
   assert(s->intrinsic == nir_intrinsic_load_preamble && nir_intrinsic_base(s) == 44);
   assert(t->intrinsic == nir_intrinsic_load_preamble && nir_intrinsic_base(t) == 58);
   assert(nir_src_as_intrinsic(dynamic->src[0])->intrinsic == nir_intrinsic_load_constant_agx);
   assert(nir_src_as_intrinsic(root->src[0])->intrinsic == nir_intrinsic_load_constant_agx);
   nir_validate_shader(b.shader, "after descriptor prefetch");
   ralloc_free(b.shader);

   b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL, "generic descriptor address");
   nir_def *address = nir_iadd_imm(&b, nir_iadd_imm(&b,
      nir_load_preamble(&b, 1, 64, .base = 24), 32), 32);
   nir_def *value = nir_load_global_constant(&b, 4, 32, address, .align_mul = 16);
   nir_store_global(&b, value, nir_imm_int64(&b, 0x1000), .align_mul = 16);
   ubo = nir_instr_as_intrinsic(nir_builder_last_instr(&b));
   struct hk_descriptor_prefetch generic[1] = {0};
   end = 28;
   assert(hk_nir_prefetch_descriptors(b.shader, 24, 1, &end, generic));
   assert(generic[0].offset_B == 64 && generic[0].size_B == 16 && end == 36);
   assert(nir_src_as_intrinsic(ubo->src[0])->intrinsic == nir_intrinsic_load_preamble);
   nir_validate_shader(b.shader, "generic descriptor prefetch");
   ralloc_free(b.shader);

   b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL, "prefetch capacity fallback");
   ubo = load(&b, 24, nir_imm_int(&b, 4), 2, 4, 32);
   struct hk_descriptor_prefetch full[1] = {0};
   end = 508;
   assert(!hk_nir_prefetch_descriptors(b.shader, 24, 1, &end, full));
   assert(end == 508 && full[0].size_B == 0);
   assert(nir_src_as_intrinsic(ubo->src[0])->intrinsic == nir_intrinsic_load_constant_agx);
   ralloc_free(b.shader);
   puts("PASS: descriptor spans, format scaling, uniform offsets and load fallback");
   return 0;
}
