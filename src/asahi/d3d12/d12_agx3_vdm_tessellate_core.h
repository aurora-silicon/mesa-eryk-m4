/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded AGX3 direct VDM Tessellate record.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_vdm_tessellate_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_VDM_TESSELLATE_DIRECT_LENGTH                                \
   AGX_VDM_TESSELLATE_DIRECT_LENGTH

struct d12_agx3_vdm_tessellate_direct {
   uint8_t bytes[D12_AGX3_VDM_TESSELLATE_DIRECT_LENGTH];
   uint8_t length;
};

/*
 * Thin D3D bridge to the shared, target-aware record codec.  This is a host
 * research/validation surface only; it does not lower D3D tessellation,
 * schedule or submit commands, admit an AGX3 device, or expose a capability.
 */
bool d12_agx3_vdm_tessellate_direct_encode(
   enum agx_chip chip, const struct agx_vdm_tessellate_direct_info *info,
   struct d12_agx3_vdm_tessellate_direct *out);

bool d12_agx3_vdm_tessellate_direct_decode(
   enum agx_chip chip, const void *command, uint32_t size,
   struct agx_vdm_tessellate_direct_info *info);

#ifdef __cplusplus
}
#endif
