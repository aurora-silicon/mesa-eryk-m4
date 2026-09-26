/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * Copyright 2023 Advanced Micro Devices, Inc.
 * Copyright 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "hk_shader.h"
#include "hk_nir_buffer_bounds.h"
#include "hk_nir_preserve_sample_loops.h"
#include "hk_nir_version_samples.h"

#include "poly/nir/poly_nir.h"
#include "agx_debug.h"
#include "agx_device.h"
#include "agx_helpers.h"
#include "agx_nir_lower_vbo.h"
#include "glsl_types.h"
#include "hk_instance.h"
#include "hk_image.h"
#include "hk_agx3_vdm_decode.h"
#include "libagx.h"
#include "nir.h"
#include "nir_serialize.h"
#include "nir_builder.h"
#include "nir_control_flow.h"
#include "nir_format_convert.h"

#include "agx_bo.h"
#include "hk_cmd_buffer.h"
#include "hk_descriptor_set_layout.h"
#include "hk_device.h"
#include "hk_physical_device.h"
#include "hk_sampler.h"
#include "hk_shader.h"

#include "nir_builder_opcodes.h"
#include "nir_builtin_builder.h"
#include "nir_intrinsics.h"
#include "nir_intrinsics_indices.h"
#include "nir_xfb_info.h"
#include "shader_enums.h"
#include "vk_graphics_state.h"
#include "vk_nir_convert_ycbcr.h"
#include "vk_physical_device_features.h"
#include "vk_pipeline.h"
#include "vk_pipeline_layout.h"
#include "vk_shader.h"
#include "vk_shader_module.h"
#include "vk_ycbcr_conversion.h"

#include "asahi/compiler/agx_compile.h"
#include "asahi/compiler/agx_nir.h"
#include "asahi/compiler/agx_nir_texture.h"
#include "asahi/lib/agx_abi.h"
#include "asahi/lib/agx_linker.h"
#include "asahi/lib/agx_tilebuffer.h"
#include "asahi/lib/agx_uvs.h"
#include "compiler/spirv/nir_spirv.h"

#include "util/blob.h"
#include "util/hash_table.h"
#include "util/macros.h"
#include "util/mesa-blake3.h"
#include "util/simple_mtx.h"
#include "util/u_debug.h"
#include "vulkan/vulkan_core.h"

/* Development capture before texture lowering, which can fail before the
 * backend's existing AGX compiler capture point is reached. */
static void
hk_capture_lowering(nir_shader *nir, const char *phase, unsigned features)
{
   const char *directory = getenv("HK_NIR_CAPTURE_DIR");
   if (!directory)
      return;
   unsigned count = 0, textures = 0;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            ++count;
            textures += instr->type == nir_instr_type_tex;
         }
      }
   }
   fprintf(stderr, "HK lowering %s stage=%u features=%x instructions=%u textures=%u\n",
           phase, nir->info.stage, features, count, textures);
   if (count < 1000)
      return;
   static unsigned serial;
   char path[1024];
   if (snprintf(path, sizeof(path), "%s/hk-%u-%s-%u.nirbin", directory,
                (unsigned)getpid(), phase, p_atomic_inc_return(&serial)) >= sizeof(path))
      return;
   FILE *file = fopen(path, "wb");
   if (!file)
      return;
   struct blob blob;
   blob_init(&blob);
   nir_serialize(&blob, nir, false);
   fwrite(blob.data, 1, blob.size, file);
   fclose(file);
   blob_finish(&blob);
}

struct hk_fs_key {
   bool zs_self_dep;

   /** True if sample shading is forced on via an API knob such as
    * VkPipelineMultisampleStateCreateInfo::minSampleShading
    */
   bool force_sample_shading;

   uint8_t pad[2];
};
static_assert(sizeof(struct hk_fs_key) == 4, "packed");

struct hk_vs_key {
   struct agx_velem_key attribs[32];
   bool skip_prolog;
   bool static_strides;
   bool kill_psiz;
   bool may_have_adjacency;
};
static_assert(sizeof(struct hk_vs_key) == 260, "packed");

union hk_key {
   struct hk_vs_key vs;
   struct hk_fs_key fs;
};

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size =
      glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length, *align = comp_size;
}

uint64_t
hk_physical_device_compiler_flags(const struct hk_physical_device *pdev)
{
   /* The low word is the agx_device debug mask. M4 executable variants
    * also depend on these compiler options; sharing cache entries with the
    * ordinary configuration can silently reuse a shader without its fast
    * variant. Include them in both pipeline UUIDs and disk-cache keys. */
   static_assert(sizeof(pdev->dev.debug) == sizeof(uint32_t));
   uint64_t flags = pdev->dev.debug;
   if (agx_chip_uses_g15_g16_shader_abi(pdev->dev.chip)) {
      flags |= (uint64_t)debug_get_bool_option("HK_M4_PREFETCH_UBOS", false) << 32;
      flags |= (uint64_t)debug_get_bool_option("HK_M4_PREFETCH_VARIANT", false) << 33;
      flags |= (uint64_t)debug_get_bool_option("HK_M4_PLAIN_SAMPLERS", false) << 34;
      flags |= (uint64_t)debug_get_bool_option("HK_M4_PREFETCH_DESCRIPTORS", false) << 35;
      flags |= (uint64_t)debug_get_bool_option("HK_M4_VERSION_SAMPLES", true) << 36;
      flags |= (uint64_t)debug_get_bool_option("HK_M4_NATIVE_PREAMBLE", false) << 37;
      flags |= (uint64_t)debug_get_bool_option("AGX_NATIVE_FLOAT_PREAMBLE", false) << 39;
   }
   if (agx_chip_uses_hal200_ioto(pdev->dev.chip)) {
      flags |= (uint64_t)debug_get_bool_option("AGX_NATIVE_PRESERVE_BRANCHES", false) << 38;
      flags |= (uint64_t)debug_get_bool_option("AGX_NATIVE_MEMORY_PREAMBLE", false) << 40;
      flags |= (uint64_t)debug_get_bool_option("HK_NATIVE_BRANCH_SCALED", true) << 41;
      flags |= (uint64_t)debug_get_bool_option("HK_NATIVE_COALESCE_BROADCAST", false) << 42;
      flags |= (uint64_t)debug_get_bool_option("HK_NATIVE_COMPACT_DESCRIPTOR_PREFETCH", false) << 43;
   }

   /* Backend debug options change executable code too. Reusing ordinary
    * binaries makes e.g. AGX_MESA_DEBUG=wait silently ineffective, and can
    * later reuse diagnostic binaries in a normal application. Reserve the
    * upper 20 bits for the compiler's original bit positions. Logging and
    * validation-only options do not change code and need no separate cache.
    */
   static_assert(AGX_DBG_NOPROMOTE < BITFIELD_BIT(20));
   flags |= (agx_get_compiler_debug() &
             (AGX_DBG_NOOPT | AGX_DBG_WAIT | AGX_DBG_NOPREAMBLE |
              AGX_DBG_DEMAND | AGX_DBG_NOSCHED | AGX_DBG_SPILL |
              AGX_DBG_NOPROMOTE)) << 44;
   return flags;
}

const nir_shader_compiler_options *
hk_get_nir_options(struct vk_physical_device *vk_pdev, mesa_shader_stage stage,
                   UNUSED const struct vk_pipeline_robustness_state *rs)
{
   return &agx_nir_options;
}

static struct spirv_to_nir_options
hk_get_spirv_options(struct vk_physical_device *vk_pdev,
                     UNUSED mesa_shader_stage stage,
                     const struct vk_pipeline_robustness_state *rs)
{
   return (struct spirv_to_nir_options){
      .ssbo_addr_format = hk_buffer_addr_format(
         rs->storage_buffers, rs->null_storage_buffer_descriptor),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .ubo_addr_format = hk_buffer_addr_format(
         rs->uniform_buffers, rs->null_uniform_buffer_descriptor),
      .shared_addr_format = nir_address_format_32bit_offset,
      .min_ssbo_alignment = HK_MIN_SSBO_ALIGNMENT,
      .min_ubo_alignment = HK_MIN_UBO_ALIGNMENT,
   };
}

void
hk_preprocess_nir_internal(struct vk_physical_device *vk_pdev, nir_shader *nir)
{
   /* Must lower before io to temps */
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_lower_terminate_to_demote);
      NIR_PASS(_, nir, nir_lower_halt_to_return);
      NIR_PASS(_, nir, nir_lower_returns);
   }

   hk_capture_lowering(nir, "preprocess-input", 0);
   if (agx_chip_uses_g15_g16_shader_abi(((struct hk_physical_device *)vk_pdev)->dev.chip))
      NIR_PASS(_, nir, hk_nir_preserve_sample_loops);

   /* Unroll loops before lowering indirects via
    * nir_lower_io_vars_to_temporaries */
   UNUSED bool progress = false;
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);

   do {
      progress = false;
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_loop);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);
   } while (progress);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      struct nir_lower_sysvals_to_varyings_options sysvals_opts = {
         .point_coord = true,
         .primitive_id = nir->info.stage == MESA_SHADER_FRAGMENT,
      };

      nir_lower_sysvals_to_varyings(nir, &sysvals_opts);
   }

   NIR_PASS(_, nir, nir_lower_system_values);

   /* Gather info before preprocess_nir but after some general lowering, so
    * inputs_read and system_values_read are accurately set.
    */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
            nir_shader_get_entrypoint(nir), nir_var_shader_out);

   NIR_PASS(_, nir, nir_lower_global_vars_to_local);

   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_split_struct_vars, nir_var_function_temp);

   /* Native compute and fragment constant blobs are uploaded through their
    * stage-specific root fields. Extract immutable arrays before preprocessing
    * lowers large function temporaries to scratch; otherwise every invocation
    * writes the whole constant table to its private stack before indexing it.
    */
   if ((nir->info.stage == MESA_SHADER_COMPUTE ||
        nir->info.stage == MESA_SHADER_FRAGMENT) &&
       agx_chip_uses_hal200_ioto(((struct hk_physical_device *)vk_pdev)->dev.chip)) {
      NIR_PASS(_, nir, nir_lower_var_copies);
      NIR_PASS(_, nir, nir_opt_large_constants, NULL, 32);
   }

   /* Optimize but allow copies because we haven't lowered them yet */
   if (agx_chip_uses_hal200_ioto(((struct hk_physical_device *)vk_pdev)->dev.chip))
      agx_preprocess_nir_g17p(nir);
   else
      agx_preprocess_nir(nir);

   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, nir, nir_lower_var_copies);
}

static void
hk_preprocess_nir(struct vk_physical_device *vk_pdev, nir_shader *nir,
                  UNUSED const struct vk_pipeline_robustness_state *rs)
{
   hk_preprocess_nir_internal(vk_pdev, nir);
   nir_lower_compute_system_values_options csv_options = {
      .has_base_workgroup_id = true,
   };
   NIR_PASS(_, nir, nir_lower_compute_system_values, &csv_options);
}

static void
hk_populate_vs_key(struct hk_vs_key *key,
                   const struct vk_graphics_pipeline_state *state)
{
   memset(key, 0, sizeof(*key));
   key->may_have_adjacency = true;

   if (state && state->ia &&
       !BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY)) {

      key->kill_psiz = state->ia->primitive_topology != MESA_PRIM_POINTS;
      key->may_have_adjacency = mesa_prim_has_adjacency(
         vk_topology_to_mesa(state->ia->primitive_topology));
   }

   if (state && state->vi && !BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_VI) &&
       !BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_VI_BINDINGS_VALID)) {

      agx_fill_velem_keys(state->vi, ~0 /* compacted on use */, key->attribs);
      key->skip_prolog = true;
      key->static_strides =
         !BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_VI_BINDING_STRIDES);

      if (!key->static_strides) {
         for (unsigned i = 0; i < ARRAY_SIZE(key->attribs); ++i) {
            key->attribs[i].stride = 0;
         }
      }
   }
}

static void
hk_populate_fs_key(struct hk_fs_key *key,
                   const struct vk_graphics_pipeline_state *state)
{
   memset(key, 0, sizeof(*key));

   if (state == NULL)
      return;

   if (state->pipeline_flags &
       VK_PIPELINE_CREATE_2_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT)
      key->zs_self_dep = true;

   /* We force per-sample interpolation whenever sampleShadingEnable is set
    * regardless of minSampleShading or rasterizationSamples.
    *
    * When sampleShadingEnable is set, few guarantees are made about the
    * location of interpolation of the inputs.  The only real guarantees are
    * that the inputs are interpolated within the pixel and that you get at
    * least `rasterizationSamples * minSampleShading` unique positions.
    * Importantly, it does not require that when `rasterizationSamples *
    * minSampleShading <= 1.0` that those positions are at the fragment
    * center.  Therefore, it's valid to just always do per-sample all the time.
    *
    * The one caveat here is that we have to be careful about gl_SampleMaskIn.
    * When `hk_fs_key::force_sample_shading = true` we also turn any reads of
    * gl_SampleMaskIn into `1 << gl_SampleID` because the hardware sample mask
    * is actually per-fragment, not per-pass.  We handle this by smashing
    * minSampleShading to 1.0 whenever gl_SampleMaskIn is read.
    */
   const struct vk_multisample_state *ms = state->ms;
   if (ms != NULL && ms->sample_shading_enable)
      key->force_sample_shading = true;
}

enum hk_feature_key {
   HK_FEAT_CUSTOM_BORDER = BITFIELD_BIT(0),
   HK_FEAT_IMAGE_VIEW_MIN_LOD = BITFIELD_BIT(1),
   HK_FEAT_SAMPLER_MINMAX = BITFIELD_BIT(2),
   HK_FEAT_STRICT_SPARSE = BITFIELD_BIT(3),
   HK_FEAT_DEPTH_CLIP_CONTROL = BITFIELD_BIT(4),
   HK_FEAT_DIRECT_BUFFER_DESCRIPTORS = BITFIELD_BIT(5),
};

static enum hk_feature_key
hk_make_feature_key(const struct vk_physical_device *pdev,
                    const struct vk_features *feats)
{
   return ((pdev->supported_features.depthClipControl &&
            (!feats || feats->depthClipControl)) ? HK_FEAT_DEPTH_CLIP_CONTROL : 0) |
          ((!feats || feats->customBorderColors) ? HK_FEAT_CUSTOM_BORDER : 0) |
          ((!feats || feats->minLod) ? HK_FEAT_IMAGE_VIEW_MIN_LOD : 0) |
          ((!feats || feats->samplerFilterMinmax) ? HK_FEAT_SAMPLER_MINMAX : 0) |
          ((pdev->properties.sparseResidencyNonResidentStrict &&
            (!feats || feats->sparseResidencyBuffer || feats->sparseResidencyImage2D))
              ? HK_FEAT_STRICT_SPARSE : 0) |
          ((feats && !feats->sparseBinding && !feats->descriptorBuffer)
              ? HK_FEAT_DIRECT_BUFFER_DESCRIPTORS : 0);
}

static void
hk_hash_graphics_state(struct vk_physical_device *device,
                       const struct vk_graphics_pipeline_state *state,
                       const struct vk_features *features,
                       VkShaderStageFlags stages, blake3_hash blake3_out)
{
   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);
   if (state && (stages & VK_SHADER_STAGE_VERTEX_BIT)) {
      struct hk_vs_key key;
      hk_populate_vs_key(&key, state);
      _mesa_blake3_update(&blake3_ctx, &key, sizeof(key));
   } else if (state && (stages & VK_SHADER_STAGE_FRAGMENT_BIT)) {
      struct hk_fs_key key;
      hk_populate_fs_key(&key, state);
      _mesa_blake3_update(&blake3_ctx, &key, sizeof(key));
   }

   if (state &&
       (stages & (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT))) {
      const bool is_multiview = state->mv->view_mask != 0;
      _mesa_blake3_update(&blake3_ctx, &is_multiview, sizeof(is_multiview));
   }

   enum hk_feature_key feature_key = hk_make_feature_key(device, features);
   _mesa_blake3_update(&blake3_ctx, &feature_key, sizeof(feature_key));

   _mesa_blake3_final(&blake3_ctx, blake3_out);
}

static nir_def *
bounds_check(nir_builder *b, nir_def *data, nir_def *offs, nir_def *bound)
{
   if (data->bit_size == 32 && data->num_components == 1) {
      return nir_bounds_agx(b, data, offs, bound);
   } else {
      /* TODO: Optimize */
      return nir_bcsel(b, nir_uge(b, bound, offs), data,
                       nir_imm_zero(b, data->num_components, data->bit_size));
   }
}

static bool
is_op(nir_scalar s, nir_op op)
{
   return nir_scalar_is_alu(s) && nir_scalar_alu_op(s) == op;
}

static nir_def *
check_in_bounds(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *offset = intr->src[1].ssa;
   nir_def *bound = intr->src[2].ssa;

   unsigned bit_size = intr->def.bit_size;
   assert(bit_size >= 8 && bit_size % 8 == 0);
   unsigned byte_size = bit_size / 8;
   unsigned load_size = byte_size * intr->num_components;

   /* Try to bounds check in terms of elements */
   nir_scalar offset_s = nir_scalar_resolved(offset, 0);
   if (is_op(offset_s, nir_op_amul)) {
      nir_scalar srcs[] = {nir_scalar_chase_alu_src(offset_s, 0),
                           nir_scalar_chase_alu_src(offset_s, 1)};
      unsigned i = nir_scalar_is_const(srcs[0]) ? 1 : 0;
      if (nir_scalar_is_const(srcs[1 - i]) &&
          nir_scalar_as_uint(srcs[1 - i]) == load_size) {

         nir_def *index = nir_mov_scalar(b, srcs[i]);
         return nir_ult(b, index, nir_udiv_imm(b, bound, load_size));
      }
   }

   /* TODO: handle also the iadd(amul) pattern, this is important */

   return hk_nir_buffer_offset_in_bounds(b, offset, bound, load_size);
}

static nir_def *
bound_offset(nir_builder *b, nir_def *valid, nir_scalar offset)
{
   /* If we can, clamp the source of an amul instead of the result, to remain
    * compatible with the hardware address mode.
    */
   if (is_op(offset, nir_op_amul)) {
      nir_scalar srcs[] = {
         nir_scalar_chase_alu_src(offset, 0),
         nir_scalar_chase_alu_src(offset, 1),
      };
      unsigned i = nir_scalar_is_const(srcs[0]) ? 1 : 0;
      nir_def *x = nir_mov_scalar(b, srcs[i]);
      nir_def *y = nir_mov_scalar(b, srcs[1 - i]);

      return nir_amul(b, nir_bcsel(b, valid, x, nir_imm_int(b, 0)), y);
   }

   /* Similar case for when there's an addition (chain) in the way */
   if (is_op(offset, nir_op_iadd)) {
      nir_scalar x = nir_scalar_chase_alu_src(offset, 0);
      nir_scalar y = nir_scalar_chase_alu_src(offset, 1);

      if (is_op(x, nir_op_amul) || is_op(y, nir_op_amul)) {
         return nir_iadd(b, bound_offset(b, valid, x),
                         bound_offset(b, valid, y));
      }
   }

   nir_def *def = nir_mov_scalar(b, offset);

   /* If the offset fits within the zero page, clamping is pointless */
   if (nir_scalar_is_const(offset) &&
       (nir_scalar_as_uint(offset) + 16) < AGX_ZERO_PAGE_SIZE)
      return def;

   /* Otherwise, fallback on clamping the offset */
   return nir_bcsel(b, valid, def, nir_imm_int(b, 0));
}

static void
lower_load_global_bounded(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *base = intr->src[0].ssa;
   nir_def *offset = intr->src[1].ssa;

   /* Bounds check compatibly with the hardware address mode */
   nir_def *valid = check_in_bounds(b, intr);
   base = nir_bcsel(b, valid, base, nir_imm_int64(b, AGX_ZERO_PAGE_ADDRESS));
   offset = bound_offset(b, valid, nir_scalar_resolved(offset, 0));

   enum gl_access_qualifier access = nir_intrinsic_access(intr);

   if (intr->intrinsic == nir_intrinsic_load_global_constant_bounded) {
      access |= ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER;
   }

   nir_def *val = nir_load_global(
      b, intr->def.num_components, intr->def.bit_size,
      nir_iadd(b, base, nir_u2u64(b, offset)),
      .align_mul = nir_intrinsic_align_mul(intr),
      .align_offset = nir_intrinsic_align_offset(intr), .access = access);

   nir_def_replace(&intr->def, val);
}

