/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "agx_chip.h"

#define AGX_PPP_HAL200_SCISSOR_RECORD_LENGTH 16u
#define AGX_PPP_HAL200_DEPTH_BIAS_RECORD_LENGTH 12u
#define AGX_PPP_HAL200_FRAGMENT_CONTROL_BASE UINT32_C(0x00040200)
#define AGX_PPP_HAL200_FRAGMENT_CONTROL_SCISSOR UINT32_C(0x00010000)
#define AGX_PPP_HAL200_FRAGMENT_CONTROL_DEPTH_BIAS UINT32_C(0x00020000)

struct agx_ppp_hal200_raster_fields {
   bool scissor_enable;
   bool depth_bias_enable;
   uint16_t max_x;
   uint16_t min_x;
   uint16_t max_y;
   uint16_t min_y;
   uint32_t min_z_raw;
   uint32_t max_z_raw;
   uint32_t depth_bias_raw;
   uint32_t slope_scale_raw;
   uint32_t clamp_raw;
};

struct agx_ppp_hal200_raster_state {
   uint32_t fragment_control;
   uint32_t draw_state_scissor_flag;
   uint8_t scissor[AGX_PPP_HAL200_SCISSOR_RECORD_LENGTH];
   uint8_t depth_bias[AGX_PPP_HAL200_DEPTH_BIAS_RECORD_LENGTH];
};

bool agx_ppp_hal200_raster_state_build(
   enum agx_chip chip, const struct agx_ppp_hal200_raster_fields *fields,
   struct agx_ppp_hal200_raster_state *out);
