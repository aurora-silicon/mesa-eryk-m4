/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_viewport_math.h"

#include <string.h>

static float
d12_clamp_float(float value, float minimum, float maximum)
{
   return value < minimum ? minimum : value > maximum ? maximum : value;
}

static uint32_t
d12_nonnegative_coordinate(int32_t value)
{
   return value > 0 ? (uint32_t)value : 0;
}

bool
d12_viewport_render_extent(
   uint32_t viewport_count, const struct d12_viewport_desc *viewports,
   uint32_t *out_width, uint32_t *out_height)
{
   float right = 0.0f;
   float bottom = 0.0f;

   if (!out_width || !out_height || viewport_count > D12_VIEWPORT_STATE_MAX ||
       (viewport_count && !viewports))
      return false;

   for (uint32_t i = 0; i < viewport_count; ++i) {
      if (!d12_viewport_desc_valid(&viewports[i]))
         return false;
      right = fmaxf(right, viewports[i].x + viewports[i].width);
      bottom = fmaxf(bottom, viewports[i].y + viewports[i].height);
   }

   *out_width = viewport_count ? (uint32_t)fmaxf(ceilf(right), 1.0f) : 1;
   *out_height = viewport_count ? (uint32_t)fmaxf(ceilf(bottom), 1.0f) : 1;
   return *out_width <= UINT16_MAX && *out_height <= UINT16_MAX;
}

bool
d12_viewport_plan_storage(
   bool has_draws, uint32_t scissor_capacity, uint32_t scissor_record_bytes,
   uint32_t depth_bias_capacity, uint32_t depth_bias_record_bytes,
   struct d12_viewport_storage_plan *out)
{
   if (!out)
      return false;

   *out = (struct d12_viewport_storage_plan){0};
   if (!has_draws)
      return true;

   out->scissor_bytes =
      (uint64_t)scissor_capacity * scissor_record_bytes;
   out->depth_bias_bytes =
      (uint64_t)depth_bias_capacity * depth_bias_record_bytes;
   return out->scissor_bytes != 0 && out->depth_bias_bytes != 0;
}

uint32_t
d12_viewport_build_hw_state(
   uint32_t viewport_count, const struct d12_viewport_desc *viewports,
   uint32_t scissor_count, const struct d12_scissor_desc *scissors,
   uint32_t target_width, uint32_t target_height,
   struct d12_viewport_hw_state out[D12_VIEWPORT_STATE_MAX])
{
   if (!out || viewport_count > D12_VIEWPORT_STATE_MAX ||
       scissor_count > D12_VIEWPORT_STATE_MAX ||
       (viewport_count && !viewports) || (scissor_count && !scissors) ||
       target_width > UINT16_MAX || target_height > UINT16_MAX)
      return 0;

   memset(out, 0, D12_VIEWPORT_STATE_MAX * sizeof(out[0]));
   const uint32_t hardware_count = viewport_count ? viewport_count : 1;

   for (uint32_t i = 0; i < hardware_count; ++i) {
      const struct d12_viewport_desc viewport =
         viewport_count ? viewports[i]
                        : (struct d12_viewport_desc){0};
      if (viewport_count && !d12_viewport_desc_valid(&viewport))
         return 0;
      const float viewport_x1 = viewport.x + viewport.width;
      const float viewport_y1 = viewport.y + viewport.height;
      const float x0 = viewport.x < viewport_x1 ? viewport.x : viewport_x1;
      const float y0 = viewport.y < viewport_y1 ? viewport.y : viewport_y1;
      const float x1 = viewport.x > viewport_x1 ? viewport.x : viewport_x1;
      const float y1 = viewport.y > viewport_y1 ? viewport.y : viewport_y1;
      uint32_t clip_x0 =
         (uint32_t)d12_clamp_float(x0, 0.0f, (float)target_width);
      uint32_t clip_y0 =
         (uint32_t)d12_clamp_float(y0, 0.0f, (float)target_height);
      uint32_t clip_x1 =
         (uint32_t)d12_clamp_float(x1, 0.0f, (float)target_width);
      uint32_t clip_y1 =
         (uint32_t)d12_clamp_float(y1, 0.0f, (float)target_height);

      if (i < scissor_count) {
         const struct d12_scissor_desc *scissor = &scissors[i];
         const uint32_t left = d12_nonnegative_coordinate(scissor->left);
         const uint32_t top = d12_nonnegative_coordinate(scissor->top);
         const uint32_t right = d12_nonnegative_coordinate(scissor->right);
         const uint32_t bottom = d12_nonnegative_coordinate(scissor->bottom);
         clip_x0 = clip_x0 > left ? clip_x0 : left;
         clip_y0 = clip_y0 > top ? clip_y0 : top;
         clip_x1 = clip_x1 < right ? clip_x1 : right;
         clip_y1 = clip_y1 < bottom ? clip_y1 : bottom;
      } else {
         clip_x0 = clip_y0 = clip_x1 = clip_y1 = 0;
      }

      clip_x0 = clip_x0 < target_width ? clip_x0 : target_width;
      clip_y0 = clip_y0 < target_height ? clip_y0 : target_height;
      if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1)
         clip_x0 = clip_y0 = clip_x1 = clip_y1 = 0;

      out[i].clip[0] = (uint16_t)clip_x0;
      out[i].clip[1] = (uint16_t)clip_y0;
      out[i].clip[2] = (uint16_t)clip_x1;
      out[i].clip[3] = (uint16_t)clip_y1;
      out[i].transform[0] = viewport.x + 0.5f * viewport.width;
      out[i].transform[1] = viewport.y + 0.5f * viewport.height;
      out[i].transform[2] = viewport.min_depth;
      out[i].transform[3] = 0.5f * viewport.width;
      out[i].transform[4] = -0.5f * viewport.height;
      out[i].transform[5] = viewport.max_depth - viewport.min_depth;
   }

   return hardware_count;
}
