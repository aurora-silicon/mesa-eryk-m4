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

   puts("d12_umd_command_core_tests: PASS");
   return 0;
}