static bool
lower_load_global_constant_offset_instr(nir_builder *b,
                                        nir_intrinsic_instr *intrin, void *data)
{
   bool *has_soft_fault = data;

   if (intrin->intrinsic == nir_intrinsic_load_global_bounded ||
       (intrin->intrinsic == nir_intrinsic_load_global_constant_bounded &&
        !(*has_soft_fault))) {

      lower_load_global_bounded(b, intrin);
      return true;
   }

   if (intrin->intrinsic != nir_intrinsic_load_global_constant_offset &&
       intrin->intrinsic != nir_intrinsic_load_global_constant_bounded)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *base_addr = intrin->src[0].ssa;
   nir_def *offset = intrin->src[1].ssa;
   nir_def *bound = NULL;

   unsigned bit_size = intrin->def.bit_size;
   assert(bit_size >= 8 && bit_size % 8 == 0);
   unsigned byte_size = bit_size / 8;
   unsigned load_size = byte_size * intrin->num_components;

   if (intrin->intrinsic == nir_intrinsic_load_global_constant_bounded) {
      bound = intrin->src[2].ssa;
   }

   unsigned align_mul = nir_intrinsic_align_mul(intrin);
   unsigned align_offset = nir_intrinsic_align_offset(intrin);

   nir_def *val = nir_load_global_constant(
      b, intrin->def.num_components, intrin->def.bit_size,
      nir_iadd(b, base_addr, nir_u2u64(b, offset)), .align_mul = align_mul,
      .align_offset = align_offset, .access = nir_intrinsic_access(intrin));

   if (intrin->intrinsic == nir_intrinsic_load_global_constant_bounded) {
      nir_scalar offs = nir_scalar_resolved(offset, 0);
      if (nir_scalar_is_const(offs)) {
         /* Compare the exclusive end against the descriptor's byte size.
          * Do not round the access down: a descriptor ending exactly where
          * this load begins must return zero with robustBufferAccess2. */
         uint64_t end = (uint64_t)nir_scalar_as_uint(offs) + load_size;
         val = end <= UINT32_MAX
                  ? bounds_check(b, val, nir_imm_int(b, end), bound)
                  : nir_imm_zero(b, val->num_components, val->bit_size);
      } else {
         /* Saturation makes a wrapping address fail the unsigned check. */
         offset = nir_uadd_sat(b, offset, nir_imm_int(b, load_size));
         val = bounds_check(b, val, offset, bound);
      }
   }

   nir_def_replace(&intrin->def, val);
   return true;
}

struct lower_ycbcr_state {
   uint32_t set_layout_count;
   struct vk_descriptor_set_layout *const *set_layouts;
};

static const struct hk_sampler *
lookup_ycbcr_sampler(const void *_state, uint32_t set, uint32_t binding,
                     uint32_t array_index)
{
   const struct lower_ycbcr_state *state = _state;
   assert(set < state->set_layout_count);
   assert(state->set_layouts[set] != NULL);
   const struct hk_descriptor_set_layout *set_layout =
      vk_to_hk_descriptor_set_layout(state->set_layouts[set]);
   assert(binding < set_layout->binding_count);

   const struct hk_descriptor_set_binding_layout *bind_layout =
      &set_layout->binding[binding];

   if (bind_layout->immutable_samplers == NULL)
      return NULL;

   array_index = MIN2(array_index, bind_layout->array_size - 1);

   const struct hk_sampler *sampler =
      bind_layout->immutable_samplers[array_index];

   return sampler && sampler->vk.ycbcr_conversion ? sampler : NULL;
}

static const struct hk_sampler *
lookup_packed_ycbcr_sampler(nir_tex_instr *tex, const void *state)
{
   int handle = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (handle < 0)
      return NULL;
   nir_deref_instr *deref = nir_src_as_deref(tex->src[handle].src);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned index = 0;
   if (deref->deref_type == nir_deref_type_array) {
      if (!nir_src_is_const(deref->arr.index))
         return NULL;
      index = nir_src_as_uint(deref->arr.index);
   }
   const struct hk_sampler *sampler = lookup_ycbcr_sampler(
      state, var->data.descriptor_set, var->data.binding, index);
   return sampler && hk_packed_ycbcr_bits(sampler->ycbcr.format) ? sampler : NULL;
}

/* API size queries return luma pixels. Run before adding private word queries. */
static bool
lower_packed_ycbcr_size(nir_builder *b, nir_tex_instr *tex, void *state)
{
   if (tex->op != nir_texop_txs || !lookup_packed_ycbcr_sampler(tex, state))
      return false;
   b->cursor = nir_after_instr(&tex->instr);
   nir_def *size = nir_vector_insert_imm(b, &tex->def,
      nir_imul_imm(b, nir_channel(b, &tex->def, 0), 2), 0);
   nir_def_rewrite_uses_after(&tex->def, size);
   return true;
}

/* The descriptor measures two-pixel packed words. LOD depends on the full
 * luma extent, so double the horizontal derivative presented to the hardware.
 * Apply to both API queries and queries generated for min/mag selection. */
static bool
lower_packed_ycbcr_lod(nir_builder *b, nir_tex_instr *tex, void *state)
{
   if (tex->op != nir_texop_lod || !lookup_packed_ycbcr_sampler(tex, state))
      return false;
   int index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (index < 0)
      return false;
   b->cursor = nir_before_instr(&tex->instr);
   nir_def *coord = tex->src[index].src.ssa;
   coord = nir_vector_insert_imm(b, coord,
      nir_fmul_imm(b, nir_channel(b, coord, 0), 2), 0);
   nir_src_rewrite(&tex->src[index].src, coord);
   return true;
}

static nir_def *
packed_ycbcr_word(nir_builder *b, nir_tex_instr *orig, nir_def *x,
                  nir_def *y, nir_def *layer, nir_def *word_size)
{
   x = nir_imin(b, nir_imax(b, x, nir_imm_int(b, 0)),
                  nir_iadd_imm(b, nir_channel(b, word_size, 0), -1));
   nir_def *coord = layer ? nir_vec3(b, x, y, layer) : nir_vec2(b, x, y);
   nir_tex_instr *fetch = nir_tex_instr_create(b->shader, 3);
   fetch->op = nir_texop_txf;
   fetch->sampler_dim = GLSL_SAMPLER_DIM_2D;
   fetch->is_array = orig->is_array;
   fetch->dest_type = nir_type_float32;
   fetch->coord_components = coord->num_components;
   fetch->src[0] = nir_tex_src_for_ssa(nir_tex_src_texture_deref,
      orig->src[nir_tex_instr_src_index(orig, nir_tex_src_texture_deref)].src.ssa);
   fetch->src[1] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);
   fetch->src[2] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_int(b, 0));
   nir_def_init(&fetch->instr, &fetch->def, 4, 32);
   nir_builder_instr_insert(b, &fetch->instr);
   return &fetch->def;
}

/* Reconstruct chroma at one luma centre. The outer explicit pass filters these
 * reconstructed samples, including separately chosen luma/chroma filters. */
static bool
lower_packed_ycbcr_center(nir_builder *b, nir_tex_instr *tex, void *state)
{
   if (tex->op != nir_texop_txl)
      return false;
   const struct hk_sampler *sampler = lookup_packed_ycbcr_sampler(tex, state);
   if (!sampler)
      return false;
   const struct vk_ycbcr_conversion_state *conversion = &sampler->ycbcr;
   unsigned bits = hk_packed_ycbcr_bits(conversion->format);
   bool first = hk_packed_ycbcr_luma_first(conversion->format);
   b->cursor = nir_before_instr(&tex->instr);
   nir_def *word_size = nir_get_texture_size(b, tex);
   nir_def *size = nir_vec2(b, nir_imul_imm(b, nir_channel(b, word_size, 0), 2),
                             nir_channel(b, word_size, 1));
   nir_def *coord = tex->src[nir_tex_instr_src_index(tex, nir_tex_src_coord)].src.ssa;
   nir_def *ij = nir_f2i32(b, nir_ffloor(b,
      nir_fmul(b, nir_channels(b, coord, 3), nir_i2f32(b, size))));
   ij = nir_imin(b, nir_imax(b, ij, nir_imm_int(b, 0)), nir_iadd_imm(b, size, -1));
   nir_def *i = nir_channel(b, ij, 0), *j = nir_channel(b, ij, 1);
   nir_def *layer = NULL;
   if (tex->is_array) {
      layer = nir_f2i32(b, nir_fround_even(b, nir_channel(b, coord, 2)));
      layer = nir_imin(b, nir_imax(b, layer, nir_imm_int(b, 0)),
                         nir_iadd_imm(b, nir_channel(b, word_size, 2), -1));
   }
   nir_def *word_x = nir_ishr_imm(b, i, 1);
   nir_def *word = packed_ycbcr_word(b, tex, word_x, j, layer, word_size);
   nir_def *y = nir_bcsel(b, nir_ine_imm(b, nir_iand_imm(b, i, 1), 0),
      nir_channel(b, word, first ? 2 : 3), nir_channel(b, word, first ? 0 : 1));
   nir_def *chroma = word;
   if (conversion->chroma_filter == VK_FILTER_LINEAR) {
      nir_def *x = nir_fmul_imm(b, nir_i2f32(b, i), 0.5);
      if (conversion->chroma_offsets[0] == VK_CHROMA_LOCATION_MIDPOINT)
         x = nir_fadd_imm(b, x, -0.25);
      nir_def *base = nir_f2i32(b, nir_ffloor(b, x));
      nir_def *a = packed_ycbcr_word(b, tex, base, j, layer, word_size);
      nir_def *z = packed_ycbcr_word(b, tex, nir_iadd_imm(b, base, 1), j, layer, word_size);
      chroma = nir_flrp(b, a, z, nir_ffract(b, x));
      b->shader->info.flrp_lowered = false;
   }
   const float scale = bits == 8 ? 1.0f :
      65535.0f / (((1u << bits) - 1u) << (16 - bits));
   nir_def *raw[4] = {
      nir_fmul_imm(b, nir_channel(b, chroma, first ? 3 : 2), scale),
      nir_fmul_imm(b, y, scale),
      nir_fmul_imm(b, nir_channel(b, chroma, first ? 1 : 0), scale),
      nir_imm_float(b, 1),
   };
   nir_def *mapped[4];
   for (unsigned c = 0; c < 4; ++c) {
      VkComponentSwizzle sw = conversion->mapping[c];
      mapped[c] = sw == VK_COMPONENT_SWIZZLE_ZERO ? nir_imm_float(b, 0) :
                  sw == VK_COMPONENT_SWIZZLE_ONE ? nir_imm_float(b, 1) :
                  raw[sw == VK_COMPONENT_SWIZZLE_IDENTITY ? c : sw - VK_COMPONENT_SWIZZLE_R];
   }
   nir_def *result = nir_vec(b, mapped, 4);
   uint32_t bpcs[4] = {bits, bits, bits, bits};
   if (conversion->ycbcr_model != VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY)
      result = nir_convert_ycbcr_to_rgb(b, conversion->ycbcr_model,
                                      conversion->ycbcr_range, result, bpcs);
   nir_def_replace(&tex->def, result);
   return true;
}

static const struct vk_ycbcr_conversion_state *
lookup_ycbcr_conversion(const void *state, uint32_t set, uint32_t binding,
                        uint32_t array_index)
{
   const struct hk_sampler *sampler =
      lookup_ycbcr_sampler(state, set, binding, array_index);
   /* Packed images were reconstructed by the native word-sampling pass. */
   return sampler && !hk_packed_ycbcr_bits(sampler->ycbcr.format) ? &sampler->ycbcr : NULL;
}

/* Reconstruct at full-resolution luma texel centres, then filter the converted
 * values. The common YCbCr pass lowers each centre sample into plane accesses
 * and colour conversion. YCbCr samplers use clamp-to-edge and a single mip. */
static bool
lower_ycbcr_explicit(nir_builder *b, nir_tex_instr *tex, void *state)
{
   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
      break;
   default:
      return false;
   }
   const int handle = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (handle < 0)
      return false;
   nir_deref_instr *deref = nir_src_as_deref(tex->src[handle].src);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned index = 0;
   if (deref->deref_type == nir_deref_type_array) {
      if (!nir_src_is_const(deref->arr.index))
         return false;
      index = nir_src_as_uint(deref->arr.index);
   }
   const struct hk_sampler *sampler = lookup_ycbcr_sampler(
      state, var->data.descriptor_set, var->data.binding, index);
   if (!sampler || !sampler->ycbcr.force_explicit_reconstruction)
      return false;

   assert(tex->sampler_dim == GLSL_SAMPLER_DIM_2D);
   b->cursor = nir_before_instr(&tex->instr);
   nir_def *size =
      nir_u2f32(b, nir_channels(b, nir_get_texture_size(b, tex), 3));
   if (hk_packed_ycbcr_bits(sampler->ycbcr.format))
      size = nir_fmul(b, size, nir_imm_vec2(b, 2, 1));
   nir_def *coord =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_coord)].src.ssa;
   nir_def *uv = nir_fmul(b, nir_channels(b, coord, 3), size);
   int offset = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset >= 0)
      uv = nir_fadd(b, uv, nir_i2f32(b, tex->src[offset].src.ssa));

   nir_def *linear;
   if (sampler->min_filter == sampler->mag_filter) {
      linear = nir_imm_bool(b, sampler->min_filter == VK_FILTER_LINEAR);
   } else {
      nir_def *lod;
      if (tex->op == nir_texop_txl) {
         lod = tex->src[nir_tex_instr_src_index(tex, nir_tex_src_lod)].src.ssa;
         lod = nir_fadd_imm(b, lod, sampler->mip_lod_bias);
      } else if (tex->op == nir_texop_txd) {
         nir_def *dx = nir_fmul(
            b, size,
            tex->src[nir_tex_instr_src_index(tex, nir_tex_src_ddx)].src.ssa);
         nir_def *dy = nir_fmul(
            b, size,
            tex->src[nir_tex_instr_src_index(tex, nir_tex_src_ddy)].src.ssa);
         lod = nir_ffma_imm12(
            b,
            nir_flog2(b, nir_fmax(b, nir_fdot(b, dx, dx), nir_fdot(b, dy, dy))),
            0.5, sampler->mip_lod_bias);
      } else {
         lod = nir_get_texture_lod(b, tex);
         if (tex->op == nir_texop_txb)
            lod = nir_fadd(
               b, lod,
               tex->src[nir_tex_instr_src_index(tex, nir_tex_src_bias)].src.ssa);
      }
      linear =
         nir_bcsel(b, nir_fle_imm(b, lod, 0.0),
                   nir_imm_bool(b, sampler->mag_filter == VK_FILTER_LINEAR),
                   nir_imm_bool(b, sampler->min_filter == VK_FILTER_LINEAR));
   }

   nir_def *pos = nir_bcsel(b, linear, nir_fadd_imm(b, uv, -0.5), uv);
   nir_def *base = nir_ffloor(b, pos);
   nir_def *weight =
      nir_bcsel(b, linear, nir_ffract(b, pos), nir_imm_vec2(b, 0, 0));
   nir_def *taps[4];
   for (unsigned tap = 0; tap < 4; ++tap) {
      nir_def *xy = nir_fadd(b, base, nir_imm_vec2(b, tap & 1, tap >> 1));
      xy = nir_fmin(b, nir_fmax(b, xy, nir_imm_float(b, 0)),
                    nir_fadd_imm(b, size, -1));
      xy = nir_fdiv(b, nir_fadd_imm(b, xy, 0.5), size);
      nir_def *center = tex->is_array ? nir_vec3(b, nir_channel(b, xy, 0),
                                                 nir_channel(b, xy, 1),
                                                 nir_channel(b, coord, 2))
                                      : xy;
      nir_def *lod_zero = nir_imm_float(b, 0);
      nir_tex_instr *sample =
         nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
      /* Register cloned source uses before removing or rewriting them. */
      nir_builder_instr_insert(b, &sample->instr);
      for (int s = sample->num_srcs - 1; s >= 0; --s) {
         switch (sample->src[s].src_type) {
         case nir_tex_src_lod:
         case nir_tex_src_bias:
         case nir_tex_src_ddx:
         case nir_tex_src_ddy:
         case nir_tex_src_offset:
            nir_tex_instr_remove_src(sample, s);
            break;
         case nir_tex_src_coord:
            nir_src_rewrite(&sample->src[s].src, center);
            break;
         default:
            break;
         }
      }
      sample->op = nir_texop_txl;
      nir_tex_instr_add_src(sample, nir_tex_src_lod, lod_zero);
      taps[tap] = &sample->def;
   }
   nir_def *x = nir_channel(b, weight, 0), *y = nir_channel(b, weight, 1);
   /* Frontend preprocessing may already have lowered the original flrps. */
   b->shader->info.flrp_lowered = false;
   nir_def *result = nir_flrp(b, nir_flrp(b, taps[0], taps[1], x),
                              nir_flrp(b, taps[2], taps[3], x), y);
   nir_def_replace(&tex->def, result);
   return true;
}

/* Look up the conversion of a plane access produced by the common pass. */
static const struct vk_ycbcr_conversion_state *
plane_tex_conversion(nir_tex_instr *tex, const void *state, unsigned *plane)
{
   const int plane_src = nir_tex_instr_src_index(tex, nir_tex_src_plane);
   const int deref_src = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (plane_src < 0 || deref_src < 0 || !nir_src_is_const(tex->src[plane_src].src))
      return NULL;

   nir_deref_instr *deref = nir_src_as_deref(tex->src[deref_src].src);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned array_index = 0;
   if (deref->deref_type != nir_deref_type_var) {
      if (deref->deref_type != nir_deref_type_array ||
          !nir_src_is_const(deref->arr.index))
         return NULL;
      array_index = nir_src_as_uint(deref->arr.index);
   }
   *plane = nir_src_as_uint(tex->src[plane_src].src);
   return lookup_ycbcr_conversion(state, var->data.descriptor_set,
                                  var->data.binding, array_index);
}

/* The texture unit filters 16-bit UNORM planes at 16-bit precision: a
 * linear blend of two texels rounds to a whole code, half a code away from
 * the exact result. Chroma reconstruction must keep the format's precision
 * (the conversion tests bound it to a 2^-16 step), so blend 16-bit chroma
 * planes in the shader from nearest fetches. Multi-planar images have one
 * mip level and YCbCr samplers clamp to edge, so the blend of the four
 * clamped neighbours at the sample position is the linear filter result. */
static bool
lower_ycbcr_chroma_blend(nir_builder *b, nir_tex_instr *tex, void *state)
{
   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
      break;
   default:
      return false;
   }
   const int coord_src = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord_src < 0 || tex->sampler_dim != GLSL_SAMPLER_DIM_2D)
      return false;

   unsigned plane;
   const struct vk_ycbcr_conversion_state *conversion =
      plane_tex_conversion(tex, state, &plane);
   if (!conversion || conversion->chroma_filter != VK_FILTER_LINEAR)
      return false;
   const struct vk_format_ycbcr_info *info =
      vk_format_get_ycbcr_info(conversion->format);
   assert(info && plane < info->n_planes);
   if (!info->planes[plane].has_chroma)
      return false;
   switch (info->planes[plane].format) {
   case VK_FORMAT_R16_UNORM:
   case VK_FORMAT_R16G16_UNORM:
      break;
   default:
      return false;
   }

   const int deref_src = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   nir_def *texture = tex->src[deref_src].src.ssa;
   b->cursor = nir_before_instr(&tex->instr);

   nir_tex_instr *txs = nir_tex_instr_create(b->shader, 2);
   txs->op = nir_texop_txs;
   txs->sampler_dim = tex->sampler_dim;
   txs->is_array = tex->is_array;
   txs->dest_type = nir_type_int32;
   txs->src[0] = nir_tex_src_for_ssa(nir_tex_src_texture_deref, texture);
   txs->src[1] = nir_tex_src_for_ssa(nir_tex_src_plane, nir_imm_int(b, plane));
   nir_def_init(&txs->instr, &txs->def, nir_tex_instr_dest_size(txs), 32);
   nir_builder_instr_insert(b, &txs->instr);
   nir_def *size = nir_channels(b, &txs->def, 3);

   /* The texture unit converts the coordinate to fixed point with
    * subTexelPrecisionBits fractional bits before filtering. Do the same, so
    * a sample that lies on a texel centre or boundary in that precision gets
    * the exact weight rather than a rounding residue of the float scaling. */
   nir_def *coord = tex->src[coord_src].src.ssa;
   nir_def *pos = nir_fadd_imm(
      b, nir_fmul(b, nir_channels(b, coord, 3), nir_i2f32(b, size)), -0.5);
   pos = nir_fmul_imm(b, nir_fround_even(b, nir_fmul_imm(b, pos, 256.0)),
                      1.0 / 256.0);
   nir_def *base = nir_ffloor(b, pos);
   nir_def *weight = nir_fsub(b, pos, base);
   nir_def *ibase = nir_f2i32(b, base);
   const int offset_src = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset_src >= 0)
      ibase = nir_iadd(b, ibase, nir_channels(b, tex->src[offset_src].src.ssa, 3));

   nir_def *layer = NULL;
   if (tex->is_array) {
      layer = nir_f2i32(b, nir_fround_even(b, nir_channel(b, coord, 2)));
      layer = nir_imin(b, nir_imax(b, layer, nir_imm_int(b, 0)),
                       nir_iadd_imm(b, nir_channel(b, &txs->def, 2), -1));
   }

   nir_def *taps[4];
   for (unsigned tap = 0; tap < 4; ++tap) {
      nir_def *xy = nir_iadd(b, ibase, nir_imm_ivec2(b, tap & 1, tap >> 1));
      xy = nir_imin(b, nir_imax(b, xy, nir_imm_int(b, 0)),
                    nir_iadd_imm(b, size, -1));
      nir_def *texel = layer ? nir_vec3(b, nir_channel(b, xy, 0),
                                        nir_channel(b, xy, 1), layer)
                             : xy;
      nir_tex_instr *fetch = nir_tex_instr_create(b->shader, 4);
      fetch->op = nir_texop_txf;
      fetch->sampler_dim = tex->sampler_dim;
      fetch->is_array = tex->is_array;
      fetch->dest_type = tex->dest_type;
      fetch->coord_components = texel->num_components;
      fetch->src[0] = nir_tex_src_for_ssa(nir_tex_src_texture_deref, texture);
      fetch->src[1] = nir_tex_src_for_ssa(nir_tex_src_coord, texel);
      fetch->src[2] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_imm_int(b, 0));
      fetch->src[3] = nir_tex_src_for_ssa(nir_tex_src_plane, nir_imm_int(b, plane));
      nir_def_init(&fetch->instr, &fetch->def, tex->def.num_components,
                   tex->def.bit_size);
      nir_builder_instr_insert(b, &fetch->instr);
      taps[tap] = &fetch->def;
   }

   nir_def *x = nir_channel(b, weight, 0), *y = nir_channel(b, weight, 1);
   if (tex->def.bit_size != 32) {
      x = nir_f2fN(b, x, tex->def.bit_size);
      y = nir_f2fN(b, y, tex->def.bit_size);
   }
   b->shader->info.flrp_lowered = false;
   nir_def *result = nir_flrp(b, nir_flrp(b, taps[0], taps[1], x),
                              nir_flrp(b, taps[2], taps[3], x), y);
   nir_def_replace(&tex->def, result);
   return true;
}

