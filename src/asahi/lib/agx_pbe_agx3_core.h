/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Exact bounded Gen9/Gen10 PBE descriptor codec.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_PBE_AGX3_DESCRIPTOR_LENGTH 32u
#define AGX_PBE_AGX3_GENERATION_9      9u
#define AGX_PBE_AGX3_GENERATION_10     10u

/* This is intentionally the subset whose complete byte geometry is recovered:
 * uncompressed, non-sparse, single-level, single-layer 2D images in ordinary
 * twiddled/GPU memory. The inherited page-aligned-layer flag is included even
 * for a single layer because pre-HAL300 writable layouts require it. Fields
 * with unresolved Gen9/10 semantics are not
 * represented and are required to be zero by the decoder.
 */
struct agx_pbe_agx3_2d_fields {
   uint8_t layout;
   uint8_t channels;
   uint8_t type;
   uint8_t swizzle_r;
   uint8_t swizzle_g;
   uint8_t swizzle_b;
   uint8_t swizzle_a;
   uint16_t width;
   uint16_t height;
   uint64_t buffer;
   bool page_aligned_layers;
};

struct agx_pbe_agx3_descriptor {
   uint8_t bytes[AGX_PBE_AGX3_DESCRIPTOR_LENGTH];
   uint8_t length;
};

bool agx_pbe_agx3_2d_pack(
   uint32_t generation,
   const struct agx_pbe_agx3_2d_fields *fields,
   struct agx_pbe_agx3_descriptor *out);

bool agx_pbe_agx3_2d_unpack(
   uint32_t generation, const void *bytes, size_t size,
   struct agx_pbe_agx3_2d_fields *fields);

#ifdef __cplusplus
}
#endif
