/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define D12_VIEWPORT_STATE_MAX 16u
#define D12_VIEWPORT_BOUNDS_MIN (-32768.0f)
#define D12_VIEWPORT_BOUNDS_MAX 32767.0f

struct d12_viewport_desc {
   float x;
   float y;
   float width;
   float height;
   float min_depth;
   float max_depth;
};

struct d12_scissor_desc {
   int32_t left;
   int32_t top;
   int32_t right;
   int32_t bottom;
};

struct d12_viewport_hw_state {
   uint16_t clip[4];
   float transform[6];
};

struct d12_viewport_storage_plan {
   uint64_t scissor_bytes;
   uint64_t depth_bias_bytes;
};

static inline bool
d12_viewport_desc_valid(const struct d12_viewport_desc *viewport)
{
   if (!viewport || !isfinite(viewport->x) || !isfinite(viewport->y) ||
       !isfinite(viewport->width) || !isfinite(viewport->height) ||
       !isfinite(viewport->min_depth) || !isfinite(viewport->max_depth))
      return false;

   return viewport->x >= D12_VIEWPORT_BOUNDS_MIN &&
          viewport->y >= D12_VIEWPORT_BOUNDS_MIN &&
          viewport->width >= 0.0f &&
          viewport->height >= 0.0f &&
          viewport->x + viewport->width <= D12_VIEWPORT_BOUNDS_MAX &&
          viewport->y + viewport->height <= D12_VIEWPORT_BOUNDS_MAX &&
          viewport->min_depth >= 0.0f &&
          viewport->max_depth <= 1.0f &&
          viewport->min_depth <= viewport->max_depth;
}

bool d12_viewport_render_extent(
   uint32_t viewport_count, const struct d12_viewport_desc *viewports,
   uint32_t *out_width, uint32_t *out_height);

bool d12_viewport_plan_storage(
   bool has_draws, uint32_t scissor_capacity, uint32_t scissor_record_bytes,
   uint32_t depth_bias_capacity, uint32_t depth_bias_record_bytes,
   struct d12_viewport_storage_plan *out);

uint32_t d12_viewport_build_hw_state(
   uint32_t viewport_count, const struct d12_viewport_desc *viewports,
   uint32_t scissor_count, const struct d12_scissor_desc *scissors,
   uint32_t target_width, uint32_t target_height,
   struct d12_viewport_hw_state out[D12_VIEWPORT_STATE_MAX]);