/* hk_format_to_pipe_format samples X6R10/X4R12 planes through R16_UNORM.
 * Restore their logical UNORM range before range expansion and YCbCr matrix
 * conversion. The correction commutes with filtering; absent channels and
 * alpha must retain their descriptor defaults. */
static bool
lower_ycbcr_plane_scale(nir_builder *b, nir_tex_instr *tex, void *state)
{
   const int plane_src = nir_tex_instr_src_index(tex, nir_tex_src_plane);
   const int deref_src = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (plane_src < 0 || deref_src < 0 || tex->op == nir_texop_txs ||
       tex->op == nir_texop_query_levels || tex->op == nir_texop_lod)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(tex->src[deref_src].src);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned array_index = 0;
   if (deref->deref_type != nir_deref_type_var) {
      if (deref->deref_type != nir_deref_type_array ||
          !nir_src_is_const(deref->arr.index))
         return false;
      array_index = nir_src_as_uint(deref->arr.index);
   }
   const struct vk_ycbcr_conversion_state *conversion =
      lookup_ycbcr_conversion(state, var->data.descriptor_set,
                              var->data.binding, array_index);
   if (!conversion)
      return false;
   const struct vk_format_ycbcr_info *info =
      vk_format_get_ycbcr_info(conversion->format);
   unsigned plane = nir_src_as_uint(tex->src[plane_src].src);
   assert(info && plane < info->n_planes);

   unsigned bits, channels;
   switch (info->planes[plane].format) {
   case VK_FORMAT_R10X6_UNORM_PACK16:
      bits = 10;
      channels = 1;
      break;
   case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
      bits = 10;
      channels = 2;
      break;
   case VK_FORMAT_R12X4_UNORM_PACK16:
      bits = 12;
      channels = 1;
      break;
   case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:
      bits = 12;
      channels = 2;
      break;
   default:
      return false;
   }
   const float scale = 65535.0f / (((1u << bits) - 1u) << (16 - bits));
   b->cursor = nir_after_instr(&tex->instr);
   nir_def *components[4];
   assert(tex->def.num_components == 4);
   for (unsigned c = 0; c < 4; ++c) {
      components[c] = nir_channel(b, &tex->def, c);
      if (c < channels)
         components[c] = nir_fmul_imm(b, components[c], scale);
   }
   nir_def *scaled = nir_vec(b, components, 4);
   nir_def_rewrite_uses_after(&tex->def, scaled);
   return true;
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

/*
 * This is the world's worst multiview implementation. We simply duplicate each
 * draw on the CPU side, changing a uniform in between, and then plumb the view
 * index into the layer ID here. Whatever, it works.
 *
 * The "proper" implementation on AGX would use vertex amplification, but a
 * MacBook is not a VR headset.
 */
static void
hk_lower_multiview(nir_shader *nir)
{
   /* If there's an existing layer ID write, ignore it. This avoids validation
    * splat with vk_meta.
    */
   nir_variable *existing = nir_find_variable_with_location(
      nir, nir_var_shader_out, VARYING_SLOT_LAYER);

   if (existing) {
      existing->data.mode = nir_var_shader_temp;
      existing->data.location = 0;
      nir_fixup_deref_modes(nir);
   }

   /* Now write the view index as the layer */
   nir_builder b =
      nir_builder_at(nir_after_impl(nir_shader_get_entrypoint(nir)));

   nir_variable *layer =
      nir_variable_create(nir, nir_var_shader_out, glsl_uint_type(), NULL);

   layer->data.location = VARYING_SLOT_LAYER;

   nir_store_var(&b, layer, nir_load_view_index(&b), nir_component_mask(1));
   b.shader->info.outputs_written |= VARYING_BIT_LAYER;
}

static nir_def *
query_custom_border(nir_builder *b, nir_tex_instr *tex)
{
   nir_def *border = nir_build_texture_query(
      b, tex, nir_texop_custom_border_color_agx, 4, tex->dest_type, false, false);
   if (tex->op == nir_texop_tg4)
      nir_instr_as_tex(nir_def_instr(border))->backend_flags |=
         AGX_TEXTURE_FLAG_BORDER_UNFILTERED;
   return border;
}

static nir_def *
has_custom_border(nir_builder *b, nir_tex_instr *tex)
{
   return nir_build_texture_query(b, tex, nir_texop_has_custom_border_color_agx,
                                  1, nir_type_bool1, false, false);
}

/* A shadow gather compares each unfiltered depth value separately. Recover
 * custom border depths before comparing: two already-compared 0/1 border
 * probes cannot distinguish a border when the reference is outside [0, 1]. */
static nir_def *
compare_gather(nir_builder *b, nir_tex_instr *tex, nir_def *depth,
               bool software_sampler)
{
   nir_def *ref = tex->src[nir_tex_instr_src_index(tex, nir_tex_src_comparator)].src.ssa;
   nir_def *sampler = tex->src[nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref)].src.ssa;
   nir_def *state = nir_load_sampler_descriptor_agx(b, sampler);
   nir_def *func = nir_ubitfield_extract_imm(
      b, nir_channel(b, state, 1), software_sampler ? HK_MINMAX_COMPARE_SHIFT : 7, 3);
   nir_def *tests[] = {
      nir_fge(b, depth, ref), nir_fge(b, ref, depth),
      nir_flt(b, ref, depth), nir_flt(b, depth, ref),
      nir_feq(b, ref, depth), nir_fneu(b, ref, depth),
      nir_imm_true(b), nir_imm_false(b),
   };
   nir_def *result = nir_imm_zero(b, 4, 1);
   for (unsigned i = 0; i < ARRAY_SIZE(tests); ++i)
      result = nir_bcsel(b, nir_ieq_imm(b, func, i), tests[i], result);
   return nir_b2f32(b, result);
}

static bool
lower_shadow_gather_border(nir_builder *b, nir_tex_instr *tex,
                           bool software_sampler)
{
   if (tex->op != nir_texop_tg4)
      return false;

   b->cursor = nir_after_instr(&tex->instr);
   nir_def *has_custom = has_custom_border(b, tex);
   nir_tex_instr *shadow =
      nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
   nir_builder_instr_insert(b, &shadow->instr);

   nir_push_if(b, has_custom);
   nir_def *raw[2];
   for (unsigned i = 0; i < 2; ++i) {
      nir_tex_instr *tap =
         nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
      nir_builder_instr_insert(b, &tap->instr);
      tap->is_shadow = tap->is_new_style_shadow = false;
      nir_tex_instr_remove_src(tap,
         nir_tex_instr_src_index(tap, nir_tex_src_comparator));
      if (i == 0)
         tap->backend_flags |= AGX_TEXTURE_FLAG_CLAMP_TO_0;
      raw[i] = nir_trim_vector(b, &tap->def, 4);
   }

   /* Compare operations lose the information needed to distinguish border
    * taps from interior taps. Probe the unfiltered depth values with both
    * hardware borders, then replace only border comparisons.
    */
   nir_def *border = nir_channel(b, query_custom_border(b, tex), 0);
   nir_def *ref =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_comparator)].src.ssa;
   ref = nir_f2fN(b, ref, border->bit_size);
   nir_def *texture =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_texture_deref)].src.ssa;
   nir_def *word = nir_load_texture_descriptor_word_agx(b, texture);
   nir_def *normalized = nir_ieq_imm(
      b, nir_ubitfield_extract_imm(b, word, 13, 3), AGX_TEXTURE_TYPE_UNORM);
   ref = nir_bcsel(b, normalized, nir_fsat(b, ref), ref);
   border = nir_bcsel(b, normalized, nir_fsat(b, border), border);

   nir_def *sampler =
      tex->src[nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref)].src.ssa;
   nir_def *state = nir_load_sampler_descriptor_agx(b, sampler);
   /* Gen4, HAL200 and Gen5 all store the comparison function at bits 39:41. */
   nir_def *func = nir_ubitfield_extract_imm(
      b, nir_channel(b, state, 1),
      software_sampler ? HK_MINMAX_COMPARE_SHIFT : 7, 3);
   nir_def *comparisons[] = {
      nir_fge(b, border, ref), nir_fge(b, ref, border),
      nir_flt(b, ref, border), nir_flt(b, border, ref),
      nir_feq(b, ref, border), nir_fneu(b, ref, border),
      nir_imm_true(b), nir_imm_false(b),
   };
   nir_def *pass = comparisons[AGX_COMPARE_FUNC_NEVER];
   for (int i = AGX_COMPARE_FUNC_ALWAYS; i >= 0; --i)
      pass = nir_bcsel(b, nir_ieq_imm(b, func, i), comparisons[i], pass);
   nir_def *result = nir_bcsel(b, nir_ieq(b, raw[0], raw[1]),
      nir_trim_vector(b, &shadow->def, 4), nir_b2fN(b, pass, tex->def.bit_size));
   if (tex->is_sparse)
      result = nir_vec5(b, nir_channel(b, result, 0), nir_channel(b, result, 1),
                       nir_channel(b, result, 2), nir_channel(b, result, 3),
                       nir_channel(b, &shadow->def, 4));
   nir_pop_if(b, NULL);
   nir_def_replace(&tex->def, nir_if_phi(b, result, &shadow->def));
   return true;
}


struct hk_border_options {
   bool g15_shadow_gather;
   bool shadow_gather;
   bool software_sampler;
};

static bool
lower(nir_builder *b, nir_tex_instr *tex, void *_data)
{
   if (tex->backend_flags & AGX_TEXTURE_FLAG_PLAIN_SAMPLER) {
      tex->backend_flags &= ~AGX_TEXTURE_FLAG_PLAIN_SAMPLER;
      return true;
   }
   if (!nir_tex_instr_need_sampler(tex) || nir_tex_instr_is_query(tex))
      return false;

   const struct hk_border_options *options = _data;
   if (tex->is_shadow && options->g15_shadow_gather)
      return lower_shadow_gather_border(b, tex, options->software_sampler);
   const bool shadow_gather = tex->is_shadow && tex->op == nir_texop_tg4 &&
                              options->shadow_gather;
   if (tex->is_shadow && !shadow_gather)
      return false;

   b->cursor = nir_after_instr(&tex->instr);
   nir_def *has_custom = has_custom_border(b, tex);

   nir_instr *orig = nir_instr_clone(b->shader, &tex->instr);
   nir_builder_instr_insert(b, orig);
   nir_def *clamp_to_1 = &nir_instr_as_tex(orig)->def;

   nir_push_if(b, has_custom);
   nir_def *replaced = NULL;
   {
      nir_tex_instr *unfiltered = tex;
      nir_def *border_1 = clamp_to_1;
      if (shadow_gather) {
         unfiltered = nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
         nir_builder_instr_insert(b, &unfiltered->instr);
         nir_tex_instr_remove_src(unfiltered,
            nir_tex_instr_src_index(unfiltered, nir_tex_src_comparator));
         unfiltered->is_shadow = unfiltered->is_new_style_shadow = false;
         border_1 = &unfiltered->def;
      }
      /* Sample again, this time with clamp-to-0 instead of clamp-to-1 */
      nir_instr *clone_instr = nir_instr_clone(b->shader, &unfiltered->instr);
      nir_builder_instr_insert(b, clone_instr);

      nir_tex_instr *tex_0 = nir_instr_as_tex(clone_instr);
      nir_def *clamp_to_0 = &tex_0->def;

      tex_0->backend_flags |= AGX_TEXTURE_FLAG_CLAMP_TO_0;

      /* Grab the border colour */
      nir_def *border = query_custom_border(b, tex_0);

      if (tex->op == nir_texop_tg4) {
         border = nir_replicate(b, nir_channel(b, border, tex->component), 4);
      }

      /* Border arithmetic operates on colour, never the sparse residency
       * code. The two address modes touch the same mapped texels. */
      nir_def *colour_1 = tex->is_sparse ? nir_trim_vector(b, border_1, 4)
                                         : border_1;
      nir_def *colour_0 = tex->is_sparse ? nir_trim_vector(b, clamp_to_0, 4)
                                         : clamp_to_0;

      /* Combine together with the border */
      if (nir_alu_type_get_base_type(tex->dest_type) == nir_type_float &&
          tex->op != nir_texop_tg4) {

         /* For floats, lerp together:
          *
          * For border texels:  (1 * border) + (0 * border      ) = border
          * For regular texels: (x * border) + (x * (1 - border)) = x.
          *
          * Linear filtering is linear (duh), so lerping is compatible.
          */
         replaced = nir_flrp(b, colour_0, colour_1, border);
         b->shader->info.flrp_lowered = false;
      } else {
         /* For integers, just select componentwise since there is no linear
          * filtering. Gathers also use this path since they are unfiltered in
          * each component.
          */
         replaced = nir_bcsel(b, nir_ieq(b, colour_0, colour_1), colour_0,
                              border);
      }
      if (shadow_gather)
         replaced = compare_gather(b, tex, replaced, options->software_sampler);
      if (tex->is_sparse)
         replaced = nir_vec5(b, nir_channel(b, replaced, 0),
                             nir_channel(b, replaced, 1),
                             nir_channel(b, replaced, 2),
                             nir_channel(b, replaced, 3),
                             nir_channel(b, clamp_to_1, 4));
   }
   nir_pop_if(b, NULL);

   /* Put it together with a phi */
   nir_def *phi = nir_if_phi(b, replaced, clamp_to_1);
   nir_def_replace(&tex->def, phi);
   return true;
}

static bool
agx_nir_lower_custom_border(nir_shader *nir, bool shadow_gather, bool software_sampler,
                            bool g15_shadow_gather)
{
   struct hk_border_options options = {g15_shadow_gather, shadow_gather, software_sampler};
   return nir_shader_tex_pass(nir, lower, nir_metadata_none, &options);
}

/* R64 integer storage views use native RG32 descriptors. Keep atomics in
 * their logical format so address lowering still sees an eight-byte texel. */
static bool
lower_int64_storage(nir_builder *b, nir_intrinsic_instr *intr, void *unused)
{
   const bool sparse =
      intr->intrinsic == nir_intrinsic_bindless_image_sparse_load;
   const bool load = sparse ||
      intr->intrinsic == nir_intrinsic_bindless_image_load;
   const bool store = intr->intrinsic == nir_intrinsic_bindless_image_store;
   if ((!load && !store) ||
       (load ? intr->def.bit_size : nir_src_bit_size(intr->src[3])) != 64)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   if (store) {
      nir_def *value = nir_channel(b, intr->src[3].ssa, 0);
      nir_src_rewrite(&intr->src[3],
         nir_vec4(b, nir_unpack_64_2x32_split_x(b, value),
                     nir_unpack_64_2x32_split_y(b, value),
                     nir_imm_int(b, 0), nir_imm_int(b, 0)));
      nir_intrinsic_set_src_type(intr, nir_type_uint32);
      nir_intrinsic_set_format(intr, PIPE_FORMAT_R32G32_UINT);
      return true;
   }

   nir_intrinsic_instr *copy = nir_instr_as_intrinsic(
      nir_instr_clone(b->shader, &intr->instr));
   copy->def.bit_size = 32;
   copy->def.num_components = sparse ? 5 : 4;
   copy->num_components = sparse ? 5 : 4;
   nir_intrinsic_set_dest_type(copy, nir_type_uint32);
   nir_intrinsic_set_format(copy, PIPE_FORMAT_R32G32_UINT);
   nir_builder_instr_insert(b, &copy->instr);
   nir_def *components[] = {
      nir_pack_64_2x32_split(b, nir_channel(b, &copy->def, 0),
                               nir_channel(b, &copy->def, 1)),
      nir_imm_int64(b, 0), nir_imm_int64(b, 0), nir_imm_int64(b, 1),
      sparse ? nir_u2u64(b, nir_channel(b, &copy->def, 4)) : NULL,
   };
   nir_def_replace(&intr->def,
                   nir_vec(b, components, intr->def.num_components));
   return true;
}

/* Unformatted float images may name scaled views. Their native descriptor
 * uses integer texels; the shader performs the Vulkan numerical conversion.
 * The descriptor's existing type/channel fields avoid a new software ABI. */
static bool
lower_scaled_storage(nir_builder *b, nir_intrinsic_instr *intr, void *unused)
{
   const bool load = intr->intrinsic == nir_intrinsic_bindless_image_load;
   const bool store = intr->intrinsic == nir_intrinsic_bindless_image_store;
   if ((!load && !store) || nir_intrinsic_format(intr) != PIPE_FORMAT_NONE ||
       (load ? nir_intrinsic_dest_type(intr) : nir_intrinsic_src_type(intr)) != nir_type_float32)
      return false;
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *address = nir_load_from_texture_handle_agx(b, intr->src[0].ssa);
   nir_def *header = nir_load_global_constant(b, 1, 32, address, .align_mul = 4);
   nir_def *type = nir_iand_imm(b, nir_ushr_imm(b, header, 13), 7);
   nir_def *is_signed = nir_ieq_imm(b, type, AGX_TEXTURE_TYPE_SINT);
   nir_def *is_integer = nir_ior(b, is_signed,
      nir_ieq_imm(b, type, AGX_TEXTURE_TYPE_UINT));
   if (load) {
      b->cursor = nir_after_instr(&intr->instr);
      nir_def *value = &intr->def;
      nir_def *converted = nir_bcsel(b, is_signed,
         nir_i2f32(b, value), nir_u2f32(b, value));
      nir_def_rewrite_uses_after(value, nir_bcsel(b, is_integer, converted, value));
      return true;
   }

   /* Native storage PBEs use linear UNORM; retain the view's gamma flag in
    * the software descriptor (PBE bit 125) and encode RGB in the shader.
    * sRGB images have no formatted SPIR-V storage-image type, so this path
    * covers their stores without changing integer or formatted operations. */
   nir_def *word3 = nir_load_global_constant(b, 1, 32,
      nir_iadd_imm(b, address, 12), .align_mul = 4);
   nir_def *srgb = nir_ine_imm(b, nir_iand_imm(b, word3, 1u << 29), 0);
   nir_def *original = intr->src[3].ssa;
   nir_push_if(b, srgb);
   nir_def *encoded = nir_format_linear_to_srgb(b, nir_fsat(b, original));
   if (original->num_components == 4)
      encoded = nir_vector_insert_imm(b, encoded, nir_channel(b, original, 3), 3);
   nir_push_else(b, NULL);
   nir_pop_if(b, NULL);
   nir_src_rewrite(&intr->src[3], nir_if_phi(b, encoded, original));

   nir_def *channels = nir_iand_imm(b, nir_ushr_imm(b, header, 6), 127);
   nir_def *is_8 = nir_ior(b,
      nir_ieq_imm(b, channels, AGX_CHANNELS_R8), nir_ior(b,
      nir_ieq_imm(b, channels, AGX_CHANNELS_R8G8),
      nir_ieq_imm(b, channels, AGX_CHANNELS_R8G8B8A8)));
   nir_def *is_16 = nir_ior(b,
      nir_ieq_imm(b, channels, AGX_CHANNELS_R16), nir_ior(b,
      nir_ieq_imm(b, channels, AGX_CHANNELS_R16G16),
      nir_ieq_imm(b, channels, AGX_CHANNELS_R16G16B16A16)));
   nir_push_if(b, nir_iand(b, is_integer, nir_ior(b, is_8, is_16)));
   {
      nir_def *limit = nir_bcsel(b, is_8, nir_imm_float(b, 256),
                                             nir_imm_float(b, 65536));
      nir_def *signed_limit = nir_fmul_imm(b, limit, 0.5);
      nir_def *upper = nir_fadd_imm(b,
         nir_bcsel(b, is_signed, signed_limit, limit), -1);
      nir_def *lower = nir_bcsel(b, is_signed, nir_fneg(b, signed_limit),
                                               nir_imm_float(b, 0));
      nir_def *rounded = nir_fround_even(b, intr->src[3].ssa);
      nir_def *clamped = nir_fmin(b, nir_fmax(b, rounded, lower), upper);
      nir_def *value = nir_bcsel(b, is_signed, nir_f2i32(b, clamped),
                                              nir_f2u32(b, clamped));
      nir_push_if(b, is_8);
      for (unsigned wide = 0; wide < 2; ++wide) {
         if (wide)
            nir_push_else(b, NULL);
         nir_intrinsic_instr *copy = nir_instr_as_intrinsic(
            nir_instr_clone(b->shader, &intr->instr));
         nir_builder_instr_insert(b, &copy->instr);
         nir_src_rewrite(&copy->src[3], value);
         /* The descriptor retains signedness and channel count. The typed
          * intrinsic selects the matching raw I8/I16 staging instruction. */
         nir_intrinsic_set_format(copy, wide ? PIPE_FORMAT_R16G16B16A16_UINT
                                            : PIPE_FORMAT_R8G8B8A8_UINT);
         nir_intrinsic_set_src_type(copy, nir_type_uint32);
      }
      nir_pop_if(b, NULL);
   }
   nir_push_else(b, NULL);
   nir_builder_instr_insert(b, nir_instr_clone(b->shader, &intr->instr));
   nir_pop_if(b, NULL);
   nir_instr_remove(&intr->instr);
   return true;
}

