/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded AGX3 VDM Stream Terminate representation.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_VDM_STREAM_TERMINATE_LENGTH 4u

struct d12_agx3_vdm_stream_terminate {
   uint8_t bytes[D12_AGX3_VDM_STREAM_TERMINATE_LENGTH];
   uint8_t length;
};

/*
 * Research/validation bridge for only the exact four-byte AGX3 VDM Stream
 * Terminate.  Type-7 Stream Return, command allocation, live emission,
 * submission, device admission, and D3D12 capabilities remain closed.
 */
bool
d12_agx3_encode_vdm_stream_terminate(enum agx_chip chip,
                                     struct d12_agx3_vdm_stream_terminate *out);

bool d12_agx3_decode_vdm_stream_terminate(enum agx_chip chip, const void *bytes,
                                          uint32_t size);

#ifdef __cplusplus
}
#endif
