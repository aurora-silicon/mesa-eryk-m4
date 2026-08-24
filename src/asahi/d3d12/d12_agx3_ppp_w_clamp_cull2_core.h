/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded AGX3 PPP W-clamp/Cull-2 payload.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_PPP_W_CLAMP_CULL2_LENGTH 12u

struct d12_agx3_ppp_w_clamp_cull2_fields {
   uint32_t w_clamp_raw;
   uint32_t cull2_raw;
};

struct d12_agx3_ppp_w_clamp_cull2 {
   uint8_t bytes[D12_AGX3_PPP_W_CLAMP_CULL2_LENGTH];
   uint8_t length;
};

/*
 * Research/validation bridge for only the exact three-word W-clamp/Cull-2
 * PPP payload.  Both payload words remain lossless raw values.  This does not
 * allocate PPP memory, emit the surrounding VDM pointer, lower D3D12 state,
 * submit a command, admit an AGX3 device, or advertise a capability.
 */
bool d12_agx3_encode_ppp_w_clamp_cull2(
   enum agx_chip chip,
   const struct d12_agx3_ppp_w_clamp_cull2_fields *fields,
   struct d12_agx3_ppp_w_clamp_cull2 *out);

bool d12_agx3_decode_ppp_w_clamp_cull2(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_w_clamp_cull2_fields *fields);

#ifdef __cplusplus
}
#endif
