/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_present_format_core.h"

#include "../libagx/present_blt.h"

#include <stddef.h>

#define D12_PRESENT_INFO(shader, size, gamma)                              \
   ((struct d12_present_format_info){                                      \
      .shader_format = (shader),                                           \
      .pixel_size_B = (size),                                              \
      .srgb = (gamma),                                                     \
   })

bool
d12_present_format_core_get(uint32_t format,
                            struct d12_present_format_info *info)
{
   struct d12_present_format_info result;

   switch (format) {
   case D12_PRESENT_CORE_FORMAT_R16G16B16A16_FLOAT:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_RGBA16_FLOAT, 8,
                                false);
      break;
   case D12_PRESENT_CORE_FORMAT_R10G10B10A2_UNORM:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_RGB10A2, 4, false);
      break;
   case D12_PRESENT_CORE_FORMAT_R8G8B8A8_UNORM:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_RGBA8, 4, false);
      break;
   case D12_PRESENT_CORE_FORMAT_R8G8B8A8_UNORM_SRGB:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_RGBA8_SRGB, 4, true);
      break;
   case D12_PRESENT_CORE_FORMAT_B5G6R5_UNORM:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_BGR565, 2, false);
      break;
   case D12_PRESENT_CORE_FORMAT_B5G5R5A1_UNORM:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_BGRA5551, 2, false);
      break;
   case D12_PRESENT_CORE_FORMAT_B8G8R8A8_UNORM:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_BGRA8, 4, false);
      break;
   case D12_PRESENT_CORE_FORMAT_B8G8R8X8_UNORM:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_BGRX8, 4, false);
      break;
   case D12_PRESENT_CORE_FORMAT_B8G8R8A8_UNORM_SRGB:
      result = D12_PRESENT_INFO(LIBAGX_PRESENT_FORMAT_BGRA8_SRGB, 4, true);
      break;
   default:
      return false;
   }

   if (info)
      *info = result;
   return true;
}

bool
d12_present_format_core_supported(uint32_t format)
{
   return d12_present_format_core_get(format, NULL);
}
