/* SPDX-License-Identifier: MIT */
#include "asahi/compiler/agx_nir.h"
#include "asahi/compiler/agx_compile.h"
#include "nir_builder.h"
#include <stdio.h>
#include <stdlib.h>

bool hk_nir_lower_minmax(nir_shader *nir, bool view_min);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #x); abort(); } } while (0)

static void
test(nir_texop op, bool view_min, bool texture_non_uniform, bool sampler_non_uniform)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                 &agx_nir_options, "reduction footprint control flow");
   nir_variable *sampler = nir_variable_create(
      b.shader, nir_var_uniform,
      glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT), "s");
   nir_def *deref = &nir_build_deref_var(&b, sampler)->def;
   nir_def *coord = nir_vec2(&b, nir_imm_float(&b, .25), nir_imm_float(&b, .75));
   nir_def *lod = nir_imm_float(&b, 0);
   nir_tex_instr *tex = nir_tex_instr_create(b.shader,
      op == nir_texop_txl ? 4 : op == nir_texop_txd ? 5 : 3);
   tex->op = op;
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->dest_type = nir_type_float32;
   tex->coord_components = 2;
   tex->texture_non_uniform = texture_non_uniform;
   tex->sampler_non_uniform = sampler_non_uniform;
   tex->src[0] = nir_tex_src_for_ssa(nir_tex_src_texture_deref, deref);
   tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_sampler_deref, deref);
   tex->src[2] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);
   if (op == nir_texop_txl)
      tex->src[3] = nir_tex_src_for_ssa(nir_tex_src_lod, lod);
   if (op == nir_texop_txd) {
      tex->src[3] = nir_tex_src_for_ssa(nir_tex_src_ddx, coord);
      tex->src[4] = nir_tex_src_for_ssa(nir_tex_src_ddy, coord);
   }
   nir_def_init(&tex->instr, &tex->def, 4, 32);
   nir_builder_instr_insert(&b, &tex->instr);
   nir_store_global(&b, &tex->def, nir_imm_int64(&b, 0x1000), .align_mul = 16);
   nir_validate_shader(b.shader, "before reduction lowering");
   CHECK(hk_nir_lower_minmax(b.shader, view_min));
   nir_validate_shader(b.shader, "after reduction lowering");
   unsigned sizes = 0, derivatives = 0;
   /* Quad derivatives may be conditional when the descriptor-dependent
    * branch is uniform. A nonuniform texture OR sampler requires them above
    * the branch, regardless of whether the other descriptor is uniform. */
   const bool unconditional = op == nir_texop_tex &&
                              (texture_non_uniform || sampler_non_uniform);
   nir_foreach_function_impl(impl, b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            bool top_level = block->cf_node.parent->type == nir_cf_node_function;
            if (instr->type == nir_instr_type_tex && nir_instr_as_tex(instr)->op == nir_texop_txs) {
               ++sizes;
               CHECK(top_level == unconditional);
            }
            if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_op op = nir_instr_as_intrinsic(instr)->intrinsic;
               if (op == nir_intrinsic_ddx || op == nir_intrinsic_ddy) {
                  ++derivatives;
                  CHECK(top_level == unconditional);
               }
            }
         }
      }
   }
   CHECK(sizes > 0);
   CHECK((derivatives > 0) == (op == nir_texop_tex));
   ralloc_free(b.shader);
}

int main(void)
{
   glsl_type_singleton_init_or_ref();
   for (unsigned view = 0; view < 2; ++view) {
      for (unsigned texture = 0; texture < 2; ++texture) {
         for (unsigned sampler = 0; sampler < 2; ++sampler) {
            test(nir_texop_txl, view, texture, sampler);
            test(nir_texop_txd, view, texture, sampler);
            test(nir_texop_tex, view, texture, sampler);
         }
      }
   }
   glsl_type_singleton_decref();
   puts("PASS: 24 descriptor/LOD cases keep quad derivatives outside nonuniform branches");
   return 0;
}
