/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded, target-aware AGX3 direct VDM Tessellate representation.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_VDM_TESSELLATE_DIRECT_LENGTH 36u

struct agx_vdm_tessellate_direct_info {
   uint64_t factor_buffer;
   uint32_t patch_count;
   uint32_t instance_count;
   uint32_t base_patch;
   uint32_t base_instance;

   /*
    * These are the exact values written by Apple's direct-draw emitter after
    * its context-side calculations.  This helper deliberately does not
    * invent byte scaling or reconstruct the surrounding tessellation state.
    */
   uint32_t instance_stride_word;
   uint32_t factor_buffer_size_minus_one;
};

struct agx_vdm_tessellate_direct {
   uint8_t bytes[AGX_VDM_TESSELLATE_DIRECT_LENGTH];
   uint8_t length;
};

/*
 * This covers only the exact nine-dword direct Tessellate record constructed
 * by the pinned G15, HAL200, and HAL300 render plugins.  The factor-buffer VA
 * is 48-bit and split across header[7:0], word 1, and word 8[7:0].  All three
 * inspected encoder families use byte-identical records, so the bytes cannot
 * classify the originating generation.
 *
 * This is a host representation/validation surface.  It does not emit into a
 * live VDM stream or enable tessellation lowering, scheduling, submission,
 * device admission, or an API capability.
 */
bool agx_vdm_tessellate_direct_target_supported(enum agx_chip chip);

bool agx_vdm_tessellate_direct_pack(
   enum agx_chip chip, const struct agx_vdm_tessellate_direct_info *info,
   struct agx_vdm_tessellate_direct *out);

bool agx_vdm_tessellate_direct_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_tessellate_direct_info *info);

#ifdef __cplusplus
}
#endif
