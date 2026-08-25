/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp validation bridge for HAL300 depth-bias/scissor payloads.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "asahi/lib/agx_ppp_hal300_depth_bias_scissor_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Strict research/validation bridges; no command recording or admission. */
bool hk_agx3_encode_ppp_hal300_depth_bias_record(
   enum agx_chip chip,
   const struct agx_ppp_hal300_depth_bias_record_fields *fields,
   struct agx_ppp_hal300_depth_bias_record *out);

bool hk_agx3_decode_ppp_hal300_depth_bias_record(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_hal300_depth_bias_record_fields *fields);

bool hk_agx3_encode_ppp_hal300_scissor_record(
   enum agx_chip chip,
   const struct agx_ppp_hal300_scissor_record_fields *fields,
   struct agx_ppp_hal300_scissor_record *out);

bool hk_agx3_decode_ppp_hal300_scissor_record(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_hal300_scissor_record_fields *fields);

bool hk_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
   enum agx_chip chip,
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields *fields,
   struct agx_ppp_hal300_depth_bias_scissor_bases *out);

bool hk_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_hal300_depth_bias_scissor_bases_fields *fields);

#ifdef __cplusplus
}
#endif
