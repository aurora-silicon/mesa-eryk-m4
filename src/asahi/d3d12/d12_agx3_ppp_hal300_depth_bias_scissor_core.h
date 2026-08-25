/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 validation bridge for HAL300 depth-bias/scissor payloads.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_PPP_HAL300_DEPTH_BIAS_RECORD_LENGTH        12u
#define D12_AGX3_PPP_HAL300_SCISSOR_RECORD_LENGTH           16u
#define D12_AGX3_PPP_HAL300_DEPTH_BIAS_SCISSOR_BASES_LENGTH 20u

struct d12_agx3_ppp_hal300_depth_bias_record_fields {
   uint32_t depth_bias_raw;
   uint32_t slope_scale_raw;
   uint32_t clamp_raw;
};

struct d12_agx3_ppp_hal300_scissor_record_fields {
   uint16_t max_x;
   uint16_t min_x;
   uint16_t max_y;
   uint16_t min_y;
   uint32_t min_z_raw;
   uint32_t max_z_raw;
};

struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields {
   uint64_t depth_bias_base;
   uint64_t scissor_base;
};

struct d12_agx3_ppp_hal300_depth_bias_record {
   uint8_t bytes[D12_AGX3_PPP_HAL300_DEPTH_BIAS_RECORD_LENGTH];
   uint8_t length;
};

struct d12_agx3_ppp_hal300_scissor_record {
   uint8_t bytes[D12_AGX3_PPP_HAL300_SCISSOR_RECORD_LENGTH];
   uint8_t length;
};

struct d12_agx3_ppp_hal300_depth_bias_scissor_bases {
   uint8_t bytes[D12_AGX3_PPP_HAL300_DEPTH_BIAS_SCISSOR_BASES_LENGTH];
   uint8_t length;
};

/* Strict research/validation bridges; no command recording or admission. */
bool d12_agx3_encode_ppp_hal300_depth_bias_record(
   enum agx_chip chip,
   const struct d12_agx3_ppp_hal300_depth_bias_record_fields *fields,
   struct d12_agx3_ppp_hal300_depth_bias_record *out);

bool d12_agx3_decode_ppp_hal300_depth_bias_record(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_hal300_depth_bias_record_fields *fields);

bool d12_agx3_encode_ppp_hal300_scissor_record(
   enum agx_chip chip,
   const struct d12_agx3_ppp_hal300_scissor_record_fields *fields,
   struct d12_agx3_ppp_hal300_scissor_record *out);

bool d12_agx3_decode_ppp_hal300_scissor_record(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_hal300_scissor_record_fields *fields);

bool d12_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
   enum agx_chip chip,
   const struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields *fields,
   struct d12_agx3_ppp_hal300_depth_bias_scissor_bases *out);

bool d12_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
   enum agx_chip chip, const void *bytes, uint32_t size,
   struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields *fields);

#ifdef __cplusplus
}
#endif
