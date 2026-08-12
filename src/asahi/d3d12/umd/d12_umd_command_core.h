/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Stable numeric values from the pinned WDK 26100.6584 DDI 0003 header.
 * The Windows-facing translation unit asserts that these still match the WDK.
 * Keeping the mapping free of Windows headers makes its full matrix host-testable.
 */
#define D12_UMD_DDI_COMMAND_LIST_DIRECT UINT32_C(0)
#define D12_UMD_DDI_COMMAND_LIST_BUNDLE UINT32_C(1)
#define D12_UMD_DDI_QUEUE_3D UINT32_C(0x1)
#define D12_UMD_DDI_QUEUE_COMPUTE UINT32_C(0x2)
#define D12_UMD_DDI_QUEUE_COPY UINT32_C(0x4)
#define D12_UMD_DDI_QUEUE_SUPPORTED_MASK                                  \
   (D12_UMD_DDI_QUEUE_3D | D12_UMD_DDI_QUEUE_COMPUTE |                   \
    D12_UMD_DDI_QUEUE_COPY)
#define D12_UMD_DDI_COMMAND_LIST_ENABLE_MARKERS UINT32_C(0x1)
#define D12_UMD_DDI_COMMAND_LIST_ENABLE_FULL_PIPELINE_MARKERS UINT32_C(0x2)

enum d12_umd_renderer_command_type {
   D12_UMD_RENDERER_COMMAND_DIRECT,
   D12_UMD_RENDERER_COMMAND_BUNDLE,
   D12_UMD_RENDERER_COMMAND_COMPUTE,
   D12_UMD_RENDERER_COMMAND_COPY,
};

bool d12_umd_command_type_from_ddi(
   uint32_t list_type, uint32_t queue_flags,
   enum d12_umd_renderer_command_type *renderer_type);

bool d12_umd_command_list_flags_valid(uint32_t flags, bool release1);
