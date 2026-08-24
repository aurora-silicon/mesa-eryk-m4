/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#define D12_CORE_BLEND_ALPHA_FACTOR UINT32_C(20)
#define D12_CORE_BLEND_INV_ALPHA_FACTOR UINT32_C(21)
#define D12_CORE_DEFAULT_BLEND_FACTOR 1.0f

enum d12_alpha_blend_factor_kind {
   D12_ALPHA_BLEND_FACTOR_NONE,
   D12_ALPHA_BLEND_FACTOR_CONSTANT,
   D12_ALPHA_BLEND_FACTOR_INVERTED,
};

static inline enum d12_alpha_blend_factor_kind
d12_alpha_blend_factor_classify(uint32_t factor)
{
   switch (factor) {
   case D12_CORE_BLEND_ALPHA_FACTOR:
      return D12_ALPHA_BLEND_FACTOR_CONSTANT;
   case D12_CORE_BLEND_INV_ALPHA_FACTOR:
      return D12_ALPHA_BLEND_FACTOR_INVERTED;
   default:
      return D12_ALPHA_BLEND_FACTOR_NONE;
   }
}
