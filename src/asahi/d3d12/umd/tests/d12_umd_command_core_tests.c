/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_umd_command_core.h"

#include <assert.h>
#include <stdio.h>

static void
expect_type(uint32_t list_type, uint32_t queue_flags,
            enum d12_umd_renderer_command_type expected)
{
   enum d12_umd_renderer_command_type actual = D12_UMD_RENDERER_COMMAND_COPY;

   assert(d12_umd_command_type_from_ddi(list_type, queue_flags, &actual));
   assert(actual == expected);
}

static void
test_command_signatures(void)
{
   const struct d12_frontend_root_parameter parameters[] = {
      {.parameter_type = 1, .constants = {.num_32bit_values = 4}},
      {.parameter_type = 2},
      {.parameter_type = 3},
      {.parameter_type = 4},
   };
   const struct d12_frontend_root_signature_desc root = {
      .num_parameters = 4,
      .parameters = parameters,
   };
   struct d12_frontend_indirect_argument_desc arguments[] = {
      {.type = 5,
       .root_parameter_index = 0,
       .destination_offset = 1,
       .value_count = 2},
      {.type = 6, .root_parameter_index = 1},
      {.type = 7, .root_parameter_index = 2},
      {.type = 8, .root_parameter_index = 3},
      {.type = 3, .root_parameter_index = 7},
      {.type = 4},
      {.type = 1},
   };
   struct d12_frontend_command_signature_desc desc = {
      .byte_stride = 84,
      .argument_count = 7,
      .arguments = arguments,
      .root_signature = (void *)(uintptr_t)1,
      .node_mask = 1,
   };

   assert(d12_umd_command_signature_desc_valid(&desc, &root));

   arguments[0].value_count = 4;
   assert(!d12_umd_command_signature_desc_valid(&desc, &root));
   arguments[0].value_count = 2;

   arguments[1].root_parameter_index = 0;
   assert(!d12_umd_command_signature_desc_valid(&desc, &root));
   arguments[1].root_parameter_index = 1;

   arguments[4].root_parameter_index = 32;
   assert(!d12_umd_command_signature_desc_valid(&desc, &root));
   arguments[4].root_parameter_index = 7;

   arguments[6].type = 0;
   assert(!d12_umd_command_signature_desc_valid(&desc, &root));
   arguments[6].type = 2;
   assert(!d12_umd_command_signature_desc_valid(&desc, &root));
   arguments[6].type = 1;

   desc.byte_stride = 80;
   assert(!d12_umd_command_signature_desc_valid(&desc, &root));
   desc.byte_stride = 84;

   assert(!d12_umd_command_signature_desc_valid(&desc, NULL));

   {
      const struct d12_frontend_indirect_argument_desc draw = {.type = 0};
      const struct d12_frontend_command_signature_desc terminal_only = {
         .byte_stride = 16,
         .argument_count = 1,
         .arguments = &draw,
      };
      assert(d12_umd_command_signature_desc_valid(&terminal_only, NULL));
      assert(!d12_umd_command_signature_desc_valid(&terminal_only, &root));
   }

   {
      const struct d12_frontend_indirect_argument_desc overlap[] = {
         {.type = 5,
          .root_parameter_index = 0,
          .destination_offset = 0,
          .value_count = 3},
         {.type = 5,
          .root_parameter_index = 0,
          .destination_offset = 2,
          .value_count = 1},
         {.type = 0},
      };
      const struct d12_frontend_command_signature_desc overlap_desc = {
         .byte_stride = 32,
         .argument_count = 3,
         .arguments = overlap,
         .root_signature = (void *)(uintptr_t)1,
      };
      assert(!d12_umd_command_signature_desc_valid(&overlap_desc, &root));
   }

   {
      const struct d12_frontend_indirect_argument_desc duplicate_vb[] = {
         {.type = 3, .root_parameter_index = 3},
         {.type = 3, .root_parameter_index = 3},
         {.type = 0},
      };
      const struct d12_frontend_command_signature_desc duplicate_vb_desc = {
         .byte_stride = 48,
         .argument_count = 3,
         .arguments = duplicate_vb,
      };
      assert(!d12_umd_command_signature_desc_valid(&duplicate_vb_desc,
                                                    NULL));

      struct d12_frontend_indirect_argument_desc high_vb[] = {
         {.type = 3, .root_parameter_index = 31},
         {.type = 0},
      };
      const struct d12_frontend_command_signature_desc high_vb_desc = {
         .byte_stride = 32,
         .argument_count = 2,
         .arguments = high_vb,
      };
      assert(d12_umd_command_signature_desc_valid(&high_vb_desc, NULL));
      high_vb[0].root_parameter_index = 32;
      assert(!d12_umd_command_signature_desc_valid(&high_vb_desc, NULL));
   }
}