/* Linear texel buffers are backed by valid zero/scratch pages when unbound.
 * Unlike sparse images, their hardware descriptor has no sparse page table.
 * Consult the buffer's bind metadata for residency while using the ordinary
 * texture operation for its value. Descriptor copies retain a live map pointer,
 * so queue rebinds do not require descriptor updates or shader recompilation.
 */
static bool
lower_sparse_buffer_or_ms(nir_builder *b, nir_instr *instr, void *data)
{
   const bool native_ms_residency = data != NULL;
   nir_def *handle, *coord, *value;
   nir_instr *clone;
   bool multisample = false;
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      multisample = tex->sampler_dim == GLSL_SAMPLER_DIM_MS;
      if (multisample && native_ms_residency)
         return false;
      if (!tex->is_sparse ||
          (tex->sampler_dim != GLSL_SAMPLER_DIM_BUF && !multisample))
         return false;
      int h = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
      int c = nir_tex_instr_src_index(tex, nir_tex_src_coord);
      assert(h >= 0 && c >= 0);
      handle = tex->src[h].src.ssa;
      coord = tex->src[c].src.ssa;
      clone = nir_instr_clone(b->shader, instr);
      nir_tex_instr *load = nir_instr_as_tex(clone);
      load->is_sparse = false;
      load->def.num_components = 4;
      value = &load->def;
   } else if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic != nir_intrinsic_bindless_image_sparse_load)
         return false;
      multisample = nir_intrinsic_image_dim(intr) == GLSL_SAMPLER_DIM_MS;
      if (multisample && native_ms_residency)
         return false;
      if (nir_intrinsic_image_dim(intr) != GLSL_SAMPLER_DIM_BUF && !multisample)
         return false;
      handle = intr->src[0].ssa;
      b->cursor = nir_before_instr(instr);
      coord = nir_channel(b, intr->src[1].ssa, 0);
      clone = nir_instr_clone(b->shader, instr);
      nir_intrinsic_instr *load = nir_instr_as_intrinsic(clone);
      load->intrinsic = nir_intrinsic_bindless_image_load;
      load->num_components = 4;
      load->def.num_components = 4;
      value = &load->def;
   } else {
      return false;
   }

   b->cursor = nir_before_instr(instr);
   nir_instr_insert(b->cursor, clone);
   b->cursor = nir_after_instr(clone);
   /* On unqualified chips no multisample sparse residency is exposed. Every valid MS
    * image access is therefore resident, including sparse-bound MS images
    * which must have all memory bound before use. */
   if (multisample) {
      nir_def *result = nir_vec5(b, nir_channel(b, value, 0),
         nir_channel(b, value, 1), nir_channel(b, value, 2),
         nir_channel(b, value, 3), nir_imm_int(b, 0));
      nir_def_replace(instr->type == nir_instr_type_tex ?
         &nir_instr_as_tex(instr)->def : &nir_instr_as_intrinsic(instr)->def, result);
      return true;
   }
   nir_def *desc = nir_load_from_texture_handle_agx(b, handle);
   nir_def *metadata = nir_iadd_imm(b, desc, HK_TEXEL_BUFFER_RESIDENCY_OFFSET);
   nir_def *map = nir_load_global_constant(b, 1, 64, metadata, .align_mul = 8);
   nir_def *info = nir_load_global_constant(b, 3, 32,
      nir_iadd_imm(b, metadata, 8), .align_mul = 4);
   nir_def *in_bounds = nir_ult(b, coord, nir_channel(b, info, 2));
   nir_push_if(b, nir_iand(b, in_bounds, nir_ine_imm(b, map, 0)));
   /* A maximum RGBA32 view can span 4 GiB including its page offset. */
   nir_def *size = nir_u2u64(b, nir_channel(b, info, 1));
   nir_def *offset = nir_iadd(b, nir_u2u64(b, nir_channel(b, info, 0)),
                            nir_imul(b, nir_u2u64(b, coord), size));
   nir_def *last = nir_iadd(b, offset, nir_iadd_imm(b, size, -1));
   nir_def *first_addr = nir_iadd(b, map,
      nir_ishl_imm(b, nir_ushr_imm(b, offset, 14), 2));
   nir_def *last_addr = nir_iadd(b, map,
      nir_ishl_imm(b, nir_ushr_imm(b, last, 14), 2));
   nir_def *first_resident = nir_load_global(b, 1, 32, first_addr, .align_mul = 4);
   nir_def *last_resident = nir_load_global(b, 1, 32, last_addr, .align_mul = 4);
   nir_def *missing = nir_ixor(b, nir_iand(b, first_resident, last_resident), nir_imm_int(b, 1));
   nir_push_else(b, NULL);
   nir_def *resident = nir_imm_int(b, 0) /* AGX resident code */;
   nir_pop_if(b, NULL);
   nir_def *code = nir_if_phi(b, missing, resident);
   nir_def *result = nir_vec5(b, nir_channel(b, value, 0),
      nir_channel(b, value, 1), nir_channel(b, value, 2),
      nir_channel(b, value, 3), code);
   nir_def *old = instr->type == nir_instr_type_tex ?
      &nir_instr_as_tex(instr)->def : &nir_instr_as_intrinsic(instr)->def;
   nir_def_replace(old, result);
   return true;
}

/* Image-view formats are dynamic descriptor state. Recover scaled samples
 * from the integer descriptor result without changing ordinary float samples. */
static bool
lower_scaled_sample(nir_builder *b, nir_tex_instr *tex, void *unused)
{
   if (tex->dest_type != nir_type_float32 || tex->is_shadow ||
       tex->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return false;
   switch (tex->op) {
   case nir_texop_tex: case nir_texop_txb: case nir_texop_txl:
   case nir_texop_txd: case nir_texop_txf: case nir_texop_txf_ms:
   case nir_texop_tg4:
      break;
   default:
      return false;
   }
   const int handle = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
   if (handle < 0)
      return false;
   const bool branch = debug_get_bool_option("HK_NATIVE_BRANCH_SCALED", true);
   nir_tex_instr *original = tex;
   if (branch) {
      /* The replacement crosses blocks. Clone the sample before replacing
       * its old SSA definition; rewrite_uses_after requires one block. */
      b->cursor = nir_before_instr(&tex->instr);
      tex = nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
      nir_builder_instr_insert(b, &tex->instr);
   }
   b->cursor = nir_before_instr(&tex->instr);
   nir_def *desc = nir_load_from_texture_handle_agx(b, tex->src[handle].src.ssa);
   const unsigned offset = 32 +
      offsetof(struct hk_sampled_image_descriptor_sw, scaled_conversion);
   nir_def *kind = nir_load_global_constant(b, 1, 16,
      nir_iadd_imm(b, desc, offset), .align_mul = 2);
   b->cursor = nir_after_instr(&tex->instr);
   nir_def *value = tex->is_sparse ? nir_trim_vector(b, &tex->def, 4) : &tex->def;
   if (branch) {
      /* Ordinary native formats need none of the scaled-format arithmetic.
       * Keep the texture operation outside control flow (implicit derivatives
       * and sparse residency retain their original execution). */
      nir_if *nif = nir_push_if(b, nir_ine_imm(b, kind, 0));
      nif->control = nir_selection_control_dont_flatten;
   }
   nir_def *converted = nir_bcsel(b, nir_ieq_imm(b, kind, 1),
      nir_i2f32(b, value), nir_u2f32(b, value));
   nir_def *scale = nir_bcsel(b, nir_ine_imm(b, nir_iand_imm(b, kind, 4), 0),
                             nir_imm_float(b, 65535), nir_imm_float(b, 255));
   scale = nir_bcsel(b, nir_ine_imm(b, nir_iand_imm(b, kind, 128), 0),
                       nir_imm_float(b, 1023), scale);
   nir_def *factors[4];
   for (unsigned c = 0; c < value->num_components; ++c) {
      unsigned component = tex->op == nir_texop_tg4 ? tex->component : c;
      factors[c] = nir_bcsel(b,
         nir_ine_imm(b, nir_iand_imm(b, kind, BITFIELD_BIT(3 + component)), 0),
         nir_bcsel(b, nir_ine_imm(b, nir_iand_imm(b, kind, BITFIELD_BIT(8 + component)), 0),
                       nir_imm_float(b, 3), scale), nir_imm_float(b, 1));
   }
   nir_def *normalized = nir_fmul(b, value, nir_vec(b, factors, value->num_components));
   if (tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms || tex->op == nir_texop_tg4)
      normalized = nir_fround_even(b, normalized);
   converted = nir_bcsel(b, nir_ieq_imm(b, nir_iand_imm(b, kind, 3), 3),
                           normalized, converted);
   nir_def *result;
   if (branch) {
      nir_push_else(b, NULL);
      nir_pop_if(b, NULL);
      result = nir_if_phi(b, converted, value);
   } else {
      result = nir_bcsel(b, nir_ine_imm(b, kind, 0), converted, value);
   }
   if (tex->is_sparse)
      result = nir_vec5(b, nir_channel(b, result, 0),
                        nir_channel(b, result, 1),
                        nir_channel(b, result, 2),
                        nir_channel(b, result, 3),
                        nir_channel(b, &tex->def, 4));
   if (branch)
      nir_def_replace(&original->def, result);
   else
      nir_def_rewrite_uses_after(&tex->def, result);
   return true;
}

static nir_def *
query_min_lod(nir_builder *b, nir_tex_instr *tex, bool int_coords)
{
   nir_alu_type T = int_coords ? nir_type_uint16 : nir_type_float16;
   return nir_build_texture_query(b, tex, nir_texop_image_min_lod_agx, 1, T,
                                  false, false);
}

static bool
lower_min_lod(nir_builder *b, nir_tex_instr *tex, UNUSED void *_data)
{
   /* Reduction already selected the two levels using the view minimum.
    * Clamping each centre tap again would suppress the lower level. */
   if (tex->backend_flags & AGX_TEXTURE_FLAG_MINMAX_TAP) {
      tex->backend_flags &= ~AGX_TEXTURE_FLAG_MINMAX_TAP;
      return true;
   }
   if (nir_tex_instr_is_query(tex))
      return false;

   /* Buffer textures don't have levels-of-detail */
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return false;

   if (tex->backend_flags & AGX_TEXTURE_FLAG_NO_CLAMP)
      return false;

   bool int_coords = tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms ||
                     tex->op == nir_texop_tg4;

   b->cursor = nir_before_instr(&tex->instr);
   nir_def *min_lod = query_min_lod(b, tex, int_coords);
   nir_def *other_min_lod = nir_steal_tex_src(tex, nir_tex_src_min_lod);

   if (tex->op == nir_texop_tg4) {
      b->cursor = nir_after_instr(&tex->instr);

      /* The Vulkan spec section "Texel Gathering" says:
       *
       *    If levelbase < minLodIntegerimageView, then any values fetched are
       *    zero if the robustImageAccess2 feature is enabled.
       *
       * We currently always enable robustImageAccess2, so implement that
       * semantic here.
       *
       * We could probably optimize this with a special descriptor for this case
       * but tg4 is rare enough I'm not bothered.
       */
      nir_def *old = &tex->def;
      nir_def *oob = nir_ine_imm(b, min_lod, 0);
      nir_def *zero = nir_imm_zero(b, old->num_components, old->bit_size);
      nir_def *new_ = nir_bcsel(b, oob, zero, old);
      nir_def_rewrite_uses_after(old, new_);
   } else if (tex->op == nir_texop_txl) {
      assert(other_min_lod == NULL && "txl doesn't have an API min lod");

      nir_def *lod = nir_steal_tex_src(tex, nir_tex_src_lod);
      if (lod) {
         min_lod = nir_fmax(b, nir_f2fN(b, lod, min_lod->bit_size), min_lod);
      }

      nir_tex_instr_add_src(tex, nir_tex_src_lod, min_lod);
   } else {
      if (other_min_lod) {
         assert(!int_coords && "no API min lod");
         min_lod = nir_fmax(b, min_lod, nir_f2f16(b, other_min_lod));
      }

      /* NIR min_lod sources are floating point, including texel fetches.
       * The descriptor supplies the already-truncated integer threshold for
       * integer-coordinate access; preserve that value when changing type. */
      if (int_coords)
         min_lod = nir_u2f16(b, min_lod);
      nir_tex_instr_add_src(tex, nir_tex_src_min_lod, min_lod);
   }

   return true;
}

static bool
agx_nir_lower_image_view_min_lod(nir_shader *nir)
{
   return nir_shader_tex_pass(nir, lower_min_lod, nir_metadata_none, NULL);
}

/*
 * In Vulkan, the VIEWPORT should read 0 in the fragment shader if it is not
 * written by the vertex shader, but in our implementation, the varying would
 * otherwise be undefined. This small pass predicates VIEWPORT reads based on
 * whether the hardware vertex shader writes the VIEWPORT (nonzero UVS index).
 */
static bool
lower_viewport_fs(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location != VARYING_SLOT_VIEWPORT)
      return false;

   b->cursor = nir_after_instr(&intr->instr);
   nir_def *orig = &intr->def;

   nir_def *uvs = nir_load_uvs_index_agx(b, .io_semantics = sem);
   nir_def *def = nir_bcsel(b, nir_ine_imm(b, uvs, 0), orig, nir_imm_int(b, 0));

   nir_def_rewrite_uses_after(orig, def);
   return true;
}

static bool
lower_subpass_dim(nir_builder *b, nir_tex_instr *tex, UNUSED void *_data)
{
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS)
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   else if (tex->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS)
      tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
   else
      return false;

   return true;
}

/* Native texel fetches are memory-safe but can alias another texel within
 * the allocation. Enforce the Vulkan view bounds before narrowing coordinates
 * or LODs for the hardware, and return zero for invalid fetches. */
static bool
hk_lower_native_robust_txf(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_txf && tex->op != nir_texop_txf_ms)
      return false;

   b->cursor = nir_before_instr(&tex->instr);
   const int coord_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   const int lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   const int sample_idx = nir_tex_instr_src_index(tex, nir_tex_src_ms_index);
   nir_def *coord = tex->src[coord_idx].src.ssa;
   const unsigned components = tex->coord_components;
   nir_def *size = nir_build_texture_query(b, tex, nir_texop_txs,
                                           components, nir_type_uint32,
                                           false, lod_idx >= 0);
   if (lod_idx >= 0) {
      nir_tex_instr *query = nir_instr_as_tex(nir_def_instr(size));
      const int query_lod = nir_tex_instr_src_index(query, nir_tex_src_lod);
      nir_src_rewrite(&query->src[query_lod].src, tex->src[lod_idx].src.ssa);
   }
   nir_def *valid = nir_ball(b, nir_ult(b, coord, size));
   if (lod_idx >= 0 && tex->sampler_dim != GLSL_SAMPLER_DIM_BUF) {
      nir_def *levels = nir_build_texture_query(b, tex, nir_texop_query_levels,
                                               1, nir_type_uint32, false, false);
      nir_def *lod = tex->src[lod_idx].src.ssa;
      valid = nir_iand(b, valid, nir_ult(b, lod, levels));
      nir_src_rewrite(&tex->src[lod_idx].src,
                      nir_bcsel(b, valid, lod, nir_imm_int(b, 0)));
   }
   if (sample_idx >= 0) {
      nir_def *samples = nir_build_texture_query(b, tex, nir_texop_texture_samples,
                                                1, nir_type_uint32, false, false);
      nir_def *sample = tex->src[sample_idx].src.ssa;
      valid = nir_iand(b, valid, nir_ult(b, sample, samples));
      nir_src_rewrite(&tex->src[sample_idx].src,
                      nir_bcsel(b, valid, sample, nir_imm_int(b, 0)));
   }
   nir_src_rewrite(&tex->src[coord_idx].src,
                   nir_bcsel(b, valid, coord, nir_imm_zero(b, components, 32)));
   /* robustImageAccess2: an invalid fetch returns (0,0,0,1) for a view whose
    * format has no alpha channel and (0,0,0,0) otherwise. The descriptor's
    * alpha swizzle is the constant one exactly for alpha-less formats. */
   nir_def *invalid_value = nir_imm_zero(b, tex->def.num_components,
                                         tex->def.bit_size);
   const int deref_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (tex->def.num_components == 4 && deref_idx >= 0) {
      nir_def *word0 = nir_load_texture_descriptor_word_agx(
         b, tex->src[deref_idx].src.ssa);
      nir_def *swizzle_a = nir_ubitfield_extract_imm(b, word0, 25, 3);
      nir_def *one = nir_alu_type_get_base_type(tex->dest_type) == nir_type_float
                        ? nir_imm_floatN_t(b, 1, tex->def.bit_size)
                        : nir_imm_intN_t(b, 1, tex->def.bit_size);
      nir_def *alpha = nir_bcsel(b, nir_ieq_imm(b, swizzle_a, AGX_CHANNEL_1),
                                 one, nir_imm_zero(b, 1, tex->def.bit_size));
      invalid_value = nir_vector_insert_imm(b, invalid_value, alpha, 3);
   }
   b->cursor = nir_after_instr(&tex->instr);
   nir_def *result = nir_bcsel(b, valid, &tex->def, invalid_value);
   nir_def_rewrite_uses_after(&tex->def, result);
   return true;
}

static bool
should_lower_robust(const nir_intrinsic_instr *intr, const void *data)
{
   /* The hardware is robust, but our software image atomics are not. Unlike the
    * GL driver, we don't use the common buffer image lowering, using the
    * agx_nir_lower_texture lowering for robustImageAccess2 semantics.
    */
   if (data && *(const bool *)data &&
       (intr->intrinsic == nir_intrinsic_image_deref_load ||
        intr->intrinsic == nir_intrinsic_image_deref_store))
      return true;

   return intr->intrinsic == nir_intrinsic_image_deref_atomic ||
          intr->intrinsic == nir_intrinsic_image_deref_atomic_swap;
}

/* robustImageAccess2 returns (0,0,0,1) out of bounds for a view whose format
 * has no alpha channel and (0,0,0,0) otherwise. A load without a format
 * qualifier reads that from the native texture descriptor: hk packs the
 * format's swizzle into it, so the alpha swizzle is the constant one exactly
 * for alpha-less formats, and the null descriptor swizzles alpha to zero. */
