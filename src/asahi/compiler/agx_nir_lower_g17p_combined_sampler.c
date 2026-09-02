/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_nir.h"
#include "agx_nir_texture.h"

#include "nir.h"
#include "nir_builder.h"

static bool
g17p_sample_shape(const nir_tex_instr *tex)
{
   if ((tex->op != nir_texop_tex && tex->op != nir_texop_txb) ||
       tex->sampler_dim != GLSL_SAMPLER_DIM_2D ||
       tex->is_array || tex->is_shadow || tex->is_sparse ||
       tex->coord_components != 2 || tex->backend_flags != 0 ||
       tex->dest_type != nir_type_float32 || tex->def.num_components != 4 ||
       tex->def.bit_size != 32)
      return false;

   const bool has_bias =
      nir_tex_instr_src_index(tex, nir_tex_src_bias) >= 0;
   if (tex->num_srcs != (has_bias ? 4 : 3) ||
       has_bias != (tex->op == nir_texop_txb))
      return false;

   int coord = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord < 0)
      coord = nir_tex_instr_src_index(tex, nir_tex_src_backend1);

   return coord >= 0 && nir_src_num_components(tex->src[coord].src) == 2 &&
          nir_src_bit_size(tex->src[coord].src) == 32 &&
          nir_tex_instr_src_index(tex, nir_tex_src_texture_handle) >= 0 &&
          nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle) >= 0;
}

static bool
g17p_handle_offset(const nir_tex_instr *tex, nir_tex_src_type source,
                   nir_intrinsic_op intrinsic, uint32_t *offset_B)
{
   const int index = nir_tex_instr_src_index(tex, source);
   if (index < 0)
      return false;

   nir_intrinsic_instr *handle = nir_src_as_intrinsic(tex->src[index].src);
   if (!handle || handle->intrinsic != intrinsic ||
       nir_intrinsic_desc_set(handle) != 0 ||
       !nir_src_is_const(handle->src[0]))
      return false;

   *offset_B = nir_src_as_uint(handle->src[0]);
   return true;
}

static bool
g17p_is_set0_address(nir_def *def)
{
   nir_scalar address = nir_scalar_resolved(def, 0);
   nir_intrinsic_instr *load =
      nir_def_as_intrinsic_or_null(address.def);

   return address.comp == 0 && load &&
          load->intrinsic == nir_intrinsic_load_descriptor_set_agx &&
          nir_intrinsic_desc_set(load) == 0;
}

static bool
g17p_is_constant_scalar(nir_scalar offset, uint32_t expected)
{
   offset = nir_scalar_chase_movs(offset);
   if (nir_scalar_is_const(offset))
      return nir_scalar_as_uint(offset) == expected;

   nir_alu_instr *convert = nir_def_as_alu_or_null(offset.def);
   if (offset.comp != 0 || !convert || convert->op != nir_op_u2u64)
      return false;

   return g17p_is_constant_scalar(nir_scalar_chase_alu_src(offset, 0),
                                  expected);
}

static bool
g17p_is_constant_offset(nir_def *def, uint32_t expected)
{
   return g17p_is_constant_scalar(nir_get_scalar(def, 0), expected);
}

/* Match only the driver-generated software LOD-bias field belonging to this
 * sampled descriptor. API samplers carry the same bias in their G17P native
 * sampler word, so this zero-valued software load must not become a txb.
 */
static bool
g17p_is_sampler_bias(const nir_tex_instr *tex, uint32_t expected_offset_B)
{
   const int index = nir_tex_instr_src_index(tex, nir_tex_src_bias);
   if (index < 0)
      return tex->op == nir_texop_tex;

   nir_intrinsic_instr *load = nir_src_as_intrinsic(tex->src[index].src);
   if (!load || load->def.num_components != 1 || load->def.bit_size != 16)
      return false;

   if (load->intrinsic == nir_intrinsic_load_global_constant_offset ||
       load->intrinsic == nir_intrinsic_load_global_constant_bounded) {
      return g17p_is_set0_address(load->src[0].ssa) &&
             g17p_is_constant_offset(load->src[1].ssa, expected_offset_B);
   }

   if (load->intrinsic != nir_intrinsic_load_global_constant)
      return false;

   nir_scalar address = nir_scalar_resolved(load->src[0].ssa, 0);
   nir_alu_instr *add = nir_def_as_alu_or_null(address.def);
   if (address.comp != 0 || !add || add->op != nir_op_iadd)
      return false;

   nir_scalar lhs = nir_scalar_chase_alu_src(address, 0);
   nir_scalar rhs = nir_scalar_chase_alu_src(address, 1);

   return (g17p_is_set0_address(lhs.def) &&
           g17p_is_constant_scalar(rhs, expected_offset_B)) ||
          (g17p_is_set0_address(rhs.def) &&
           g17p_is_constant_scalar(lhs, expected_offset_B));
}

static bool
g17p_has_descriptor_access(const nir_shader *shader)
{
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            const nir_intrinsic_op op =
               nir_instr_as_intrinsic(instr)->intrinsic;
            if (op == nir_intrinsic_bindless_image_agx ||
                op == nir_intrinsic_bindless_sampler_agx ||
                op == nir_intrinsic_load_descriptor_set_agx ||
                op == nir_intrinsic_load_root_agx)
               return true;
         }
      }
   }

   return false;
}