int
main(void)
{
   expect_type(D12_UMD_DDI_COMMAND_LIST_DIRECT, D12_UMD_DDI_QUEUE_3D,
               D12_UMD_RENDERER_COMMAND_DIRECT);
   expect_type(D12_UMD_DDI_COMMAND_LIST_DIRECT,
               D12_UMD_DDI_QUEUE_3D | D12_UMD_DDI_QUEUE_COMPUTE |
                  D12_UMD_DDI_QUEUE_COPY,
               D12_UMD_RENDERER_COMMAND_DIRECT);
   expect_type(D12_UMD_DDI_COMMAND_LIST_DIRECT,
               D12_UMD_DDI_QUEUE_COMPUTE,
               D12_UMD_RENDERER_COMMAND_COMPUTE);
   expect_type(D12_UMD_DDI_COMMAND_LIST_DIRECT,
               D12_UMD_DDI_QUEUE_COMPUTE | D12_UMD_DDI_QUEUE_COPY,
               D12_UMD_RENDERER_COMMAND_COMPUTE);
   expect_type(D12_UMD_DDI_COMMAND_LIST_DIRECT, D12_UMD_DDI_QUEUE_COPY,
               D12_UMD_RENDERER_COMMAND_COPY);
   expect_type(D12_UMD_DDI_COMMAND_LIST_BUNDLE, D12_UMD_DDI_QUEUE_3D,
               D12_UMD_RENDERER_COMMAND_BUNDLE);

   assert(!d12_umd_command_type_from_ddi(
      D12_UMD_DDI_COMMAND_LIST_DIRECT, 0, NULL));
   assert(!d12_umd_command_type_from_ddi(
      D12_UMD_DDI_COMMAND_LIST_DIRECT, 0,
      &(enum d12_umd_renderer_command_type){0}));
   assert(!d12_umd_command_type_from_ddi(
      D12_UMD_DDI_COMMAND_LIST_DIRECT, UINT32_C(0x8),
      &(enum d12_umd_renderer_command_type){0}));
   assert(!d12_umd_command_type_from_ddi(
      D12_UMD_DDI_COMMAND_LIST_BUNDLE, D12_UMD_DDI_QUEUE_COMPUTE,
      &(enum d12_umd_renderer_command_type){0}));
   assert(!d12_umd_command_type_from_ddi(
      UINT32_C(2), D12_UMD_DDI_QUEUE_3D,
      &(enum d12_umd_renderer_command_type){0}));

   assert(d12_umd_command_list_flags_valid(0, false));
   assert(d12_umd_command_list_flags_valid(
      D12_UMD_DDI_COMMAND_LIST_ENABLE_MARKERS, false));
   assert(!d12_umd_command_list_flags_valid(
      D12_UMD_DDI_COMMAND_LIST_ENABLE_FULL_PIPELINE_MARKERS, false));
   assert(d12_umd_command_list_flags_valid(
      D12_UMD_DDI_COMMAND_LIST_ENABLE_MARKERS |
         D12_UMD_DDI_COMMAND_LIST_ENABLE_FULL_PIPELINE_MARKERS,
      true));
   assert(!d12_umd_command_list_flags_valid(UINT32_C(0x4), true));

   test_command_signatures();

   puts("d12_umd_command_core_tests: PASS");
   return 0;
}