static nir_def *
hk_native_robust_oob_alpha(nir_builder *b, nir_intrinsic_instr *load,
                           const void *data)
{
   const nir_alu_type type = nir_intrinsic_dest_type(load);
   const unsigned bit_size = load->def.bit_size;
   nir_def *zero = nir_imm_zero(b, 1, bit_size);
   nir_def *one = nir_alu_type_get_base_type(type) == nir_type_float
                     ? nir_imm_floatN_t(b, 1, bit_size)
                     : nir_imm_intN_t(b, 1, bit_size);

   /* The descriptor read is resolved by hk_nir_lower_descriptors, which runs
    * after this pass; the wrap itself must precede descriptor lowering so
    * the native read path's own coordinate clamp applies inside the branch. */
   if (!(data && *(const bool *)data) ||
       load->intrinsic != nir_intrinsic_image_deref_load)
      return one;

   nir_def *word0 = nir_load_image_descriptor_word_agx(b, load->src[0].ssa);
   nir_def *swizzle_a = nir_ubitfield_extract_imm(b, word0, 25, 3);
   return nir_bcsel(b, nir_ieq_imm(b, swizzle_a, AGX_CHANNEL_1), one, zero);
}

/* Keep native gradient offsets: adding offset / base-level size to the
 * normalized coordinate is wrong when derivatives select another mip level.
 * Both immediate and register array offsets have native staging support. */
static bool
hk_lower_native_texture_offset(const nir_instr *instr, UNUSED const void *data)
{
   const nir_tex_instr *tex = nir_instr_as_tex(instr);
   const int offset = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset < 0)
      return false;
   return tex->op == nir_texop_txf ||
          (tex->op == nir_texop_tg4 &&
           (tex->is_shadow || !nir_src_is_const(tex->src[offset].src)));
}

/* Use the existing coefficient interpolation at the selected sample's
 * programmable location. Locations come from this draw's root descriptor. */
static bool
hk_lower_native_sample_barycentric(nir_builder *b, nir_intrinsic_instr *intr,
                                  UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_barycentric_at_sample)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *packed = nir_load_sample_positions_agx(b);
   nir_def *sample = nir_u2u32(b, intr->src[0].ssa);
   nir_def *position = nir_ushr(b, packed, nir_imul_imm(b, sample, 8));
   nir_def *offset[2];
   for (unsigned c = 0; c < 2; ++c) {
      nir_def *nibble = nir_iand_imm(b, nir_ushr_imm(b, position, c * 4), 15);
      offset[c] = nir_fadd_imm(b, nir_fmul_imm(b, nir_u2f32(b, nibble),
                                             1.0 / 16.0), -0.5);
   }
   nir_def_replace(&intr->def, nir_load_barycentric_at_offset(
      b, intr->def.bit_size, nir_vec2(b, offset[0], offset[1]),
      .interp_mode = nir_intrinsic_interp_mode(intr)));
   return true;
}

static void
hk_lower_nir(struct hk_device *dev, nir_shader *nir,
             const struct vk_pipeline_robustness_state *rs, bool is_multiview,
             uint32_t set_layout_count,
             struct vk_descriptor_set_layout *const *set_layouts,
             enum hk_feature_key features)
{
   if (HK_PERF(dev, NOROBUST)) {
      rs = &vk_robustness_disabled;
   }

   const nir_opt_access_options access_options = {
      .is_vulkan = true,
   };
   NIR_PASS(_, nir, nir_opt_access, &access_options);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_lower_input_attachments,
               &(nir_input_attachment_options){
                  .use_view_id_for_layer = is_multiview,
               });

      NIR_PASS(_, nir, nir_shader_tex_pass, lower_subpass_dim, nir_metadata_all,
               NULL);
      NIR_PASS(_, nir, nir_lower_wpos_center);
   }

   /* XXX: should be last geometry stage, how do I get to that? */
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      if (is_multiview)
         hk_lower_multiview(nir);
   }

   if (nir->info.stage == MESA_SHADER_TESS_EVAL) {
      NIR_PASS(_, nir, nir_lower_patch_vertices,
               nir->info.tess.tcs_vertices_out, NULL);
   }

   const struct lower_ycbcr_state ycbcr_state = {
      .set_layout_count = set_layout_count,
      .set_layouts = set_layouts,
   };
   NIR_PASS(_, nir, nir_shader_tex_pass, lower_packed_ycbcr_size,
            nir_metadata_control_flow, (void *)&ycbcr_state);
   NIR_PASS(_, nir, nir_shader_tex_pass, lower_ycbcr_explicit,
            nir_metadata_none, (void *)&ycbcr_state);
   NIR_PASS(_, nir, nir_shader_tex_pass, lower_packed_ycbcr_center,
            nir_metadata_none, (void *)&ycbcr_state);
   NIR_PASS(_, nir, nir_shader_tex_pass, lower_packed_ycbcr_lod,
            nir_metadata_control_flow, (void *)&ycbcr_state);
   NIR_PASS(_, nir, nir_vk_lower_ycbcr_tex, lookup_ycbcr_conversion,
            &ycbcr_state);
   NIR_PASS(_, nir, nir_shader_tex_pass, lower_ycbcr_chroma_blend,
            nir_metadata_none, (void *)&ycbcr_state);
   NIR_PASS(_, nir, nir_shader_tex_pass, lower_ycbcr_plane_scale,
            nir_metadata_control_flow, (void *)&ycbcr_state);

   /* Lower push constants before lower_descriptors */
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const,
            nir_address_format_32bit_offset);

   /* Turn cache flushes into image coherency bits while we still have derefs */
   NIR_PASS(_, nir, nir_lower_memory_model);

   const bool native_robust_images = agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
      rs->images != VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED_EXT;
   NIR_PASS(_, nir, nir_lower_robust_access_cb, should_lower_robust,
            hk_native_robust_oob_alpha, &native_robust_images);
   if (native_robust_images) {
      const nir_lower_tex_options offsets = {.lower_txf_offset = true};
      NIR_PASS(_, nir, nir_lower_tex, &offsets);
      NIR_PASS(_, nir, nir_shader_tex_pass, hk_lower_native_robust_txf,
               nir_metadata_control_flow, NULL);
   }

   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
       debug_get_bool_option("HK_M4_VERSION_SAMPLES", true)) {
      nir_variable *sampler = hk_nir_sample_version_candidate(nir);
      if (sampler && sampler->data.descriptor_set < set_layout_count &&
          set_layouts[sampler->data.descriptor_set]) {
         const struct hk_descriptor_set_layout *layout = vk_to_hk_descriptor_set_layout(
            set_layouts[sampler->data.descriptor_set]);
         if (sampler->data.binding < layout->binding_count) {
            const struct hk_descriptor_set_binding_layout *binding =
               &layout->binding[sampler->data.binding];
            if (binding->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
                binding->array_size == 1 && !binding->flags) {
               NIR_PASS(_, nir, hk_nir_version_samples, sampler,
                        features & HK_FEAT_IMAGE_VIEW_MIN_LOD);
            }
         }
      }
   }
   hk_capture_lowering(nir, "before-minmax", features);
   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
       (features & (HK_FEAT_SAMPLER_MINMAX | HK_FEAT_IMAGE_VIEW_MIN_LOD)) &&
       !HK_PERF(dev, NOMINMAX)) {
      /* Keep cube gradients until reduction has selected its anisotropic
       * footprint. The usual early pass turns them into scalar LODs. */
      const nir_lower_tex_options prepare = {
         .lower_1d = true,
         .lower_txp = ~0u,
         .lower_invalid_implicit_lod = true,
      };
      NIR_PASS(_, nir, nir_lower_tex, &prepare);
      NIR_PASS(_, nir, hk_nir_lower_minmax, features & HK_FEAT_IMAGE_VIEW_MIN_LOD);
   }

   hk_capture_lowering(nir, "after-minmax", features);

   /* We must do early lowering before hk_nir_lower_descriptors, since this will
    * create lod_bias instructions.
    */
   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip)) {
      /* Preprocessing scalarizes constants. Recover constant vectors before
       * deciding whether their texel offsets need coordinate lowering. */
      NIR_PASS(_, nir, nir_opt_constant_folding);
      const nir_lower_tex_options options = {
         .lower_txf_offset = true,
         .lower_offset_filter = hk_lower_native_texture_offset,
      };
      NIR_PASS(_, nir, nir_lower_tex, &options);
   }
   NIR_PASS(_, nir, agx_nir_lower_texture_early_with_2d_robustness,
            true /* support_lod_bias */, agx_chip_uses_hal200_ioto(dev->dev.chip),
            agx_chip_sampler_generation(dev->dev.chip) == AGX_SAMPLER_GENERATION_GEN4);
   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip)) {
      /* Early lowering expands textureGatherOffsets into individual gathers.
       * Lower offsets on those new shadow gathers after projection, before
       * descriptor lowering consumes the texture-size queries we create. */
      const nir_lower_tex_options offsets = {
         .lower_offset_filter = hk_lower_native_texture_offset,
      };
      NIR_PASS(_, nir, nir_lower_tex, &offsets);
   }

   /* Only enabled devices need the dynamic descriptor minima. Include this
    * in the feature key so pipeline caches cannot mix the two variants. */
   if (features & HK_FEAT_IMAGE_VIEW_MIN_LOD) {
      NIR_PASS(_, nir, agx_nir_lower_image_view_min_lod);
   }

   if (((features & HK_FEAT_CUSTOM_BORDER) || agx_native_graphics(&dev->dev)) &&
       !HK_PERF(dev, NOBORDER)) {
      NIR_PASS(_, nir, agx_nir_lower_custom_border,
               dev->dev.chip == AGX_CHIP_G16G, features & HK_FEAT_IMAGE_VIEW_MIN_LOD,
               dev->dev.chip == AGX_CHIP_G15S);
   }

   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip))
      NIR_PASS(_, nir, nir_shader_intrinsics_pass,
               hk_lower_native_sample_barycentric, nir_metadata_control_flow, NULL);

   NIR_PASS(_, nir, hk_nir_lower_descriptors, dev->dev.chip,
            features & HK_FEAT_IMAGE_VIEW_MIN_LOD,
            features & HK_FEAT_DIRECT_BUFFER_DESCRIPTORS,
            rs, set_layout_count, set_layouts);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_global,
            nir_address_format_64bit_global);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ssbo,
            hk_buffer_addr_format(rs->storage_buffers,
                                  rs->null_storage_buffer_descriptor));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo,
            hk_buffer_addr_format(rs->uniform_buffers,
                                  rs->null_uniform_buffer_descriptor));

   /* Before inserting bounds checks, we want to do a fair bit of optimization.
    * lower_load_global_constant_offset_instr has special optimizations for
    * constant offsets, so we want as many offsets to be constant as possible.
    */
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
   } while (progress);

   nir_move_options opts = nir_move_load_ssbo | nir_move_load_ubo;
   NIR_PASS(progress, nir, nir_opt_sink, opts);
   NIR_PASS(progress, nir, nir_opt_move, opts);

   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
       nir->info.stage == MESA_SHADER_COMPUTE) {
      NIR_PASS(_, nir, nir_opt_cse);
      hk_capture_lowering(nir, "before-coalesce-bounded", features);
      NIR_PASS(_, nir, hk_nir_coalesce_bounded);
   }

   bool soft_fault = agx_has_soft_fault(&dev->dev);
   NIR_PASS(_, nir, nir_shader_intrinsics_pass,
            lower_load_global_constant_offset_instr, nir_metadata_none,
            &soft_fault);

   assert(nir->info.shared_size == 0);

   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
            shared_var_info);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_shared,
            nir_address_format_32bit_offset);

   if (nir->info.zero_initialize_shared_memory && nir->info.shared_size > 0) {
      /* Align everything up to 16B so we can write whole vec4s. */
      nir->info.shared_size = align(nir->info.shared_size, 16);
      NIR_PASS(_, nir, nir_zero_initialize_shared_memory, nir->info.shared_size,
               16);

      /* We need to call lower_compute_system_values again because
       * nir_zero_initialize_shared_memory generates load_invocation_id which
       * has to be lowered to load_invocation_index.
       */
      NIR_PASS(_, nir, nir_lower_compute_system_values, NULL);
   }

   /* TODO: we can do indirect VS output */
   nir_variable_mode lower_indirect_modes = 0;
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      lower_indirect_modes |= nir_var_shader_out;
      /* Native interpolation instructions name coefficient slots directly.
       * Lower dynamic input indexing before it becomes legacy ITERPROJ. */
      if (agx_chip_uses_hal200_ioto(dev->dev.chip))
         lower_indirect_modes |= nir_var_shader_in;
   } else if (nir->info.stage == MESA_SHADER_VERTEX)
      lower_indirect_modes |= nir_var_shader_in | nir_var_shader_out;

   NIR_PASS(_, nir, nir_lower_indirect_derefs_to_if_else_trees,
            lower_indirect_modes, UINT32_MAX);

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            glsl_type_size,
            nir_lower_io_lower_64bit_to_32 |
               nir_lower_io_use_interpolated_input_intrinsics);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_viewport_fs,
               nir_metadata_control_flow, NULL);
   }

   if (agx_chip_descriptor_generation(dev->dev.chip) == 9) {
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_int64_storage,
               nir_metadata_control_flow, NULL);
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_scaled_storage,
               nir_metadata_none, NULL);
      NIR_PASS(_, nir, nir_shader_tex_pass, lower_scaled_sample,
               nir_metadata_none, NULL);
   }

   if (agx_chip_descriptor_generation(dev->dev.chip) == 9)
      NIR_PASS(_, nir, nir_shader_instructions_pass, lower_sparse_buffer_or_ms,
               nir_metadata_none,
               agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) ? dev : NULL);

   hk_capture_lowering(nir, "before-texture", features);
   NIR_PASS(_, nir, agx_nir_lower_texture_for_generation,
            agx_chip_descriptor_generation(dev->dev.chip));
   hk_capture_lowering(nir, "after-texture", features);
   NIR_PASS(_, nir, agx_nir_lower_multisampled_image_store);

   const bool defer_ubo_aliases = agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
      nir->info.stage == MESA_SHADER_FRAGMENT &&
      debug_get_bool_option("HK_M4_PREFETCH_UBOS", false);
   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) && !defer_ubo_aliases)
      NIR_PASS(_, nir, hk_nir_lower_memory_aliases);

   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) && !defer_ubo_aliases &&
       (features & HK_FEAT_STRICT_SPARSE))
      NIR_PASS(_, nir, hk_nir_lower_sparse_access);

   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip))
      NIR_PASS(_, nir, hk_nir_specialize_memory_window,
               dev->dev.main_heap_base, dev->dev.main_heap_size);

   if (agx_chip_uses_hal200_ioto(dev->dev.chip))
      agx_preprocess_nir_g17p(nir);
   else
      agx_preprocess_nir(nir);

   nir_opt_peephole_select_options peephole_select_options = {
      .limit = 0,
      .discard_ok = true,
   };
   NIR_PASS(_, nir, nir_opt_peephole_select, &peephole_select_options);
   NIR_PASS(_, nir, nir_opt_if,
            nir_opt_if_optimize_phi_true_false | nir_opt_if_avoid_64bit_phis);
}

static void
hk_upload_shader(struct hk_device *dev, struct hk_shader *shader)
{
   if (shader->b.info.has_preamble || shader->b.info.rodata.size_16) {
      /* TODO: Do we wnat to compact? Revisit when we rework prolog/epilogs. */
      size_t size = shader->b.info.binary_size;
      assert(size > 0);

      shader->bo = agx_bo_create(&dev->dev, size, 0,
                                 AGX_BO_EXEC | AGX_BO_LOW_VA, "Preamble");
      if (!shader->bo) {
         fprintf(stderr,
                 "hk_upload_shader: executable preamble allocation failed "
                 "(%zu bytes, stage %u)\n",
                 size, (unsigned)shader->info.stage);
         return;
      }

      void *map = agx_bo_map(shader->bo);
      if (!map) {
         fprintf(stderr,
                 "hk_upload_shader: executable preamble mapping failed "
                 "(%zu bytes, stage %u)\n",
                 size, (unsigned)shader->info.stage);
         agx_bo_unreference(&dev->dev, shader->bo);
         shader->bo = NULL;
         return;
      }

      memcpy(map, shader->b.binary, size);
      if (agx_bo_sync_for_device(shader->bo, 0, size)) {
         agx_bo_unreference(&dev->dev, shader->bo);
         shader->bo = NULL;
         return;
      }
      shader->preamble_addr =
         shader->bo->va->addr + shader->b.info.preamble_offset;
   }

   if (!shader->linked.ht) {
      /* If we only have a single variant, link now. */
      shader->only_linked = hk_fast_link(dev, false, shader, NULL, NULL, 0);
   }

   if (shader->info.stage == MESA_SHADER_FRAGMENT) {
      agx_pack_fragment_face_2(&shader->frag_face, 0, &shader->b.info);
   }

   agx_pack(&shader->counts, COUNTS, cfg) {
      cfg.uniform_register_count = shader->b.info.push_count;
      cfg.preshader_register_count = shader->b.info.nr_preamble_gprs;
      cfg.sampler_state_register_count = agx_translate_sampler_state_count(
         shader->b.info.sampler_state_count, false);
      cfg.texture_state_register_count = shader->b.info.texture_state_count;
   }
}

DERIVE_HASH_TABLE(hk_fast_link_key_vs);
DERIVE_HASH_TABLE(hk_fast_link_key_fs);

static VkResult
hk_init_link_ht(struct hk_shader *shader, mesa_shader_stage sw_stage)
{
   simple_mtx_init(&shader->linked.lock, mtx_plain);

   bool multiple_variants =
      sw_stage == MESA_SHADER_VERTEX || sw_stage == MESA_SHADER_FRAGMENT;

   if (!multiple_variants)
      return VK_SUCCESS;

   if (sw_stage == MESA_SHADER_VERTEX)
      shader->linked.ht = hk_fast_link_key_vs_table_create(NULL);
   else
      shader->linked.ht = hk_fast_link_key_fs_table_create(NULL);

   return (shader->linked.ht == NULL) ? VK_ERROR_OUT_OF_HOST_MEMORY
                                      : VK_SUCCESS;
}

/* Remove only the explicit eligibility tests introduced by UBO prefetch.
 * Command recording chooses this executable only for ordinary immutable UBOs. */
static bool
hk_specialize_ubo_direct(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   const struct hk_shader_info *info = data;
   if (intr->intrinsic != nir_intrinsic_load_preamble ||
       intr->def.bit_size != 32 || intr->def.num_components != 1)
      return false;
   for (unsigned i = 0; i < info->ubo_prefetch_count; ++i) {
      if (nir_intrinsic_base(intr) == info->ubo_prefetch[i].direct_half) {
         b->cursor = nir_before_instr(&intr->instr);
         nir_def_replace(&intr->def, nir_imm_int(b, 1));
         return true;
      }
   }
   return false;
}

static bool
lower_uniforms(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   /* Root is first, descriptor sets follow immediately. */
   unsigned *root_ = data;
   unsigned root = *root_;
   unsigned sets = root + 4;

   if (intr->intrinsic == nir_intrinsic_bindless_image_agx ||
       intr->intrinsic == nir_intrinsic_bindless_sampler_agx) {
      /* Change of units from sets to uniforms */
      nir_intrinsic_set_desc_set(intr,
                                 sets + (nir_intrinsic_desc_set(intr) * 4));
      return true;
   }

   if (intr->intrinsic != nir_intrinsic_load_root_agx &&
       intr->intrinsic != nir_intrinsic_load_descriptor_set_agx)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *rep;

   if (intr->intrinsic == nir_intrinsic_load_descriptor_set_agx) {
      unsigned s = nir_intrinsic_desc_set(intr);
      rep = nir_load_preamble(b, 1, 64, .base = sets + (4 * s));
   } else {
      rep = nir_load_preamble(b, 1, 64, .base = root);
   }

   nir_def_replace(&intr->def, rep);
   return true;
}

/* The native G17P flat path takes the C coefficient directly. Its launch
 * admission currently permits only non-fan triangle primitives, so the
 * first-fan B/C selection is unreachable. Keep that topology restriction
 * paired with this lowering; do not import a root pointer and perform a
 * redundant per-pixel select for a topology this path never executes.
 */
static bool
hk_g17p_lower_flat_nonfan_triangles(nir_builder *b, nir_intrinsic_instr *intr,
                                    UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_is_first_fan_agx)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_imm_false(b));
   return true;
}

static bool
kill_psiz_write(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output ||
       nir_intrinsic_io_semantics(intr).location != VARYING_SLOT_PSIZ)
      return false;

   if (nir_remove_sysval_output(intr, MESA_SHADER_FRAGMENT)) {
      b->shader->info.outputs_written &= ~VARYING_BIT_PSIZ;
   }

   return true;
}

static bool
hk_nir_uses_root(const nir_shader *nir)
{
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_intrinsic &&
                nir_instr_as_intrinsic(instr)->intrinsic ==
                   nir_intrinsic_load_root_agx)
               return true;
         }
      }
   }

   return false;
}

