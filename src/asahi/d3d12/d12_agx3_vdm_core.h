/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 bridge for bounded AGX3 VDM-state representations.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_VDM_VERTEX_PIPELINE_PAIR_LENGTH 8u

struct d12_agx3_vdm_vertex_pipeline_pair {
   uint8_t bytes[D12_AGX3_VDM_VERTEX_PIPELINE_PAIR_LENGTH];
   uint8_t length;
};

/*
 * Encode/decode only the exact widened vertex-pipeline pair embedded in an
 * AGX3 VDM State update.  `raw_template` is an opaque 22-bit payload; this
 * bridge does not assign semantics to it or construct the surrounding state.
 *
 * This is a research/validation surface only.  It is deliberately not wired
 * to D3D draw lowering or command submission and does not enable AGX3 device
 * admission or any D3D12 feature level/capability.
 */
bool d12_agx3_vdm_encode_vertex_pipeline_pair(
   enum agx_chip chip, uint64_t pipeline, uint32_t raw_template,
   struct d12_agx3_vdm_vertex_pipeline_pair *out);

bool d12_agx3_vdm_decode_vertex_pipeline_pair(
   enum agx_chip chip, const void *bytes, uint32_t size, uint64_t *pipeline,
   uint32_t *raw_template);

#ifdef __cplusplus
}
#endif
