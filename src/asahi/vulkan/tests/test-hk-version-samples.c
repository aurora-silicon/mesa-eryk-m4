/* SPDX-License-Identifier: MIT */
#include "hk_nir_version_samples.h"
#include "agx_nir_texture.h"
#include "agx_nir.h"
#include "hk_sampler.h"
#include "nir_builder.h"
#include "nir_serialize.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #x); abort(); } } while (0)

static nir_shader *
fixture(unsigned samples)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                 &agx_nir_options, "versioned sampling");
   nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform,
      glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT), "sampler");
   nir_variable *output = nir_variable_create(b.shader, nir_var_shader_out,
                                              glsl_vec4_type(), "color");
   output->data.location = FRAG_RESULT_DATA0;
   nir_push_loop(&b);
   for (unsigned i = 0; i < samples; ++i) {
      nir_def *deref = &nir_build_deref_var(&b, sampler)->def;
      nir_tex_instr *tex = nir_tex_instr_create(b.shader, 4);
      tex->op = nir_texop_txl;
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->dest_type = nir_type_float32;
      tex->coord_components = 2;
      tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_texture_deref, deref);
      tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_sampler_deref, deref);
      tex->src[2] = nir_tex_src_for_ssa(nir_tex_src_coord, nir_imm_vec2(&b, i, .5));
      tex->src[3] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_float(&b, 0));
      nir_def_init(&tex->instr, &tex->def, 4, 32);
      nir_builder_instr_insert(&b, &tex->instr);
      nir_store_global(&b, &tex->def, nir_imm_int64(&b, 0x1000 + 16 * i), .align_mul = 16);
      nir_store_var(&b, output, &tex->def, 0xf);
   }
   nir_jump(&b, nir_jump_break);
   nir_pop_loop(&b, NULL);
   return b.shader;
}

static void
check_selection(unsigned mode, bool border, float min_lod)
{
   nir_shader *nir = fixture(16);
   nir_variable *sampler = hk_nir_sample_version_candidate(nir);
   CHECK(sampler && hk_nir_version_samples(nir, sampler, true));
   nir_validate_shader(nir, "both sampler versions");
   /* Epilog exports are unconditional. Duplicated branch-local output
    * stores caused RGB channel corruption in the first hardware probe. */
   unsigned outputs = 0;
   nir_function_impl *entry = nir_shader_get_entrypoint(nir);
   nir_foreach_block(block, entry) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic == nir_intrinsic_store_deref &&
             nir_deref_mode_is(nir_src_as_deref(intr->src[0]), nir_var_shader_out)) {
            CHECK(block == nir_impl_last_block(entry));
            outputs++;
         }
      }
   }
   CHECK(outputs == 1);
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            nir_builder b = nir_builder_at(nir_before_instr(instr));
            if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
               if (intr->intrinsic == nir_intrinsic_load_sampler_descriptor_agx)
                  nir_def_replace(&intr->def, nir_imm_ivec2(&b, 0, mode << HK_MINMAX_MODE_SHIFT));
            } else if (instr->type == nir_instr_type_tex) {
               nir_tex_instr *tex = nir_instr_as_tex(instr);
               if (tex->op == nir_texop_has_custom_border_color_agx)
                  nir_def_replace(&tex->def, nir_imm_bool(&b, border));
               else if (tex->op == nir_texop_image_min_lod_agx)
                  nir_def_replace(&tex->def, nir_imm_float16(&b, min_lod));
            }
         }
      }
   }
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_dce);
   } while (progress);
   unsigned samples = 0, tagged = 0;
   nir_foreach_function_impl(impl, nir)
      nir_foreach_block(block, impl)
         nir_foreach_instr(instr, block)
            if (instr->type == nir_instr_type_tex) {
               nir_tex_instr *tex = nir_instr_as_tex(instr);
               CHECK(tex->op == nir_texop_txl);
               samples++;
               tagged += !!(tex->backend_flags & AGX_TEXTURE_FLAG_PLAIN_SAMPLER);
            }
   CHECK(samples == 16);
   CHECK(tagged == ((!mode && !border && min_lod == 0) ? 16 : 0));
   nir_validate_shader(nir, "selected sampler version");
   ralloc_free(nir);
}

int main(int argc, char **argv)
{
   glsl_type_singleton_init_or_ref();
   for (unsigned mode = 0; mode <= 2; ++mode)
      for (unsigned border = 0; border < 2; ++border)
         for (unsigned min = 0; min < 2; ++min)
            check_selection(mode, border, min ? .5 : 0);
   nir_shader *small = fixture(15);
   CHECK(!hk_nir_sample_version_candidate(small));
   ralloc_free(small);
   if (argc == 2) {
      FILE *file = fopen(argv[1], "rb");
      CHECK(file);
      fseek(file, 0, SEEK_END);
      long size = ftell(file);
      CHECK(size > 0 && size < 32 * 1024 * 1024);
      rewind(file);
      void *data = malloc(size);
      CHECK(data && fread(data, 1, size, file) == (size_t)size);
      fclose(file);
      struct blob_reader reader;
      blob_reader_init(&reader, data, size);
      nir_shader *nir = nir_deserialize(NULL, &agx_nir_options, &reader);
      CHECK(nir && !reader.overrun && reader.current == reader.end);
      nir_variable *sampler = hk_nir_sample_version_candidate(nir);
      CHECK(sampler && hk_nir_version_samples(nir, sampler, true));
      nir_validate_shader(nir, "captured shader versions");
      ralloc_free(nir);
      free(data);
   }
   glsl_type_singleton_decref();
   puts("PASS: sampler versions select ordinary, min/max, border and view-minimum paths");
   return 0;
}