/* Like the Gallium Asahi clip conversion, this runs only on the hardware
 * vertex/copy variant. Software VS/TES/GS outputs and XFB stay in API space.
 */
static bool
hk_lower_clip_negative_one_to_one(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_create(impl);
   nir_variable *temp = NULL;
   nir_intrinsic_instr *output = NULL;
   unsigned mask = 0;

   /* TES and generated copy shaders can write position in several pieces.
    * Collect the final value before converting Z, including conditional and
    * partial stores, rather than assuming one vec4 store.
    */
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_store_output ||
             nir_intrinsic_io_semantics(intr).location != VARYING_SLOT_POS)
            continue;

         if (!temp) {
            temp = nir_local_variable_create(impl, glsl_vec4_type(), "clip_position");
            output = nir_instr_as_intrinsic(nir_instr_clone(nir, instr));
         }
         b.cursor = nir_before_instr(instr);
         unsigned component = nir_intrinsic_component(intr);
         unsigned write_mask = nir_intrinsic_write_mask(intr) << component;
         nir_def *value = intr->src[0].ssa;
         unsigned swizzle[4] = {0};
         for (unsigned i = 0; i < value->num_components; ++i)
            swizzle[component + i] = i;
         nir_store_var(&b, temp, nir_swizzle(&b, value, swizzle, 4), write_mask);
         mask |= write_mask;
         nir_instr_remove(instr);
      }
   }
   if (!temp)
      return nir_no_progress(impl);

   b.cursor = nir_after_impl(impl);
   nir_def *pos = nir_load_var(&b, temp);
   nir_def *z = nir_channel(&b, pos, 2);
   nir_def *w = nir_channel(&b, pos, 3);
   nir_def *c = nir_load_clip_z_coeff_agx(&b);
   nir_def *mapped = nir_fmul_imm(&b, nir_fadd(&b, z, w), 0.5f);
   nir_def *new_z = nir_bcsel(&b, nir_fneu_imm(&b, c, 0.0f), mapped, z);
   output->num_components = 4;
   nir_intrinsic_set_component(output, 0);
   nir_intrinsic_set_write_mask(output, mask);
   output->src[0] = nir_src_for_ssa(nir_vector_insert_imm(&b, pos, new_z, 2));
   output->src[1] = nir_src_for_ssa(nir_imm_int(&b, 0));
   nir_builder_instr_insert(&b, &output->instr);
   nir_progress(true, impl, nir_metadata_control_flow);
   nir_lower_vars_to_ssa(nir);
   return true;
}

static void
hk_lower_hw_vs(nir_shader *nir, struct hk_shader *shader, bool kill_psiz,
               bool native, bool depth_clip_control)
{
   /* Point size must be clamped, excessively large points don't render
    * properly on G13.
    *
    * Must be synced with pointSizeRange.
    */
   NIR_PASS(_, nir, nir_lower_point_size, 1.0f, 511.95f);

   if (kill_psiz) {
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, kill_psiz_write,
               nir_metadata_control_flow, NULL);
   }

   if (depth_clip_control)
      NIR_PASS(_, nir, hk_lower_clip_negative_one_to_one);

   NIR_PASS(_, nir, nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);
   NIR_PASS(_, nir, agx_nir_lower_cull_distance_vs);

   /* Native fragment layer reads use a flat coefficient. Supply Vulkan's
    * default layer zero even when the last vertex stage does not write it. */
   if (native && !(nir->info.outputs_written & VARYING_BIT_LAYER)) {
      nir_builder b = nir_builder_at(nir_after_impl(nir_shader_get_entrypoint(nir)));
      nir_store_output(&b, nir_imm_int(&b, 0), nir_imm_int(&b, 0),
                       .src_type = nir_type_uint32,
                       .io_semantics = {.location = VARYING_SLOT_LAYER,
                                        .num_slots = 1},
                       .write_mask = 1);
      nir->info.outputs_written |= VARYING_BIT_LAYER;
   }
   NIR_PASS(_, nir, agx_nir_lower_uvs, &shader->info.uvs);
}

struct hk_ubo_prefetch_layouts {
   struct vk_descriptor_set_layout *const *sets;
   unsigned count;
};

static bool
hk_ubo_prefetch_binding_allowed(void *data, unsigned set, unsigned offset)
{
   const struct hk_ubo_prefetch_layouts *info = data;
   struct vk_descriptor_set_layout *const *layouts = info->sets;
   if (set == HK_UBO_PREFETCH_ROOT_SET) {
      const unsigned base = hk_root_descriptor_offset(dynamic_buffers);
      if (!layouts || offset < base || (offset - base) % sizeof(struct hk_buffer_address))
         return false;
      unsigned index = (offset - base) / sizeof(struct hk_buffer_address);
      if (index >= HK_MAX_DYNAMIC_BUFFERS)
         return false;
      for (unsigned s = 0; s < info->count; ++s) {
         if (!layouts[s])
            return false;
         const struct hk_descriptor_set_layout *layout =
            vk_to_hk_descriptor_set_layout(layouts[s]);
         if (index >= layout->vk.dynamic_descriptor_count) {
            index -= layout->vk.dynamic_descriptor_count;
            continue;
         }
         for (unsigned b = 0; b < layout->binding_count; ++b) {
            const struct hk_descriptor_set_binding_layout *binding = &layout->binding[b];
            if (binding->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
                !binding->flags && index >= binding->dynamic_buffer_index &&
                index - binding->dynamic_buffer_index < binding->array_size)
               return true;
         }
         return false;
      }
      return false;
   }

   if (!layouts || set >= info->count || !layouts[set])
      return false;
   const struct hk_descriptor_set_layout *layout =
      vk_to_hk_descriptor_set_layout(layouts[set]);
   if (layout->vk.flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT)
      return false;
   for (unsigned i = 0; i < layout->binding_count; ++i) {
      const struct hk_descriptor_set_binding_layout *b = &layout->binding[i];
      if (b->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && b->stride &&
          !(b->flags & (VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                        VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT)) &&
          offset >= b->offset && (offset - b->offset) % b->stride == 0 &&
          (offset - b->offset) / b->stride < b->array_size)
         return true;
   }
   return false;
}

static VkResult
hk_compile_nir(struct hk_device *dev, const VkAllocationCallbacks *pAllocator,
               nir_shader *nir, VkShaderCreateFlagsEXT shader_flags,
               const struct vk_pipeline_robustness_state *rs,
               const union hk_key *key, enum hk_feature_key features,
               struct hk_shader *shader, mesa_shader_stage sw_stage, bool hw,
               nir_xfb_info *xfb_info, unsigned set_count,
               struct vk_descriptor_set_layout *const *set_layouts)
{
   /* Capture XFB metadata before resource lowering replaces and frees the
    * input NIR.  For the last VS/TES variant xfb_info belongs to that input,
    * so retaining the pointer until after compilation is a use-after-free.
    */
   if (xfb_info) {
      assert(xfb_info->output_count < ARRAY_SIZE(shader->info.xfb_outputs));

      memcpy(&shader->info.xfb_info, xfb_info,
             nir_xfb_info_size(xfb_info->output_count));

      typed_memcpy(shader->info.xfb_stride, nir->info.xfb_stride, 4);
   }

   /* Geometry's count variant removes application side effects. Expand
    * atomic64 only after Poly split, otherwise it drops the lock release
    * while retaining the acquisition that controls the retry loop. */
   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
       hk_nir_lower_atomic64(nir))
      agx_preprocess_nir_g17p(nir);

   unsigned nr_vbos = 0;
   bool kill_psiz = false;
   shader->info.writes_memory = nir->info.writes_memory;

   if (sw_stage == MESA_SHADER_TESS_EVAL ||
       sw_stage == MESA_SHADER_TESS_CTRL) {
      shader->info.tess.info.ccw = nir->info.tess.ccw;
      shader->info.tess.info.points = nir->info.tess.point_mode;
      shader->info.tess.info.spacing = nir->info.tess.spacing;
      shader->info.tess.info.mode = nir->info.tess._primitive_mode;

      if (sw_stage == MESA_SHADER_TESS_CTRL) {
         shader->info.tess.tcs_output_patch_size =
            nir->info.tess.tcs_vertices_out;
         shader->info.tess.tcs_per_vertex_outputs =
            poly_tcs_per_vertex_outputs(nir);
         shader->info.tess.tcs_nr_patch_outputs =
            util_last_bit(nir->info.patch_outputs_written);
         shader->info.tess.tcs_output_stride = poly_tcs_output_stride(nir);
         if (agx_chip_uses_hal200_ioto(dev->dev.chip)) {
            /* Poly has already lowered patch I/O to global memory. */
            nir->info.workgroup_size[0] = nir->info.tess.tcs_vertices_out;
            nir->info.workgroup_size[1] = nir->info.workgroup_size[2] = 1;
            nir->info.stage = MESA_SHADER_COMPUTE;
            memset(&nir->info.cs, 0, sizeof(nir->info.cs));
         }
      } else {
         /* This destroys info so it needs to happen after the gather */
         NIR_PASS(_, nir, poly_nir_lower_tes, hw);
         /* The software variant uses the same resource operations as the
          * hardware TES. Output lowering changes it to compute below. */
         if (agx_chip_uses_hal200_ioto(dev->dev.chip)) {
            nir->info.stage = MESA_SHADER_VERTEX;
            memset(&nir->info.vs, 0, sizeof(nir->info.vs));
            /* The software TES still uses tessellation's linear invocation
             * IDs for its GS input-buffer stores. Without this tag Poly treats
             * it as an ordinary VS and reads the hardware vertex/instance ABI
             * registers, which are not supplied to a compute dispatch. */
            nir->info.vs.tes_poly = true;
         }
      }
   }

   /* Geometry shaders execute as compute. Like vertex shaders, they can
    * sample explicit levels/gradients or gather from the base texture level. */
   if (agx_chip_uses_hal200_ioto(dev->dev.chip) &&
       (nir->info.stage == MESA_SHADER_VERTEX ||
        nir->info.stage == MESA_SHADER_GEOMETRY ||
        sw_stage == MESA_SHADER_TESS_CTRL)) {
      NIR_PASS(_, nir, agx_nir_lower_g17p_cube_samples,
               agx_chip_uses_g15_g16_shader_abi(dev->dev.chip));
      nir_shader *candidate = nir_shader_clone(NULL, nir);
      if (candidate && agx_nir_lower_g17p_indexed_samplers(candidate)) {
         const bool direct_storage =
            agx_nir_lower_g17p_bindless_storage(candidate);
         ralloc_free(nir);
         nir = candidate;
         shader->info.agx3_indexed_sampler_valid = true;
         shader->info.agx3_indexed_images_valid = direct_storage;
      } else {
         ralloc_free(candidate);
      }
   }

   if (agx_chip_uses_hal200_ioto(dev->dev.chip) &&
       nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, agx_nir_lower_g17p_cube_samples,
               agx_chip_uses_g15_g16_shader_abi(dev->dev.chip));
      nir_shader *candidate = nir_shader_clone(NULL, nir);
      struct agx_g17p_combined_sampler_binding binding;
      if (candidate &&
          !dev->vk.enabled_features.descriptorBindingSampledImageUpdateAfterBind &&
          agx_nir_lower_g17p_combined_sampler(candidate, &binding)) {
         ralloc_free(nir);
         nir = candidate;
         shader->info.agx3_combined_sampler_valid = true;
         shader->info.agx3_combined_sampler = binding;

         /* Texture and sampler table pointers occupy uniform halves 0..7.
          * Keep ordinary push-root accesses in the separate native fragment ABI;
          * the lowering removes descriptor accesses, not push constants.
          */
         set_count = 0;
      } else {
         ralloc_free(candidate);
         candidate = nir_shader_clone(NULL, nir);
         if (candidate && agx_nir_lower_g17p_indexed_samplers(candidate)) {
            const bool direct_storage =
               agx_nir_lower_g17p_bindless_storage(candidate);
            ralloc_free(nir);
            nir = candidate;
            shader->info.agx3_indexed_sampler_valid = true;
            shader->info.agx3_indexed_images_valid = direct_storage;
         } else {
            ralloc_free(candidate);
         }
      }
   } else if (agx_chip_uses_hal200_ioto(dev->dev.chip) &&
              nir->info.stage == MESA_SHADER_COMPUTE &&
              sw_stage == MESA_SHADER_COMPUTE) {
      NIR_PASS(_, nir, agx_nir_lower_g17p_cube_samples,
               agx_chip_uses_g15_g16_shader_abi(dev->dev.chip));
      nir_shader *candidate = nir_shader_clone(NULL, nir);
      struct agx_g17p_sampled_storage_binding sampled_storage;
      if (candidate &&
          !dev->vk.enabled_features.descriptorBindingSampledImageUpdateAfterBind &&
          agx_nir_lower_g17p_sampled_storage(candidate, &sampled_storage)) {
         ralloc_free(nir);
         nir = candidate;
         shader->info.agx3_sampled_storage_valid = true;
         shader->info.agx3_sampled_storage = sampled_storage;
      } else {
         ralloc_free(candidate);
         candidate = nir_shader_clone(NULL, nir);
         struct agx_g17p_storage_image_binding storage;
         if (candidate &&
             agx_nir_lower_g17p_storage_image(candidate, &storage)) {
            ralloc_free(nir);
            nir = candidate;
            shader->info.agx3_storage_image_valid = true;
            shader->info.agx3_storage_image = storage;
         } else {
            ralloc_free(candidate);
            candidate = nir_shader_clone(NULL, nir);
            struct agx_g17p_combined_sampler_binding sampled_binding;
            if (candidate &&
                !dev->vk.enabled_features.descriptorBindingSampledImageUpdateAfterBind &&
                agx_nir_lower_g17p_combined_sampler(candidate,
                                                    &sampled_binding)) {
               ralloc_free(nir);
               nir = candidate;
               shader->info.agx3_combined_sampler_valid = true;
               shader->info.agx3_combined_sampler = sampled_binding;
               shader->info.agx3_txf_texture_valid = true;
               shader->info.agx3_txf_texture.texture_offset_B =
                  sampled_binding.texture_offset_B;
            } else {
               ralloc_free(candidate);
               candidate = nir_shader_clone(NULL, nir);
               if (candidate &&
                   agx_nir_lower_g17p_indexed_samplers(candidate)) {
                  struct agx_g17p_indexed_storage_binding indexed_storage;
                  const bool direct_storage =
                     agx_nir_lower_g17p_bindless_storage(candidate);
                  const bool has_indexed_storage =
                     !direct_storage && agx_nir_lower_g17p_indexed_storage(candidate,
                                                        &indexed_storage);
                  ralloc_free(nir);
                  nir = candidate;
                  shader->info.agx3_indexed_sampler_valid = true;
                  shader->info.agx3_indexed_images_valid = direct_storage;
                  shader->info.agx3_indexed_storage_valid =
                     has_indexed_storage;
                  if (has_indexed_storage)
                     shader->info.agx3_indexed_storage = indexed_storage;
               } else {
                  ralloc_free(candidate);
                  candidate = nir_shader_clone(NULL, nir);
                  struct agx_g17p_txf_binding binding;
                  if (candidate &&
                      agx_nir_lower_g17p_txf(candidate, &binding)) {
                     ralloc_free(nir);
                     nir = candidate;
                     shader->info.agx3_txf_texture_valid = true;
                     shader->info.agx3_txf_texture = binding;
                  } else {
                     ralloc_free(candidate);
                  }
               }
            }
         }
      }
   }

   /* For now, only shader objects are supported */
   if (sw_stage == MESA_SHADER_VERTEX) {
      nr_vbos = DIV_ROUND_UP(
         BITSET_LAST_BIT(shader->info.vs.attrib_components_read), 4);
      kill_psiz = key->vs.kill_psiz;
   } else if (sw_stage == MESA_SHADER_FRAGMENT) {
      shader->info.fs.writes_memory = nir->info.writes_memory;
      shader->info.fs.post_depth_coverage = nir->info.fs.post_depth_coverage;
      shader->info.fs.interp.linear = nir->info.linear_varyings;
      shader->info.fs.interp.flat =
         ~(nir->info.linear_varyings | nir->info.perspective_varyings);

      /* Discards must be lowering before lowering MSAA to handle discards */
      NIR_PASS(_, nir, agx_nir_lower_discard_zs_emit);

      /* A fragment shader that writes gl_SampleMask runs its own coverage
       * update with depth/stencil tests, so the epilog cannot apply
       * alpha-to-coverage afterwards (its later test-mode update wrote
       * neither depth nor stencil on M4). Apply it here instead, gated on
       * the draw's enable, before that coverage update: the demoted samples
       * then skip the tests and the colour store, and occlusion queries
       * count only the surviving samples
       * (query_pool.discard.normal.*.alpha_to_coverage). */
      if (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK)) {
         nir_builder b = nir_builder_at(
            nir_before_impl(nir_shader_get_entrypoint(nir)));
         nir_def *enable =
            nir_ine_imm(&b, nir_load_alpha_to_coverage_agx(&b), 0);
         NIR_PASS(_, nir, nir_lower_alpha_to_coverage, false, enable);
      }

      if (agx_chip_uses_hal200_ioto(dev->dev.chip)) {
         if (debug_get_bool_option("HK_NATIVE_COALESCE_BROADCAST", false))
            NIR_PASS(_, nir, agx_nir_coalesce_fragcolor);
         NIR_PASS(_, nir, agx_nir_lower_fs_output_to_epilog_g17p,
                  &shader->info.fs.epilog_key);
      } else {
         NIR_PASS(_, nir, agx_nir_lower_fs_output_to_epilog,
                  &shader->info.fs.epilog_key);
      }
      if (agx_chip_uses_hal200_ioto(dev->dev.chip))
         NIR_PASS(_, nir, agx_nir_lower_sample_mask_deferred);
      else
         NIR_PASS(_, nir, agx_nir_lower_sample_mask);

      if (nir->info.fs.uses_sample_shading) {
         /* Ensure the sample mask is preserved in register */
         nir_builder b =
            nir_builder_at(nir_after_impl(nir_shader_get_entrypoint(nir)));

         nir_def *mask =
            nir_load_exported_agx(&b, 1, 16, .base = AGX_ABI_FIN_SAMPLE_MASK);

         nir_export_agx(&b, mask, .base = AGX_ABI_FOUT_SAMPLE_MASK);

         NIR_PASS(_, nir, agx_nir_lower_to_per_sample);
      }

      NIR_PASS(_, nir, agx_nir_lower_fs_active_samples_to_register);
      if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip))
         NIR_PASS(_, nir, nir_shader_intrinsics_pass,
                  hk_lower_native_sample_barycentric,
                  nir_metadata_control_flow, NULL);
      if (agx_chip_uses_hal200_ioto(dev->dev.chip))
         NIR_PASS(_, nir, agx_nir_lower_interpolation_g17p);
      else
         NIR_PASS(_, nir, agx_nir_lower_interpolation);
      /* Flat inputs keep the first-fan B/C selection on every chip; the
       * descriptor lowering reads draw.provoking from the root table for
       * it, so triangle fans with the first provoking vertex
       * (rasterization.flatshading.triangle_fan, provoking_vertex.draw.*.
       * triangle_fan) interpolate the fan vertex the rasterizer stored. */
   }

   /* Normally, vertex shaders need to write a default point size. However, if
    * we have a geometry/tessellation shader, the hardware vertex (software
    * GS/TES) will handle this itself instead.
    */
   if ((sw_stage == MESA_SHADER_VERTEX || sw_stage == MESA_SHADER_TESS_EVAL) &&
       hw && !kill_psiz) {
      /* An evaluation shader is the hardware vertex shader too, and with
       * shader objects point mode may come from the control shader alone;
       * HLSL domain shaders write no point size
       * (shader_object.tessellation.hlsl.point_mode*). */
      NIR_PASS(_, nir, nir_lower_default_point_size);
   }

   uint64_t outputs = nir->info.outputs_written;
   if (sw_stage == MESA_SHADER_VERTEX || sw_stage == MESA_SHADER_TESS_EVAL) {
      if (hw) {
         hk_lower_hw_vs(nir, shader, kill_psiz, agx_chip_uses_g15_g16_shader_abi(dev->dev.chip),
                        features & HK_FEAT_DEPTH_CLIP_CONTROL);
         outputs = nir->info.outputs_written;
      } else {
         NIR_PASS(_, nir, poly_nir_lower_vs_before_gs);
         nir->info.stage = MESA_SHADER_COMPUTE;
         memset(&nir->info.cs, 0, sizeof(nir->info.cs));
         nir->xfb_info = NULL;
      }
   }

   unsigned root = (shader->info.agx3_storage_image_valid ||
                    shader->info.agx3_sampled_storage_valid ||
                    shader->info.agx3_indexed_storage_valid ||
                    shader->info.agx3_indexed_images_valid)
                      ? AGX_G17P_STORAGE_ROOT_HALFS
                   : (shader->info.agx3_txf_texture_valid ||
                      shader->info.agx3_indexed_sampler_valid)
                      ? AGX_G17P_TXF_ROOT_HALFS
                      : 0;
   if (sw_stage == MESA_SHADER_FRAGMENT)
      root = AGX_ABI_FUNI_ROOT +
             (agx_chip_uses_hal200_ioto(dev->dev.chip)
                 ? AGX_ABI_FUNI_NATIVE_PREFIX : 0);
   else if (sw_stage == MESA_SHADER_VERTEX)
      root = AGX_ABI_VUNI_COUNT_VK(nr_vbos);

   shader->info.set_count = set_count;

   /* XXX: rename */
   NIR_PASS(_, nir, hk_lower_uvs_index, sw_stage, nr_vbos);
   unsigned reserved = root + (4 * (1 + set_count));
   shader->info.agx3_root_uniform = root;
   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) && sw_stage == MESA_SHADER_FRAGMENT &&
       debug_get_bool_option("HK_M4_PREFETCH_UBOS", false)) {
      const struct hk_ubo_prefetch_layouts prefetch_layouts = {
         .sets = set_layouts, .count = set_count,
      };
      NIR_PASS(_, nir, hk_nir_prefetch_ubos, root + 4, set_count, &reserved,
               shader->info.ubo_prefetch, &shader->info.ubo_prefetch_count,
               hk_ubo_prefetch_binding_allowed, (void *)&prefetch_layouts);
      if (shader->info.ubo_prefetch_direct) {
         if (debug_get_bool_option("HK_M4_PLAIN_SAMPLERS", false)) {
            for (unsigned set = 0; set < set_count; ++set) {
               if (!set_layouts || !set_layouts[set])
                  continue;
               const struct hk_descriptor_set_layout *layout =
                  vk_to_hk_descriptor_set_layout(set_layouts[set]);
               if (layout->vk.flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT)
                  continue;
               for (unsigned binding = 0; binding < layout->binding_count; ++binding) {
                  const struct hk_descriptor_set_binding_layout *b = &layout->binding[binding];
                  if (b->type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                      b->array_size != 1 || b->flags ||
                      shader->info.plain_sampler_count == HK_PLAIN_SAMPLER_MAX)
                     continue;
                  shader->info.plain_samplers[shader->info.plain_sampler_count++] =
                     (struct hk_plain_sampler){ .set = set, .descriptor_offset_B = b->offset };
               }
            }
            NIR_PASS(_, nir, hk_nir_specialize_plain_samplers, root + 4, set_count,
                     shader->info.plain_samplers, &shader->info.plain_sampler_count);
         }
         NIR_PASS(_, nir, nir_shader_intrinsics_pass, hk_specialize_ubo_direct,
                  nir_metadata_control_flow, &shader->info);
         NIR_PASS(_, nir, nir_opt_constant_folding);
         NIR_PASS(_, nir, nir_opt_dead_cf);
         NIR_PASS(_, nir, nir_opt_dce);
      }
      NIR_PASS(_, nir, hk_nir_lower_memory_aliases);
      if (features & HK_FEAT_STRICT_SPARSE)
         NIR_PASS(_, nir, hk_nir_lower_sparse_access);
      NIR_PASS(_, nir, hk_nir_specialize_memory_window,
               dev->dev.main_heap_base, dev->dev.main_heap_size);
      agx_preprocess_nir_g17p(nir);
      if (debug_get_bool_option("HK_M4_PREFETCH_AUDIT", false))
         fprintf(stderr, "M4 UBO prefetch: loads=%u reserved=%u\n",
                 shader->info.ubo_prefetch_count, reserved);
   }
   /* Stage lowering can introduce driver sysvals (for example discard's
    * no_epilog_discard mask). hk_lower_uvs_index turns those into root
    * accesses, so gather the resource requirement after that pass and before
    * lower_uniforms removes the load_root_agx markers. */
   if (agx_chip_uses_hal200_ioto(dev->dev.chip))
      shader->info.agx3_uses_root = hk_nir_uses_root(nir);

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_uniforms,
            nir_metadata_control_flow, &root);

   if (agx_chip_uses_hal200_ioto(dev->dev.chip) &&
       (nir->info.stage == MESA_SHADER_VERTEX || sw_stage == MESA_SHADER_VERTEX)) {
      /* Software vertex execution keeps the vertex root and draw-parameter
       * ABI. Image access reserves halves 0..15 in both execution engines. */
      agx_nir_reserve_native_vertex_prefix(nir);
      root += AGX_ABI_VUNI_NATIVE_PREFIX;
      reserved += AGX_ABI_VUNI_NATIVE_PREFIX;
      shader->info.agx3_root_uniform = root;
   }

   if (agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
       debug_get_bool_option("HK_M4_PREFETCH_DESCRIPTORS", false) &&
       (sw_stage == MESA_SHADER_FRAGMENT || (sw_stage == MESA_SHADER_VERTEX && hw))) {
      if (debug_get_bool_option("HK_NATIVE_COMPACT_DESCRIPTOR_PREFETCH", false)) {
         NIR_PASS(_, nir, hk_nir_prefetch_descriptors_compact, root + 4, set_count,
                  &reserved, shader->info.descriptor_prefetch_compact,
                  &shader->info.descriptor_prefetch_compact_count);
      } else {
         NIR_PASS(_, nir, hk_nir_prefetch_descriptors, root + 4, set_count,
                  &reserved, shader->info.descriptor_prefetch);
      }
      if (debug_get_bool_option("HK_M4_PREFETCH_AUDIT", false)) {
         fprintf(stderr, "M4 descriptor prefetch: stage=%u reserved=%u", sw_stage, reserved);
         for (unsigned i = 0; i < shader->info.descriptor_prefetch_compact_count; ++i) {
            const struct hk_descriptor_prefetch *span = &shader->info.descriptor_prefetch_compact[i];
            fprintf(stderr, " compact-set%u+%u:%u->u%u", span->set,
                    span->offset_B, span->size_B, span->uniform_half);
         }
         for (unsigned set = 0; set < set_count; ++set) {
            const struct hk_descriptor_prefetch *span = &shader->info.descriptor_prefetch[set];
            if (span->size_B)
               fprintf(stderr, " set%u+%u:%u->u%u", set, span->offset_B,
                       span->size_B, span->uniform_half);
         }
         fprintf(stderr, "\n");
      }
   }

   /* Report every uniform half this shader actually reads.
    *
    * The disassembled G17P binary references uniform halves the declared
    * footprint (`reserved_preamble = root + 4 * (1 + set_count)`) does not
    * cover.  That is either the descriptor lowering emitting a set index the
    * layout does not have, or the backend assigning uniforms above the
    * reservation -- and the two need different fixes.  `lower_uniforms` has
    * just rewritten every root/descriptor-set read into a load_preamble, so
    * the bases here are exactly what the compiler believes it needs, from the
    * compiler's own side rather than from a disassembly of its output.
    */
   if (hk_g17p_launch_trace_enabled()) {
      fprintf(stderr,
              "G17P uniforms: stage=%u set_count=%u root=%u reserved=%u ",
              (unsigned)sw_stage, set_count, root,
              reserved);
      for (unsigned set = 0; set < set_count; ++set) {
         const struct hk_descriptor_prefetch *span = &shader->info.descriptor_prefetch[set];
         if (span->size_B)
            fprintf(stderr, "prefetch[set%u+%u:%u->u%u] ", set,
                    span->offset_B, span->size_B, span->uniform_half);
      }
      nir_foreach_function_impl(impl, nir) {
         nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;
               nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
               if (in->intrinsic == nir_intrinsic_load_preamble)
                  fprintf(stderr, "half%u ", nir_intrinsic_base(in));
            }
         }
      }
      fprintf(stderr, "\n");
   }

