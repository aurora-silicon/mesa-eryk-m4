/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral DXGI presentation-format policy.  This is deliberately
 * independent of DirectX, WDK, COM and Mesa types so DXGI validation, format
 * caps and the native blit recorder consume one host-tested table.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum d12_present_core_format {
   D12_PRESENT_CORE_FORMAT_R16G16B16A16_FLOAT = 10,
   D12_PRESENT_CORE_FORMAT_R10G10B10A2_UNORM = 24,
   D12_PRESENT_CORE_FORMAT_R8G8B8A8_UNORM = 28,
   D12_PRESENT_CORE_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
   D12_PRESENT_CORE_FORMAT_B5G6R5_UNORM = 85,
   D12_PRESENT_CORE_FORMAT_B5G5R5A1_UNORM = 86,
   D12_PRESENT_CORE_FORMAT_B8G8R8A8_UNORM = 87,
   D12_PRESENT_CORE_FORMAT_B8G8R8X8_UNORM = 88,
   D12_PRESENT_CORE_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
};

struct d12_present_format_info {
   uint32_t shader_format;
   uint32_t pixel_size_B;
   bool srgb;
};

bool d12_present_format_core_get(uint32_t format,
                                 struct d12_present_format_info *info);
bool d12_present_format_core_supported(uint32_t format);
