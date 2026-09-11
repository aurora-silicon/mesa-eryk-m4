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

int main(void)
{
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
   nir_def *address = nir_iadd_imm(&b, nir_load_preamble(&b, 1, 64, .base = 24), 64);
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
