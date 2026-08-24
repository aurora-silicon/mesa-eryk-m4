/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Generation-indexed native texture/PBE heap geometry.  This header is kept
 * independent of Mesa and Windows headers so the byte arithmetic can be
 * exhaustively host-tested.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D12_NATIVE_DESCRIPTOR_GEN7  7u
#define D12_NATIVE_DESCRIPTOR_GEN8  8u
#define D12_NATIVE_DESCRIPTOR_GEN9  9u
#define D12_NATIVE_DESCRIPTOR_GEN10 10u

struct d12_native_descriptor_layout {
   uint8_t texture_stride_B;
   uint8_t pbe_stride_B;
};

/* Layout recognition is not a device-support gate. In particular, Gen9/10
 * command streams and ISA emission remain separately fail-closed. */
static inline bool
d12_native_descriptor_layout(uint32_t generation,
                             struct d12_native_descriptor_layout *out)
{
   if (!out)
      return false;

   switch (generation) {
   case D12_NATIVE_DESCRIPTOR_GEN7:
   case D12_NATIVE_DESCRIPTOR_GEN8:
      *out = (struct d12_native_descriptor_layout){24, 24};
      return true;
   case D12_NATIVE_DESCRIPTOR_GEN9:
   case D12_NATIVE_DESCRIPTOR_GEN10:
      *out = (struct d12_native_descriptor_layout){32, 32};
      return true;
   default:
      *out = (struct d12_native_descriptor_layout){0, 0};
      return false;
   }
}

static inline bool
d12_native_descriptor_span(uint8_t stride_B, size_t count,
                           size_t extra_elements, size_t *size_B)
{
   if (!size_B || !stride_B || count > SIZE_MAX - extra_elements)
      return false;

   count += extra_elements;
   if (count > SIZE_MAX / stride_B)
      return false;

   *size_B = count * stride_B;
   return true;
}

static inline bool
d12_native_descriptor_offset(uint8_t stride_B, size_t index,
                             size_t *offset_B)
{
   return d12_native_descriptor_span(stride_B, index, 0, offset_B);
}

/* Gen9/10 relocated nonlinear PBE mip fields are read only when the
 * generation's mipmapped gate is asserted.  Linear descriptors do not carry
 * Level/Levels in this location. */
static inline bool
d12_native_pbe_has_levels(bool linear, uint32_t level, uint32_t levels)
{
   return !linear && (level != 0 || levels > 1);
}