bool
agx_nir_lower_g17p_combined_sampler(
   nir_shader *shader, struct agx_g17p_combined_sampler_binding *binding)
{
   if (!shader || !binding || shader->info.stage != MESA_SHADER_FRAGMENT)
      return false;

   nir_tex_instr *sample = NULL;
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_tex)
               continue;

            if (sample || !g17p_sample_shape(nir_instr_as_tex(instr)))
               return false;
            sample = nir_instr_as_tex(instr);
         }
      }
   }

   if (!sample)
      return false;

   uint32_t texture_offset_B, sampler_state_offset_B;
   if (!g17p_handle_offset(sample, nir_tex_src_texture_handle,
                           nir_intrinsic_bindless_image_agx,
                           &texture_offset_B) ||
       !g17p_handle_offset(sample, nir_tex_src_sampler_handle,
                           nir_intrinsic_bindless_sampler_agx,
                           &sampler_state_offset_B))
      return false;

   /* Gen9 sampled descriptors contain one 32-byte texture followed by the
    * eight-byte native sampler word and its 16-bit global-heap index.
    */
   if ((texture_offset_B & 7) != 0 ||
       sampler_state_offset_B != texture_offset_B + 32)
      return false;

   /* The Gen9 software LOD bias is at byte 12 of sampler state. */
   if (!g17p_is_sampler_bias(sample, sampler_state_offset_B + 12))
      return false;

   nir_steal_tex_src(sample, nir_tex_src_texture_handle);
   nir_steal_tex_src(sample, nir_tex_src_sampler_handle);
   if (sample->op == nir_texop_txb) {
      nir_steal_tex_src(sample, nir_tex_src_bias);
      sample->op = nir_texop_tex;
   }
   sample->texture_index = 0;
   /* Sampler state 0 is reserved for texel fetches throughout libagx. */
   sample->sampler_index = 1;

   NIR_PASS(_, shader, nir_opt_dce);
   if (g17p_has_descriptor_access(shader))
      return false;

   *binding = (struct agx_g17p_combined_sampler_binding){
      .texture_offset_B = texture_offset_B,
      .sampler_index_offset_B = sampler_state_offset_B + 8,
   };
   return true;
}

static bool
g17p_txf_shape(const nir_tex_instr *tex)
{
   if (tex->op != nir_texop_txf ||
       tex->sampler_dim != GLSL_SAMPLER_DIM_2D || tex->is_shadow ||
       tex->is_sparse || (tex->coord_components != 2 &&
                          tex->coord_components != 3) ||
       tex->is_array != (tex->coord_components == 3) ||
       (tex->backend_flags & ~AGX_TEXTURE_FLAG_NO_CLAMP) != 0 ||
       tex->dest_type != nir_type_uint32 || tex->def.num_components != 4 ||
       tex->def.bit_size != 32 || tex->texture_index != 0 ||
       tex->sampler_index != 0)
      return false;

   const int handle =
      nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
   const int lod = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   int coord = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord < 0)
      coord = nir_tex_instr_src_index(tex, nir_tex_src_backend1);

   if (handle < 0 || lod < 0 || coord < 0 || tex->num_srcs != 3 ||
       nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle) >= 0 ||
       nir_tex_instr_src_index(tex, nir_tex_src_sampler_offset) >= 0 ||
       nir_tex_instr_src_index(tex, nir_tex_src_texture_offset) >= 0 ||
       !nir_src_is_const(tex->src[lod].src) ||
       nir_src_as_uint(tex->src[lod].src) != 0)
      return false;

   return nir_src_num_components(tex->src[coord].src) ==
             tex->coord_components &&
          nir_src_bit_size(tex->src[coord].src) == 32;
}

static bool
g17p_has_bindless_texture_access(const nir_shader *shader)
{
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            const nir_intrinsic_op op =
               nir_instr_as_intrinsic(instr)->intrinsic;
            if (op == nir_intrinsic_bindless_image_agx ||
                op == nir_intrinsic_bindless_sampler_agx)
               return true;
         }
      }
   }

   return false;
}

bool
agx_nir_lower_g17p_txf(nir_shader *shader,
                        struct agx_g17p_txf_binding *binding)
{
   if (!shader || !binding || shader->info.stage != MESA_SHADER_COMPUTE)
      return false;

   nir_tex_instr *fetch = NULL;
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_tex)
               continue;

            if (fetch || !g17p_txf_shape(nir_instr_as_tex(instr)))
               return false;
            fetch = nir_instr_as_tex(instr);
         }
      }
   }

   if (!fetch)
      return false;

   uint32_t texture_offset_B;
   if (!g17p_handle_offset(fetch, nir_tex_src_texture_handle,
                           nir_intrinsic_bindless_image_agx,
                           &texture_offset_B) ||
       (texture_offset_B & 7) != 0)
      return false;

   nir_steal_tex_src(fetch, nir_tex_src_texture_handle);
   fetch->texture_index = 0;

   NIR_PASS(_, shader, nir_opt_dce);
   if (g17p_has_bindless_texture_access(shader))
      return false;

   *binding = (struct agx_g17p_txf_binding){
      .texture_offset_B = texture_offset_B,
   };
   return true;
}
