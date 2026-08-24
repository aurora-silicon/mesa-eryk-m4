/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded AGX3 VDM Stream Link representation.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_VDM_STREAM_LINK_LENGTH 8u

struct d12_agx3_vdm_stream_link {
   uint8_t bytes[D12_AGX3_VDM_STREAM_LINK_LENGTH];
   uint8_t length;
};

/*
 * Research/validation bridge for only the exact direct, non-returning AGX3
 * VDM Stream Link.  It deliberately does not model calls, returns, command
 * allocation, draw lowering, submission, AGX3 device admission, or D3D12
 * capabilities.
 */
bool d12_agx3_encode_vdm_stream_link(enum agx_chip chip, uint64_t target,
                                     struct d12_agx3_vdm_stream_link *out);

bool d12_agx3_decode_vdm_stream_link(enum agx_chip chip, const void *bytes,
                                     uint32_t size, uint64_t *target);

#ifdef __cplusplus
}
#endif
