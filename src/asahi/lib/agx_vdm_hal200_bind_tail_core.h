/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded G17P VertexProgramVariant bind-tail representation.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_VDM_HAL200_BIND_COUNT 8u
#define AGX_VDM_HAL200_BIND_TAIL_LENGTH 68u
#define AGX_VDM_HAL200_BIND_HEADER_CONTROL UINT32_C(0x00000500)

struct agx_vdm_hal200_bind_pair {
   uint32_t context_offset;
   uint32_t control;
};

struct agx_vdm_hal200_bind_tail {
   uint8_t bytes[AGX_VDM_HAL200_BIND_TAIL_LENGTH];
   uint8_t length;
};

/*
 * Append this 68-byte tail immediately after the 28-byte G17P
 * VertexProgramVariant prefix.  The result is the native 0x60-byte encoder
 * prefix whose draw record begins at byte 0x60:
 *
 *   u32 header control = 0x500
 *   8 x { u32 render-context-relative address, u32 opaque slot control }
 *
 * Controls encode the word count of the following PPP payload. The leading
 * control describes the first address; each pair's control describes the
 * next address, and the last describes the draw-index payload after this
 * tail. In particular pairs[4].control must reflect viewport payload size.
 */
bool agx_vdm_hal200_bind_tail_pack(
   enum agx_chip chip,
   const struct agx_vdm_hal200_bind_pair pairs[AGX_VDM_HAL200_BIND_COUNT],
   struct agx_vdm_hal200_bind_tail *out);

bool agx_vdm_hal200_bind_tail_unpack(
   enum agx_chip chip, const void *bytes, size_t size,
   struct agx_vdm_hal200_bind_pair pairs[AGX_VDM_HAL200_BIND_COUNT]);

#ifdef __cplusplus
}
#endif

uint32_t agx_vdm_hal200_bind_viewport_control(size_t viewport_bytes);