#if 0
   /* TODO */
   nir_variable_mode robust2_modes = 0;
   if (rs->uniform_buffers == VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT)
      robust2_modes |= nir_var_mem_ubo;
   if (rs->storage_buffers == VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT)
      robust2_modes |= nir_var_mem_ssbo;
#endif

   const bool native_preamble = agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
      nir->info.stage == MESA_SHADER_FRAGMENT &&
      debug_get_bool_option("HK_M4_NATIVE_PREAMBLE", false);

   /* Ioto-populated inputs and native preamble outputs must not share the
    * final 128-bit uniform block. On G15S and G16G, placing outputs at half
    * 44 after an input pointer at halves 40..43 intermittently zeroes the
    * first two outputs. Starting at half 48 fixes the sampler/UBO readback
    * probes on both chips. Keep this within the experimental preamble path;
    * its broader memory-load correctness is still being qualified. */
   struct agx_shader_key backend_key = {
      .reserved_preamble = native_preamble ? ALIGN_POT(reserved, 8) : reserved,
      .dev = agx_gather_device_key(&dev->dev),
      .no_stop = nir->info.stage == MESA_SHADER_FRAGMENT,
      /* Native Ioto declares TPS for internal kernels too. */
      .has_scratch = !nir->info.internal || agx_native_graphics(&dev->dev),
      .promote_constants = true,
      .promote_textures = !native_preamble,
      /* G17P's Ioto resource program populates CProg before the main shader;
       * it does not execute AGX2's tex_state_store/sampler_state_store shader
       * preamble.  The bounded G17P descriptor lowerings above convert the
       * admitted static texture cases to immediate state slots before the
       * backend runs. M4 can opt into integer-only arithmetic appended to
       * the Ioto constant program; descriptor stores remain unsupported and
       * unhandled descriptor shapes must fail closed in the main packer.
       */
      .no_preamble = agx_chip_uses_hal200_ioto(dev->dev.chip) && !native_preamble,
   };

   /* For now, sample shading is always dynamic. Indicate that. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT &&
       nir->info.fs.uses_sample_shading)
      backend_key.fs.inside_sample_loop = true;

   simple_mtx_t *lock = NULL;
   if (agx_get_compiler_debug())
      lock = &hk_device_physical(dev)->debug_compile_lock;

   if (lock)
      simple_mtx_lock(lock);

   assert(nir->info.io_lowered);
   const bool compiled =
      agx_compile_shader_nir(nir, &backend_key, &shader->b);

   if (lock)
      simple_mtx_unlock(lock);

   if (!compiled) {
      mesa_loge("AGX backend rejected %s shader for %s (name=%s)",
                _mesa_shader_stage_to_string(nir->info.stage),
                agx_chip_name(dev->dev.chip), nir->info.name ? nir->info.name : "unnamed");
      /* Ownership is consumed on failure as well as success. */
      ralloc_free(nir);
      return vk_errorf(dev, VK_ERROR_FEATURE_NOT_PRESENT,
                       "AGX backend cannot encode shader for %s",
                       agx_chip_name(dev->dev.chip));
   }

   shader->code_ptr = shader->b.binary;
   shader->code_size = shader->b.info.binary_size;

   shader->info.stage = sw_stage;
   shader->info.clip_distance_array_size = nir->info.clip_distance_array_size;
   shader->info.cull_distance_array_size = nir->info.cull_distance_array_size;
   shader->b.info.outputs = outputs;

   /* Geometry lowering also produces a hardware vertex shader. Its native
    * VDM metadata follows the compiled stage, not the originating API stage. */
   if (agx_chip_uses_hal200_ioto(dev->dev.chip) &&
       shader->b.info.stage == MESA_SHADER_VERTEX && hw) {
      const struct hk_agx3_g17p_vertex_compile_info compiler_info = {
         .hardware_vertex = shader->b.info.stage == MESA_SHADER_VERTEX,
         /* Rasterizer-discard TES may export no values. UVS still reserves
          * the four position words; its layout is the position-only layout
          * even when those words are undefined and never rasterized. */
         .position_only = (shader->b.info.outputs & ~VARYING_BIT_POS) == 0,
         .has_preamble = shader->b.info.has_preamble,
         .nr_preamble_gprs = shader->b.info.nr_preamble_gprs,
         .preamble_scratch_size = shader->b.info.preamble_scratch_size,
         .uvs_written = shader->info.uvs.written,
         .position_off = shader->info.uvs.group_offs[UVS_POSITION],
         .varyings_off = shader->info.uvs.group_offs[UVS_VARYINGS],
         .psiz_off = shader->info.uvs.group_offs[UVS_PSIZ],
         .layer_viewport_off = shader->info.uvs.group_offs[UVS_LAYER_VIEWPORT],
         .clip_off = shader->info.uvs.group_offs[UVS_CLIP_DIST],
         .uvs_size = shader->info.uvs.size,
         .user_size = shader->info.uvs.user_size,
      };
      hk_agx3_g17p_vertex_program_info(dev->dev.chip, &compiler_info,
                                       &shader->agx3_vdm_state_update);
   }

   if (nir->constant_data_size > 0) {
      uint32_t data_size = align(nir->constant_data_size, HK_MIN_UBO_ALIGNMENT);

      void *data = malloc(data_size);
      if (data == NULL) {
         ralloc_free(nir);
         return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      memcpy(data, nir->constant_data, nir->constant_data_size);

      assert(nir->constant_data_size <= data_size);
      memset(data + nir->constant_data_size, 0,
             data_size - nir->constant_data_size);

      shader->data_ptr = data;
      shader->data_size = data_size;
   }

   ralloc_free(nir);

   VkResult result = hk_init_link_ht(shader, sw_stage);
   if (result != VK_SUCCESS)
      return vk_error(dev, result);

   hk_upload_shader(dev, shader);
   return VK_SUCCESS;
}

static const struct vk_shader_ops hk_shader_ops;

static void
hk_destroy_linked_shader(struct hk_device *dev, struct hk_linked_shader *linked)
{
   agx_bo_unreference(&dev->dev, linked->b.bo);
   ralloc_free(linked);
}

static void
hk_shader_destroy(struct hk_device *dev, struct hk_shader *s)
{
   free((void *)s->code_ptr);
   free((void *)s->data_ptr);
   agx_bo_unreference(&dev->dev, s->bo);

   simple_mtx_destroy(&s->linked.lock);

   if (s->only_linked)
      hk_destroy_linked_shader(dev, s->only_linked);

   if (s->linked.ht) {
      hash_table_foreach(s->linked.ht, entry) {
         hk_destroy_linked_shader(dev, entry->data);
      }
      _mesa_hash_table_destroy(s->linked.ht, NULL);
   }
}

void
hk_api_shader_destroy(struct vk_device *vk_dev, struct vk_shader *vk_shader,
                      const VkAllocationCallbacks *pAllocator)
{
   struct hk_device *dev = container_of(vk_dev, struct hk_device, vk);
   struct hk_api_shader *obj =
      container_of(vk_shader, struct hk_api_shader, vk);

   hk_foreach_variant(obj, shader) {
      hk_shader_destroy(dev, shader);
   }

   vk_shader_free(&dev->vk, pAllocator, &obj->vk);
}

VkResult
hk_compile_shader(struct hk_device *dev, struct vk_shader_compile_info *info,
                  const struct vk_graphics_pipeline_state *state,
                  const struct vk_features *vk_features,
                  const VkAllocationCallbacks *pAllocator,
                  struct hk_api_shader **shader_out)
{
   VkResult result;
   enum hk_feature_key features = hk_make_feature_key(dev->vk.physical, vk_features);

   /* We consume the NIR, regardless of success or failure */
   nir_shader *nir = info->nir;

   size_t size = sizeof(struct hk_api_shader) +
                 sizeof(struct hk_shader) * hk_num_variants(info->stage);
   struct hk_api_shader *obj =
      vk_shader_zalloc(&dev->vk, &hk_shader_ops, info->stage, pAllocator, size);

