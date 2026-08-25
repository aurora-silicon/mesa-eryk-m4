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

enum d12_agx3_vdm_state_update_template_variant {
   D12_AGX3_VDM_STATE_UPDATE_TEMPLATE_00 = 0,
   D12_AGX3_VDM_STATE_UPDATE_TEMPLATE_01 = 1,
   D12_AGX3_VDM_STATE_UPDATE_TEMPLATE_10 = 2,
   D12_AGX3_VDM_STATE_UPDATE_TEMPLATE_11 = 3,
};

/* Structural compiler/configuration inputs only.  This mirrors the bounded
 * shared template materializer without claiming that the existing AGX2 D3D
 * counts, UVS, or vertex-unknown state has the same encoding. */
struct d12_agx3_vdm_state_update_template_info {
   uint64_t pipeline;
   uint16_t raw_state_load_count_12;
   uint32_t raw_state_load_count_18;
   uint8_t raw_vs_output_count_0;
   uint8_t raw_vs_output_count_2;
   bool raw_vs_output_count_1_extra;
   uint8_t raw_vs_output_selector;
   bool raw_vs_output_flag_6;
   bool pre_hal300_flag_group_nonzero;
   bool pre_hal300_flag_bit_4;
   enum d12_agx3_vdm_state_update_template_variant variant;
   /* Exact raw RenderContext input.  UINT32_MAX emits zero; all other values
    * emit `(raw_active_context_selector - 1) & 7`. */
   uint32_t raw_active_context_selector;
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

bool d12_agx3_encode_vdm_state_update_template(
   enum agx_chip chip,
   const struct d12_agx3_vdm_state_update_template_info *info,
   struct d12_agx3_vdm_state_update *out);

bool d12_agx3_decode_vdm_state_update(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_vdm_state_update_info *info);

#ifdef __cplusplus
}
#endif
