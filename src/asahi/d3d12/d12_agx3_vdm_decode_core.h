/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for strict AGX3 VDM record decoding.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_vdm_decode_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode one record from a potentially longer AGX3 VDM stream through the
 * shared Mesa classifier. This does not follow links, inspect PPP payloads,
 * emit commands, submit work, admit a device, or expose a D3D12 capability.
 */
bool d12_agx3_decode_vdm_record(enum agx_chip chip, const void *command,
                                uint32_t available,
                                struct agx_vdm_decoded_record *record);

#ifdef __cplusplus
}
#endif
