/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_compile.h"
#include "nir_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned
count_pattern(const uint8_t *bytes, size_t size, const uint8_t *pattern,
              size_t pattern_size)
{
   unsigned count = 0;
   for (size_t i = 0; i + pattern_size <= size; ++i)
      count += memcmp(bytes + i, pattern, pattern_size) == 0;
   return count;
}

static bool
check_barrier(mesa_scope execution_scope, nir_variable_mode modes)
{
   static const uint8_t barrier[] = {0x07, 0x04, 0x54,
                                     0x61, 0x09, 0x00};
   static const uint8_t fence[] = {0x07, 0x04, 0x54, 0x84, 0x0a, 0x00};
   static const uint8_t texture_fence[] = {
      0x07, 0x04, 0x54, 0x50, 0x06, 0x00,
      0x07, 0x04, 0x54, 0xd0, 0x06, 0x00,
   };
   static const uint8_t end[] = {0x0e, 0x00, 0x00, 0x00};
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "G17P shared barrier exchange");
   b.shader->info.workgroup_size[0] = 32;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.shared_size = 32 * sizeof(uint32_t);

   nir_def *lane = nir_load_local_invocation_index(&b);
   nir_def *offset = nir_imul_imm(&b, lane, sizeof(uint32_t));
   nir_store_shared(&b, nir_iadd_imm(&b, lane, 0x1234), offset,
                    .align_mul = 4);
   nir_barrier(&b, .execution_scope = execution_scope,
               .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL,
               .memory_modes = modes);
   nir_def *peer = nir_load_shared(&b, 1, 32, nir_imm_int(&b, 0),
                                   .align_mul = 4);
   nir_def *output = nir_load_preamble(&b, 1, 64, .base = 0);
   nir_store_global(&b, nir_iadd(&b, peer, lane), output, .align_mul = 4);
   agx_preprocess_nir(b.shader);

   struct agx_shader_key key = {
      .dev.encoding = AGX_ISA_ENCODING_AGX3B,
      .reserved_preamble = 4,
      .no_preamble = true,
   };
   struct agx_shader_part part = {0};
   const bool compiled = agx_compile_shader_nir(b.shader, &key, &part);
   const bool valid =
      compiled && part.binary != NULL &&
      part.info.local_size == 32 * sizeof(uint32_t) &&
      part.info.workgroup_size[0] == 32 && part.info.workgroup_size[1] == 1 &&
      part.info.workgroup_size[2] == 1 &&
      count_pattern(part.binary, part.info.binary_size, barrier,
                    sizeof(barrier)) == (execution_scope == SCOPE_WORKGROUP) &&
      count_pattern(part.binary, part.info.binary_size, fence,
                    sizeof(fence)) ==
         (execution_scope != SCOPE_WORKGROUP) * (1 + !!(modes & nir_var_image)) &&
      count_pattern(part.binary, part.info.binary_size, texture_fence,
                    sizeof(texture_fence)) == !!(modes & nir_var_image) &&
      part.info.binary_size >= sizeof(end) &&
      memcmp((const uint8_t *)part.binary + part.info.binary_size - sizeof(end),
             end, sizeof(end)) == 0;

   if (!valid)
      fprintf(stderr, "AGX3B barrier oracle failed: execution=%u modes=%u\n",
              execution_scope, modes);

   free(part.binary);
   ralloc_free(b.shader);
   return valid;
}

static bool
check_conditional_image_fence(nir_intrinsic_op op)
{
   const uint8_t device_barrier[] = {0x07, 0x04, 0x54, 0x85, 0x08, 0x00};
   const uint8_t texture_barrier[] = {0x07, 0x04, 0x54, 0x51, 0x0e, 0x00};
   const uint8_t device_fence[] = {0x07, 0x04, 0x54, 0x84, 0x0a, 0x00};
   const uint8_t texture_fence[] = {0x07, 0x04, 0x54, 0x50, 0x06, 0x00};
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "conditional image ordering");
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = b.shader->info.workgroup_size[2] = 1;
   nir_def *lane = nir_load_local_invocation_index(&b);
   nir_push_if(&b, nir_ieq_imm(&b, nir_iand_imm(&b, lane, 1), 0));
   nir_intrinsic_instr *fence = nir_intrinsic_instr_create(b.shader, op);
   nir_builder_instr_insert(&b, &fence->instr);
   nir_pop_if(&b, NULL);
   agx_preprocess_nir(b.shader);

   struct agx_shader_key key = {
      .dev.encoding = AGX_ISA_ENCODING_AGX3B,
      .no_preamble = true,
   };
   struct agx_shader_part part = {0};
   bool valid = agx_compile_shader_nir(b.shader, &key, &part) && part.binary &&
      !count_pattern(part.binary, part.info.binary_size, device_barrier,
                     sizeof(device_barrier)) &&
      !count_pattern(part.binary, part.info.binary_size, texture_barrier,
                     sizeof(texture_barrier)) &&
      count_pattern(part.binary, part.info.binary_size, texture_fence,
                    sizeof(texture_fence)) == 1 &&
      count_pattern(part.binary, part.info.binary_size, device_fence,
                    sizeof(device_fence)) ==
         (op == nir_intrinsic_fence_mem_to_tex_agx);
   if (!valid)
      fprintf(stderr, "AGX3B conditional image fence oracle failed: %u\n", op);
   free(part.binary);
   ralloc_free(b.shader);
   return valid;
}

int
main(void)
{
   if (!check_barrier(SCOPE_WORKGROUP, nir_var_mem_shared) ||
       !check_barrier(SCOPE_SUBGROUP, nir_var_mem_shared) ||
       !check_barrier(SCOPE_NONE, nir_var_mem_shared) ||
       !check_barrier(SCOPE_SUBGROUP, nir_var_mem_shared | nir_var_image))
      return 1;

   if (!check_conditional_image_fence(nir_intrinsic_fence_pbe_to_tex_agx) ||
       !check_conditional_image_fence(nir_intrinsic_fence_mem_to_tex_agx) ||
       !check_conditional_image_fence(nir_intrinsic_fence_pbe_to_tex_pixel_agx))
      return 1;

   puts("agx3_workgroup: workgroup rendezvous and non-rendezvous fences verified");
   return 0;
}
