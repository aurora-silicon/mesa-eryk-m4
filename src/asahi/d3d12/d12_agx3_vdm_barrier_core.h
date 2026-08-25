/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded AGX3 VDM memory-barrier representation.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_VDM_BARRIER_LENGTH 4u

struct d12_agx3_vdm_barrier {
   uint8_t bytes[D12_AGX3_VDM_BARRIER_LENGTH];
   uint8_t length;
};

/*
 * Bridge for only the canonical four-byte AGX3 VDM memory barrier.  D3D12's
 * render-stream prologue emits this exact form.  The ungrounded aggregate
 * G15X multiple-AGC form remains closed, as do AGX3 device admission and
 * D3D12 capability advertisement.
 */
bool d12_agx3_encode_vdm_barrier(enum agx_chip chip,
                                 struct d12_agx3_vdm_barrier *out);

bool d12_agx3_decode_vdm_barrier(enum agx_chip chip, const void *bytes,
                                 uint32_t size);

#ifdef __cplusplus
}
#endif
