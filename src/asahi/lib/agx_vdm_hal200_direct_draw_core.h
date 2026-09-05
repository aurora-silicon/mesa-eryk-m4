/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_VDM_HAL200_DIRECT_DRAW_LENGTH 20u
#define AGX_VDM_HAL200_DIRECT_DRAW_OPCODE UINT32_C(0x61c4)

struct agx_vdm_hal200_direct_draw_fields {
   uint32_t draw_state_context_offset;
   uint8_t primitive;
   uint32_t vertex_count;
   uint32_t instance_count;
   uint32_t vertex_start;
};

struct agx_vdm_hal200_direct_draw {
   uint8_t bytes[AGX_VDM_HAL200_DIRECT_DRAW_LENGTH];
   uint8_t length;
};

/* Pack the context-relative PPP pointer plus the direct, non-indexed draw.
 * Stream Terminate is a separate four-byte record emitted only when the
 * complete control stream is closed, not after each draw.
 */
bool agx_vdm_hal200_direct_draw_pack(
   enum agx_chip chip, const struct agx_vdm_hal200_direct_draw_fields *fields,
   struct agx_vdm_hal200_direct_draw *out);

bool agx_vdm_hal200_direct_draw_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_hal200_direct_draw_fields *fields);

#ifdef __cplusplus
}
#endif
