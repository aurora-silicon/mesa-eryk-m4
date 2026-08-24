/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded AGX3 VDM PPP State Update representation.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_VDM_PPP_STATE_LENGTH 8u

struct d12_agx3_vdm_ppp_state {
   uint8_t bytes[D12_AGX3_VDM_PPP_STATE_LENGTH];
   uint8_t length;
};

/*
 * Research/validation bridge for only the exact AGX3 VDM PPP State Update
 * pointer/size record.  It deliberately does not inspect PPP payloads or
 * model command allocation, draw lowering, submission, AGX3 device admission,
 * or D3D12 capabilities.
 */
bool d12_agx3_encode_vdm_ppp_state(enum agx_chip chip, uint64_t pointer,
                                   uint32_t size_words,
                                   struct d12_agx3_vdm_ppp_state *out);

bool d12_agx3_decode_vdm_ppp_state(enum agx_chip chip, const void *bytes,
                                   uint32_t size, uint64_t *pointer,
                                   uint32_t *size_words);

#ifdef __cplusplus
}
#endif
