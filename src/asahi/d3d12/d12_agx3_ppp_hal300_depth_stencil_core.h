/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded HAL300 PPP depth/stencil block.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_PPP_HAL300_DEPTH_STENCIL_LENGTH 24u

struct d12_agx3_ppp_hal300_depth_stencil_fields {
   uint32_t fragment_control_raw;
   uint32_t fragment_front_face_raw;
   uint32_t fragment_back_face_raw;
   uint32_t fragment_front_stencil_raw;
   uint32_t fragment_back_stencil_raw;
};

struct d12_agx3_ppp_hal300_depth_stencil {
   uint8_t bytes[D12_AGX3_PPP_HAL300_DEPTH_STENCIL_LENGTH];
   uint8_t length;
};

/*
 * Research/validation bridge for exactly one HAL300 header-bit-0 block.  It
 * performs no D3D12 state lowering, PPP allocation, VDM emission, submission,
 * device admission, or feature advertisement.
 */
bool d12_agx3_encode_ppp_hal300_depth_stencil(
   enum agx_chip chip,
   const struct d12_agx3_ppp_hal300_depth_stencil_fields *fields,
   struct d12_agx3_ppp_hal300_depth_stencil *out);

bool d12_agx3_decode_ppp_hal300_depth_stencil(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_hal300_depth_stencil_fields *fields);

#ifdef __cplusplus
}
#endif
