/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded AGX3 viewport-only PPP payload.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_PPP_VIEWPORT_MAX_VIEWPORTS 16u
#define D12_AGX3_PPP_VIEWPORT_MAX_LENGTH    520u

struct d12_agx3_ppp_viewport_transform_raw {
   uint32_t translate_x;
   uint32_t scale_x;
   uint32_t translate_y;
   uint32_t scale_y;
   uint32_t translate_z;
   uint32_t scale_z;
};

struct d12_agx3_ppp_viewport_fields {
   uint8_t viewport_count;
   uint64_t region_clip_raw[D12_AGX3_PPP_VIEWPORT_MAX_VIEWPORTS];
   uint32_t viewport_control_raw;
   struct d12_agx3_ppp_viewport_transform_raw
      viewports[D12_AGX3_PPP_VIEWPORT_MAX_VIEWPORTS];
};

struct d12_agx3_ppp_viewport_payload {
   uint8_t bytes[D12_AGX3_PPP_VIEWPORT_MAX_LENGTH];
   uint16_t length;
};

/*
 * Research/validation bridge for only the exact viewport-only PPP payload.
 * Clip/control/transform words remain lossless raw values.  This does not
 * allocate PPP memory, emit the VDM pointer, lower a D3D12 viewport, submit a
 * command, admit an AGX3 device, or advertise a capability.
 */
bool d12_agx3_encode_ppp_viewport_payload(
   enum agx_chip chip, const struct d12_agx3_ppp_viewport_fields *fields,
   struct d12_agx3_ppp_viewport_payload *out);

bool d12_agx3_decode_ppp_viewport_payload(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_viewport_fields *fields);

#ifdef __cplusplus
}
#endif