   if (obj == NULL) {
      ralloc_free(nir);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   if (!nir->info.io_lowered) {
      hk_lower_nir(dev, nir, info->robustness, false, info->set_layout_count,
                   info->set_layouts, features);
   }

   mesa_shader_stage sw_stage = nir->info.stage;

   union hk_key key_tmp, *key = NULL;
   if (sw_stage == MESA_SHADER_FRAGMENT) {
      hk_populate_fs_key(&key_tmp.fs, state);
      key = &key_tmp;

      nir->info.fs.uses_sample_shading |= key->fs.force_sample_shading;

      /* G17P advertises only VK_SAMPLE_COUNT_1_BIT. Canonicalize sample
       * builtins and qualifiers now, before split FS lowering observes
       * uses_sample_shading and selects the legacy external sample loop. */
      if (dev->dev.chip == AGX_CHIP_G17P &&
          nir->info.fs.uses_sample_shading)
         NIR_PASS(_, nir, agx_nir_lower_g17p_1x_sample_shading);

      /* Force late-Z for Z/S self-deps. TODO: There's probably a less silly way
       * to do this.
       */
      if (key->fs.zs_self_dep) {
         nir_builder b =
            nir_builder_at(nir_before_impl(nir_shader_get_entrypoint(nir)));
         nir_discard_if(&b, nir_imm_false(&b));
         nir->info.fs.uses_discard = true;
      }

      NIR_PASS(_, nir, agx_nir_lower_sample_intrinsics, false);
   } else if (sw_stage == MESA_SHADER_VERTEX) {
      hk_populate_vs_key(&key_tmp.vs, state);
      key = &key_tmp;
   } else if (sw_stage == MESA_SHADER_TESS_CTRL) {
      NIR_PASS(_, nir, poly_nir_lower_tcs);
   }

   /* Compile all variants up front */
   if (sw_stage == MESA_SHADER_GEOMETRY) {
      struct hk_shader *main_variant = hk_main_gs_variant(obj);
      struct hk_shader *count_variant = hk_count_gs_variant(obj);

      nir_shader *count = NULL, *rast = NULL, *pre_gs = NULL;

      NIR_PASS(_, nir, poly_nir_lower_gs, &count, &rast, &pre_gs,
               &count_variant->info.gs);

      agx_preprocess_nir(count);
      agx_preprocess_nir(rast);
      agx_preprocess_nir(pre_gs);

      struct hk_shader *shader = &obj->variants[HK_GS_VARIANT_RAST];
      hk_lower_hw_vs(rast, shader, false, agx_chip_uses_g15_g16_shader_abi(dev->dev.chip),
                     features & HK_FEAT_DEPTH_CLIP_CONTROL);
      shader->info.gs = count_variant->info.gs;
      main_variant->info.gs = count_variant->info.gs;

      struct {
         nir_shader *in;
         struct hk_shader *out;
      } variants[] = {
         {nir, hk_main_gs_variant(obj)},
         {pre_gs, hk_pre_gs_variant(obj)},
         {count, count_variant},
         {rast, &obj->variants[HK_GS_VARIANT_RAST]},
      };

      for (unsigned v = 0; v < ARRAY_SIZE(variants); ++v) {
         if (variants[v].in) {
            nir_shader *input = variants[v].in;
            variants[v].in = NULL; /* hk_compile_nir consumes this input. */
            result =
               hk_compile_nir(dev, pAllocator, input, info->flags,
                              info->robustness, NULL, features, variants[v].out,
                              sw_stage, true, NULL, info->set_layout_count, info->set_layouts);

            if (result != VK_SUCCESS) {
               hk_api_shader_destroy(&dev->vk, &obj->vk, pAllocator);
               for (unsigned pending = v + 1; pending < ARRAY_SIZE(variants);
                    ++pending)
                  ralloc_free(variants[pending].in);
               return result;
            }
         }
      }
   } else if (sw_stage == MESA_SHADER_VERTEX ||
              sw_stage == MESA_SHADER_TESS_EVAL) {

      VkShaderStageFlags next_stage = info->next_stage_mask;

      /* Transform feedback and native adjacency lowering may insert a
       * passthrough GS even without an application geometry stage. Retain
       * its producer variant, including when topology is dynamic. */
      if (nir->xfb_info != NULL ||
          (agx_native_graphics(&dev->dev) &&
           (!key || key->vs.may_have_adjacency))) {
         next_stage |= VK_SHADER_STAGE_GEOMETRY_BIT;
      }

      if (sw_stage == MESA_SHADER_VERTEX) {
         assert(
            !(nir->info.inputs_read & BITFIELD64_MASK(VERT_ATTRIB_GENERIC0)) &&
            "Fixed-function attributes not used in Vulkan");

         NIR_PASS(_, nir, nir_recompute_io_bases, nir_var_shader_in);
      }

      /* Fold constant offset srcs for IO. */
      NIR_PASS(_, nir, nir_opt_constant_folding);

      for (enum hk_vs_variant v = 0; v < HK_VS_VARIANTS; ++v) {
         /* Only compile the software variant if we might use this shader with
          * geometry/tessellation. We need to compile the hardware variant
          * unconditionally to handle the VS -> null FS case, which does not
          * require setting the FRAGMENT bit.
          */
         if (v == HK_VS_VARIANT_SW &&
             !(next_stage & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                             VK_SHADER_STAGE_GEOMETRY_BIT)))
            continue;

         struct hk_shader *shader = &obj->variants[v];
         bool hw = v == HK_VS_VARIANT_HW;
         bool last = (v + 1) == HK_VS_VARIANTS;

         /* Each variant gets its own NIR. To save an extra clone, we use the
          * original NIR for the last stage.
          */
         nir_shader *clone = last ? nir : nir_shader_clone(NULL, nir);

         /* Without a declared fragment successor this hardware variant is
          * only used for VS -> null FS. Geometry/tessellation and transform
          * feedback consume the separate software variant's outputs.
          * Remove unconsumed user outputs before assigning a UVS layout;
          * fixed-function outputs and side effects remain intact. */
         if (hw && !(next_stage & VK_SHADER_STAGE_FRAGMENT_BIT)) {
            NIR_PASS(_, clone, nir_remove_outputs, MESA_SHADER_FRAGMENT,
                     BITFIELD64_RANGE(VARYING_SLOT_VAR0, 32), 0);
         }

         NIR_PASS(_, clone, agx_nir_gather_vs_inputs,
                  shader->info.vs.attrib_components_read);

         if (sw_stage == MESA_SHADER_VERTEX) {
            shader->info.vs.use_prolog = !(key && key->vs.skip_prolog);
            shader->info.vs.attribs_read =
               nir->info.inputs_read >> VERT_ATTRIB_GENERIC0;

            if (shader->info.vs.use_prolog) {
               NIR_PASS(_, clone, agx_nir_lower_vs_input_to_prolog);
            } else {
               struct agx_velem_key attribs[AGX_MAX_ATTRIBS];
               if (!agx_compact_velem_keys(
                      attribs, ARRAY_SIZE(attribs), key->vs.attribs,
                      ARRAY_SIZE(key->vs.attribs), shader->info.vs.attribs_read)) {
                  ralloc_free(clone);
                  if (!last)
                     ralloc_free(nir);
                  hk_api_shader_destroy(&dev->vk, &obj->vk, pAllocator);
                  return VK_ERROR_FEATURE_NOT_PRESENT;
               }

               struct agx_robustness agx_rs = {
                  .soft_fault = agx_has_soft_fault(&dev->dev),

                  /* Correctly handling GPL + pipeline-robustness requires
                   * runtime changes, so derive the level from the enabled
                   * device features exactly as the vertex prolog does
                   * (hk_cmd_draw.c:3200-3208) rather than pessimising every
                   * pipeline to D3D.  Hardcoding D3D here made every vertex
                   * fetch emit an out-of-bounds select even when the
                   * application never asked for robustness; the two paths
                   * must agree anyway, since a monolithic VS and a prologue
                   * fetch the same attributes.
                   */
                  .level =
                     (dev->vk.enabled_features.robustBufferAccess2 ||
                      dev->vk.enabled_features.pipelineRobustness)
                        ? AGX_ROBUSTNESS_D3D
                     : dev->vk.enabled_features.robustBufferAccess
                        ? AGX_ROBUSTNESS_GL
                        : AGX_ROBUSTNESS_DISABLED,
               };

               if (agx_chip_uses_hal200_ioto(dev->dev.chip))
                  agx_nir_lower_vbo_raw(clone, attribs, agx_rs,
                                        !key->vs.static_strides);
               else
                  agx_nir_lower_vbo(clone, attribs, agx_rs,
                                    !key->vs.static_strides);

               unsigned nr = DIV_ROUND_UP(
                  BITSET_LAST_BIT(shader->info.vs.attrib_components_read), 4);
               agx_nir_lower_non_monolithic_uniforms(clone, nr);
            }
         }

         /* hk_compile_nir takes ownership of the clone */
         result =
            hk_compile_nir(dev, pAllocator, clone, info->flags,
                           info->robustness, key, features, shader, sw_stage,
                           hw, nir->xfb_info, info->set_layout_count, info->set_layouts);
         if (result != VK_SUCCESS) {
            hk_api_shader_destroy(&dev->vk, &obj->vk, pAllocator);
            if (!last)
               ralloc_free(nir);
            return result;
         }
      }
   } else {
      struct hk_shader *shader = hk_only_variant(obj);
      const bool direct_variant = sw_stage == MESA_SHADER_FRAGMENT &&
         agx_chip_uses_g15_g16_shader_abi(dev->dev.chip) &&
         debug_get_bool_option("HK_M4_PREFETCH_UBOS", false) &&
         debug_get_bool_option("HK_M4_PREFETCH_VARIANT", false);
      nir_shader *direct_nir = direct_variant ? nir_shader_clone(NULL, nir) : NULL;

      /* hk_compile_nir takes ownership of nir */
      result = hk_compile_nir(dev, pAllocator, nir, info->flags,
                              info->robustness, key, features, shader, sw_stage,
                              true, NULL, info->set_layout_count, info->set_layouts);
      if (result == VK_SUCCESS && direct_nir &&
          (shader->info.ubo_prefetch_count ||
           debug_get_bool_option("HK_M4_PLAIN_SAMPLERS", false))) {
         struct hk_shader *direct = &obj->variants[1];
         direct->info.ubo_prefetch_direct = true;
         result = hk_compile_nir(dev, pAllocator, direct_nir, info->flags,
                                info->robustness, key, features, direct, sw_stage,
                                true, NULL, info->set_layout_count, info->set_layouts);
         direct_nir = NULL;
      }
      ralloc_free(direct_nir);
      if (result != VK_SUCCESS) {
         hk_api_shader_destroy(&dev->vk, &obj->vk, pAllocator);
         return result;
      }
   }

   *shader_out = obj;
   return VK_SUCCESS;
}

static void
nir_opts(nir_shader *nir, void *data)
{
   bool progress;

   do {
      progress = false;

      NIR_PASS(progress, nir, nir_opt_loop);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);

      NIR_PASS(progress, nir, nir_opt_if, 0);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);

      NIR_PASS(progress, nir, nir_opt_peephole_select,
               &(nir_opt_peephole_select_options){
                  .limit = 8,
                  .expensive_alu_ok = true,
                  .discard_ok = true,
               });

      NIR_PASS(progress, nir, nir_opt_phi_precision);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);
   } while (progress);
}

/* Native varyings use 32-bit components. Widen narrow integer interfaces
 * before interstage optimization: nir_opt_varyings may retype flat IO as
 * float, after which a numeric half-float conversion loses integer NaN bits.
 * Keep narrowing inside the consumer, where it is ordinary integer ALU.
 */
static bool
hk_widen_integer_io(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   bool store;
   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output:
      if (b->shader->info.stage == MESA_SHADER_FRAGMENT)
         return false;
      store = true;
      break;
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_vertex_input:
      if (b->shader->info.stage == MESA_SHADER_VERTEX)
         return false;
      store = false;
      break;
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_output:
      store = false;
      break;
   default:
      return false;
   }

   nir_alu_type type = store ? nir_intrinsic_src_type(intr)
                            : nir_intrinsic_dest_type(intr);
   nir_alu_type base = nir_alu_type_get_base_type(type);
   unsigned bits = store ? intr->src[0].ssa->bit_size : intr->def.bit_size;
   if (bits != 16 || (base != nir_type_int && base != nir_type_uint))
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (store && sem.no_varying)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   if (store) {
      /* XFB observes the original component width independently of the
       * internal varying representation. Retain a separate XFB-only store. */
      if (nir_instr_xfb_write_mask(intr)) {
         nir_intrinsic_instr *xfb = nir_instr_as_intrinsic(
            nir_instr_clone(b->shader, &intr->instr));
         nir_io_semantics xfb_sem = sem;
         xfb_sem.no_varying = true;
         nir_intrinsic_set_io_semantics(xfb, xfb_sem);
         nir_builder_instr_insert(b, &xfb->instr);
         nir_intrinsic_set_io_xfb(intr, (nir_io_xfb){0});
      }
      nir_def *value = nir_convert_to_bit_size(b, intr->src[0].ssa, base, 32);
      nir_src_rewrite(&intr->src[0], value);
      nir_intrinsic_set_src_type(intr, base | 32);
   } else {
      intr->def.bit_size = 32;
      nir_intrinsic_set_dest_type(intr, base | 32);
      b->cursor = nir_after_instr(&intr->instr);
      nir_def *value = nir_u2u16(b, &intr->def);
      nir_def_rewrite_uses_after(&intr->def, value);
   }
   return true;
}

static VkResult
hk_compile_shaders(struct vk_device *vk_dev, uint32_t shader_count,
                   struct vk_shader_compile_info *infos,
                   const struct vk_graphics_pipeline_state *state,
                   const struct vk_features *features,
                   const VkAllocationCallbacks *pAllocator,
                   struct vk_shader **shaders_out)
{
   struct hk_device *dev = container_of(vk_dev, struct hk_device, vk);
   nir_shader *shaders[shader_count];

   /* Lower shaders, notably lowering I/O. This is a prerequisite for
    * intershader optimization.
    */
   for (uint32_t i = 0; i < shader_count; i++) {
      const struct vk_shader_compile_info *info = &infos[i];
      /* TODO: Multiview with ESO */
      const bool is_multiview = state && state->mv->view_mask != 0;
      enum hk_feature_key hk_features = hk_make_feature_key(dev->vk.physical, features);
      nir_shader *nir = info->nir;

      hk_lower_nir(dev, nir, info->robustness, is_multiview,
                   info->set_layout_count, info->set_layouts, hk_features);

      if (nir->xfb_info) {
         /* Fold constant offset srcs for IO. */
         NIR_PASS(_, nir, nir_opt_constant_folding);

         nir_io_add_intrinsic_xfb_info(nir);
      }

      if (agx_chip_uses_hal200_ioto(dev->dev.chip))
         NIR_PASS(_, nir, nir_shader_intrinsics_pass, hk_widen_integer_io,
                  nir_metadata_control_flow, NULL);

      shaders[i] = nir;
   }

   /* Static rasterizer discard makes user varyings unobservable (XFB writes
    * are preserved by nir_remove_outputs). Missing fragment stages alone do
    * not: this may be a pre-raster pipeline library, linked to a fragment
    * library later. state->pipeline_flags contains mergeable rendering flags,
    * not VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR, so it cannot distinguish them.
    */
   if (agx_chip_uses_hal200_ioto(dev->dev.chip) && shader_count > 0 &&
       state && state->rs && state->rs->rasterizer_discard_enable &&
       !BITSET_TEST(state->dynamic,
                    MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE)) {
      nir_shader *last = shaders[shader_count - 1];
      if (last->info.stage <= MESA_SHADER_GEOMETRY)
         NIR_PASS(_, last, nir_remove_outputs, MESA_SHADER_FRAGMENT,
                  BITFIELD64_RANGE(VARYING_SLOT_VAR0, 32), 0);
   }

   nir_opt_varyings_bulk(shaders, shader_count, true, UINT32_MAX, UINT32_MAX,
                         nir_opts, NULL);

   for (uint32_t i = 0; i < shader_count; i++) {
      VkResult result =
         hk_compile_shader(dev, &infos[i], state, features, pAllocator,
                           (struct hk_api_shader **)&shaders_out[i]);
      if (result != VK_SUCCESS) {
         /* Clean up all the shaders before this point */
         for (uint32_t j = 0; j < i; j++)
            hk_api_shader_destroy(&dev->vk, shaders_out[j], pAllocator);

         /* Clean up all the NIR after this point */
         for (uint32_t j = i + 1; j < shader_count; j++)
            ralloc_free(infos[j].nir);

         /* Memset the output array */
         memset(shaders_out, 0, shader_count * sizeof(*shaders_out));

         return result;
      }
   }

   return VK_SUCCESS;
}

static VkResult
hk_deserialize_shader(struct hk_device *dev, struct blob_reader *blob,
                      struct hk_shader *shader)
{
   struct hk_shader_info info;
   blob_copy_bytes(blob, &info, sizeof(info));

   struct agx_shader_info b_info;
   blob_copy_bytes(blob, &b_info, sizeof(b_info));

   struct agx_vdm_state_update_program_info agx3_vdm_state_update;
   if (!hk_agx3_vdm_program_cache_deserialize(blob,
                                               &agx3_vdm_state_update))
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   const uint32_t code_size = blob_read_uint32(blob);
   const uint32_t data_size = blob_read_uint32(blob);
   if (blob->overrun)
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   VkResult result = hk_init_link_ht(shader, info.stage);
   if (result != VK_SUCCESS)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   simple_mtx_init(&shader->linked.lock, mtx_plain);

   shader->b.info = b_info;
   shader->info = info;
   shader->agx3_vdm_state_update = agx3_vdm_state_update;
   shader->code_size = code_size;
   shader->data_size = data_size;
   shader->b.info.binary_size = code_size;

   shader->code_ptr = malloc(code_size);
   if (shader->code_ptr == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   shader->data_ptr = malloc(data_size);
   if (shader->data_ptr == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   blob_copy_bytes(blob, (void *)shader->code_ptr, shader->code_size);
   blob_copy_bytes(blob, (void *)shader->data_ptr, shader->data_size);
   if (blob->overrun)
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   shader->b.binary = (void *)shader->code_ptr;
   hk_upload_shader(dev, shader);
   return VK_SUCCESS;
}

static VkResult
hk_deserialize_api_shader(struct vk_device *vk_dev, struct blob_reader *blob,
                          uint32_t binary_version,
                          const VkAllocationCallbacks *pAllocator,
                          struct vk_shader **shader_out)
{
   struct hk_device *dev = container_of(vk_dev, struct hk_device, vk);

   if (!hk_agx3_shader_binary_version_supported(binary_version))
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   mesa_shader_stage stage = blob_read_uint8(blob);
   if (blob->overrun)
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   size_t size = sizeof(struct hk_api_shader) +
                 sizeof(struct hk_shader) * hk_num_variants(stage);

   struct hk_api_shader *obj =
      vk_shader_zalloc(&dev->vk, &hk_shader_ops, stage, pAllocator, size);

   if (obj == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   hk_foreach_variant(obj, shader) {
      VkResult result = hk_deserialize_shader(dev, blob, shader);

      if (result != VK_SUCCESS) {
         hk_api_shader_destroy(&dev->vk, &obj->vk, pAllocator);
         return result;
      }
   }

   *shader_out = &obj->vk;
   return VK_SUCCESS;
}

static void
hk_shader_serialize(struct vk_device *vk_dev, const struct hk_shader *shader,
                    struct blob *blob)
{
   blob_write_bytes(blob, &shader->info, sizeof(shader->info));
   blob_write_bytes(blob, &shader->b.info, sizeof(shader->b.info));
   if (!hk_agx3_vdm_program_cache_serialize(
          blob, &shader->agx3_vdm_state_update))
      return;

   blob_write_uint32(blob, shader->code_size);
   blob_write_uint32(blob, shader->data_size);
   blob_write_bytes(blob, shader->code_ptr, shader->code_size);
   blob_write_bytes(blob, shader->data_ptr, shader->data_size);
}

static bool
hk_api_shader_serialize(struct vk_device *vk_dev,
                        const struct vk_shader *vk_shader, struct blob *blob)
{
   struct hk_api_shader *obj =
      container_of(vk_shader, struct hk_api_shader, vk);

   blob_write_uint8(blob, vk_shader->stage);

   hk_foreach_variant(obj, shader) {
      hk_shader_serialize(vk_dev, shader, blob);
   }

   return !blob->out_of_memory;
}

static VkResult
hk_shader_get_executable_properties(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t *executable_count, VkPipelineExecutablePropertiesKHR *properties)
{
   struct hk_api_shader *obj =
      container_of(vk_shader, struct hk_api_shader, vk);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out, properties,
                          executable_count);

   vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props)
   {
      props->stages = mesa_to_vk_shader_stage(obj->vk.stage);
      props->subgroupSize = 32;
      VK_COPY_STR(props->name, _mesa_shader_stage_to_string(obj->vk.stage));
      VK_PRINT_STR(props->description, "%s shader",
                   _mesa_shader_stage_to_string(obj->vk.stage));
   }

   return vk_outarray_status(&out);
}

static VkResult
hk_shader_get_executable_statistics(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *statistic_count,
   VkPipelineExecutableStatisticKHR *statistics)
{
   struct hk_api_shader *obj =
      container_of(vk_shader, struct hk_api_shader, vk);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out, statistics,
                          statistic_count);

   assert(executable_index == 0);

   /* TODO: find a sane way to report multiple variants and have that play nice
    * with zink.
    */
   struct hk_shader *shader = hk_any_variant(obj);
   vk_add_agx2_stats(out, &shader->b.info.stats);
   return vk_outarray_status(&out);
}

static bool
write_ir_text(VkPipelineExecutableInternalRepresentationKHR *ir,
              const char *data)
{
   ir->isText = VK_TRUE;

   size_t data_len = strlen(data) + 1;

   if (ir->pData == NULL) {
      ir->dataSize = data_len;
      return true;
   }

   strncpy(ir->pData, data, ir->dataSize);
   if (ir->dataSize < data_len)
      return false;

   ir->dataSize = data_len;
   return true;
}

static VkResult
hk_shader_get_executable_internal_representations(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *internal_representation_count,
   VkPipelineExecutableInternalRepresentationKHR *internal_representations)
{
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          internal_representations,
                          internal_representation_count);
   bool incomplete_text = false;

   assert(executable_index == 0);

   /* TODO */
#if 0
   vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
      VK_COPY_STR(ir->name, "AGX assembly");
      VK_COPY_STR(ir->description, "AGX assembly");
      if (!write_ir_text(ir, TODO))
         incomplete_text = true;
   }
#endif

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}

static const struct vk_shader_ops hk_shader_ops = {
   .destroy = hk_api_shader_destroy,
   .serialize = hk_api_shader_serialize,
   .get_executable_properties = hk_shader_get_executable_properties,
   .get_executable_statistics = hk_shader_get_executable_statistics,
   .get_executable_internal_representations =
      hk_shader_get_executable_internal_representations,
};

const struct vk_device_shader_ops hk_device_shader_ops = {
   .get_nir_options = hk_get_nir_options,
   .get_spirv_options = hk_get_spirv_options,
   .preprocess_nir = hk_preprocess_nir,
   .hash_state = hk_hash_graphics_state,
   .compile = hk_compile_shaders,
   .deserialize = hk_deserialize_api_shader,
   .cmd_set_dynamic_graphics_state = vk_cmd_set_dynamic_graphics_state,
   .cmd_bind_shaders = hk_cmd_bind_shaders,
};

struct hk_linked_shader *
hk_fast_link(struct hk_device *dev, bool fragment, struct hk_shader *main,
             struct agx_shader_part *prolog, struct agx_shader_part *epilog,
             unsigned nr_samples_shaded)
{
   struct hk_linked_shader *s = rzalloc(NULL, struct hk_linked_shader);
   if (!s)
      return NULL;
   if (!agx_fast_link(&s->b, &dev->dev, fragment, &main->b, prolog, epilog,
                      nr_samples_shaded)) {
      ralloc_free(s);
      return NULL;
   }

   if (fragment) {
      unsigned samplers = main->b.info.sampler_state_count;
      if (s->b.uses_txf)
         samplers = MAX2(samplers, 1);

      agx_pack(&s->fs_counts, FRAGMENT_SHADER_WORD_0, cfg) {
         cfg.cf_binding_count = s->b.cf.nr_bindings;
         cfg.uniform_register_count = main->b.info.push_count;
         cfg.preshader_register_count = main->b.info.nr_preamble_gprs;
         cfg.texture_state_register_count = main->b.info.texture_state_count;
         cfg.sampler_state_register_count =
            agx_translate_sampler_state_count(samplers, false);
      }
   }

   /* HAL200 consumes the joined Ioto program built at command-recording time.
    * The legacy USC list below is not part of that contract. */
   if (agx_chip_command_stream_generation(dev->dev.chip) !=
       AGX_CS_GENERATION_AGX2)
      return s;

   /* Now that we've linked, bake the USC words to bind this program */
   struct agx_usc_builder b = agx_usc_builder(s->usc.data, sizeof(s->usc.data));

   if (main && main->b.info.rodata.size_16) {
      agx_usc_immediates(&b, &main->b.info.rodata, main->bo->va->addr);
   }

   if (s->b.uses_txf)
      agx_usc_push_packed(&b, SAMPLER, dev->dev.txf_sampler);

   agx_usc_shared_non_fragment(&b, &main->b.info, 0);
   agx_usc_push_packed(&b, SHADER, s->b.shader);
   agx_usc_push_packed(&b, REGISTERS, s->b.regs);

   if (fragment)
      agx_usc_push_packed(&b, FRAGMENT_PROPERTIES, s->b.fragment_props);

   if (main && main->b.info.has_preamble) {
      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code = agx_usc_addr(&dev->dev, main->preamble_addr);
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
   }

   s->usc.size = b.head - s->usc.data;
   return s;
}
