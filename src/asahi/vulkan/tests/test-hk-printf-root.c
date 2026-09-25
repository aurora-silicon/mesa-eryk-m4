/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_cmd_buffer.h"
#include "hk_buffer.h"
#include "hk_descriptor_set_layout.h"
#include "hk_shader.h"

#include "nir_builder.h"
#include "vk_pipeline.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void
check_direct_descriptor(mesa_shader_stage stage, bool storage,
                        unsigned product, bool enabled, bool expect_direct)
{
   nir_builder b = nir_builder_init_simple_shader(stage, NULL, "descriptor provenance");
   struct hk_descriptor_set_layout *layout = calloc(1, sizeof(*layout) + sizeof(layout->binding[0]));
   layout->binding_count = 1;
   layout->binding[0].type = storage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   layout->binding[0].array_size = 1;
   layout->binding[0].stride = 16;
   struct vk_descriptor_set_layout *sets[] = {&layout->vk};
   nir_descriptor_type type = storage ? nir_descriptor_type_storage_buffer : nir_descriptor_type_uniform_buffer;
   nir_def *idx = nir_vulkan_resource_index(&b, 3, 32, nir_imm_int(&b, 0),
                                           .desc_set = 0, .binding = 0, .desc_type = type);
   nir_def *desc = nir_load_vulkan_descriptor(&b, 4, 32, idx, .desc_type = type);
   nir_store_global(&b, desc, nir_imm_int64(&b, 0x10000), .align_mul = 16);
   if (product) {
      nir_def *coord = nir_load_frag_coord(&b);
      nir_def *w = nir_channel(&b, coord, 3);
      nir_def *inv = nir_frcp(&b, w);
      nir_def *chain = nir_fmul(&b, nir_fmul(&b, nir_channel(&b, coord, 0), inv), nir_imm_float(&b, 2));
      /* Same-denominator cancellation is sensitive; an unrelated factor is not. */
      nir_def *value = nir_fmul(&b, chain, product == 1 ? w : nir_channel(&b, coord, 2));
      nir_store_global(&b, value, nir_imm_int64(&b, 0x10100), .align_mul = 4);
   }
   const struct vk_pipeline_robustness_state rs = {
      .storage_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .uniform_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .vertex_inputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED_EXT,
   };
   assert(hk_nir_lower_descriptors(b.shader, AGX_CHIP_G15S, false, enabled, &rs, 1, sets));
   bool marked = false;
   nir_foreach_block(block, b.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_alu) continue;
         nir_alu_instr *alu = nir_instr_as_alu(instr);
         if (alu->op != nir_op_ior || alu->def.bit_size != 64) continue;
         for (unsigned i = 0; i < 2; ++i)
            marked |= nir_src_is_const(alu->src[i].src) &&
                      nir_src_as_uint(alu->src[i].src) == HK_BUFFER_ADDRESS_DIRECT_BIT;
      }
   }
   assert(marked == expect_direct);
   nir_validate_shader(b.shader, "descriptor provenance boundary");
   ralloc_free(b.shader); free(layout);
}

static bool
source_is_constant(nir_src src, uint64_t expected)
{
   if (nir_src_is_const(src))
      return nir_src_as_uint(src) == expected;

   if (!nir_def_is_alu(src.ssa))
      return false;

   nir_alu_instr *alu = nir_def_as_alu(src.ssa);
   return (alu->op == nir_op_u2u64 || alu->op == nir_op_i2i64) &&
          nir_src_is_const(alu->src[0].src) &&
          nir_src_as_uint(alu->src[0].src) == expected;
}

int
main(void)
{
   check_direct_descriptor(MESA_SHADER_COMPUTE, false, 0, true, true);
   check_direct_descriptor(MESA_SHADER_COMPUTE, true, 0, true, true);
   check_direct_descriptor(MESA_SHADER_FRAGMENT, false, 0, true, true);
   check_direct_descriptor(MESA_SHADER_FRAGMENT, false, 1, true, false);
   check_direct_descriptor(MESA_SHADER_FRAGMENT, false, 2, true, true);
   check_direct_descriptor(MESA_SHADER_FRAGMENT, true, 0, true, true);
   check_direct_descriptor(MESA_SHADER_FRAGMENT, true, 1, true, false);
   check_direct_descriptor(MESA_SHADER_FRAGMENT, false, 0, false, false);
   assert(hk_buffer_addr_format(
             VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT, false) ==
          nir_address_format_64bit_global_32bit_offset);
   assert(hk_buffer_addr_format(
             VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT, true) ==
          nir_address_format_64bit_bounded_global);
   assert(hk_buffer_addr_format(
             VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT,
             false) == nir_address_format_64bit_bounded_global);
   const struct hk_addr_range null_buffer =
      hk_buffer_addr_range(NULL, 0, VK_WHOLE_SIZE, true);
   assert(null_buffer.addr == AGX_ZERO_PAGE_ADDRESS && null_buffer.range == 0);

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, "Honeykrisp runtime printf address");
   nir_def *address = nir_load_printf_buffer_address(&b, 64);
   nir_store_global(&b, address, nir_imm_int64(&b, UINT64_C(0x2000000000)),
                    .align_mul = 8);

   const struct vk_pipeline_robustness_state robustness = {
      .storage_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .uniform_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .vertex_inputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED_EXT,
   };
   assert(hk_nir_lower_descriptors(b.shader, AGX_CHIP_G15S, false, false, &robustness, 0, NULL));

   /* Reproduce an assertion inserted by texture lowering after the first
    * descriptor pass. The late driver-sysval pass must resolve it too. */
   address = nir_load_printf_buffer_address(&b, 64);
   nir_store_global(&b, address, nir_imm_int64(&b, UINT64_C(0x2000000008)),
                    .align_mul = 8);
   assert(hk_lower_uvs_index(b.shader, MESA_SHADER_COMPUTE, 0));

   const uint32_t expected = hk_root_descriptor_offset(printf_buffer_address);
   bool saw_root = false;
   bool saw_offset = false;

   nir_foreach_function_impl(impl, b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            assert(intr->intrinsic != nir_intrinsic_load_printf_buffer_address);
            saw_root |= intr->intrinsic == nir_intrinsic_load_root_agx;

            if (intr->intrinsic != nir_intrinsic_load_global_constant)
               continue;

            nir_def *pointer = intr->src[0].ssa;
            if (!nir_def_is_alu(pointer))
               continue;

            nir_alu_instr *add = nir_def_as_alu(pointer);
            if (add->op != nir_op_iadd)
               continue;

            for (unsigned i = 0; i < 2; ++i) {
               if (source_is_constant(add->src[i].src, expected))
                  saw_offset = true;
            }
         }
      }
   }

   assert((expected & 7) == 0);
   if (!saw_root || !saw_offset)
      nir_print_shader(b.shader, stderr);
   assert(saw_root && saw_offset);
   ralloc_free(b.shader);
   return 0;
}
