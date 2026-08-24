/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for the bounded AGX3 VDM State Update representation.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_VDM_STATE_UPDATE_LENGTH 28u

struct d12_agx3_vdm_state_update_info {
   uint32_t raw_vertex_shader_word0;
   uint64_t pipeline;
   uint32_t raw_pipeline_template;
   uint32_t raw_vertex_outputs[2];
   uint32_t raw_vertex_unknown;
};

struct d12_agx3_vdm_state_update {
   uint8_t bytes[D12_AGX3_VDM_STATE_UPDATE_LENGTH];
   uint8_t length;
};

/*
 * Research/validation bridge only.  Raw payload words remain unnamed beyond
 * their proven record positions.  These functions are not wired to D3D draw
 * lowering or command submission and do not change AGX3 device admission or
 * D3D12 capabilities.
 */
bool d12_agx3_encode_vdm_state_update(
   enum agx_chip chip, const struct d12_agx3_vdm_state_update_info *info,
   struct d12_agx3_vdm_state_update *out);

bool d12_agx3_decode_vdm_state_update(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_vdm_state_update_info *info);

#ifdef __cplusplus
}
#endif
