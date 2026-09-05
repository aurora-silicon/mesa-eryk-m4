/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct hk_g17p_full_frame_scissor {
   uint32_t width, height;
   int32_t area_x, area_y, scissor_x, scissor_y;
   uint32_t area_width, area_height, scissor_width, scissor_height;
   float viewport_x, viewport_y, viewport_width, viewport_height;
   float min_depth, max_depth;
   uint32_t viewport_count, scissor_count;
   bool single_color_layer_sample;
   bool default_depth;
};

/* Native ClipRectContext (2bf958..968, 2bf9b0..b8) defaults to the full
 * attachment and Z=[0,1]. updateClipRegions (2c06c8..708) compares effective
 * bounds with that screen; canUseScissorConfigReg (2c0950..9dc) additionally
 * requires matching Z. handleScissorUpdates (477684..69c) then clears only
 * PRIMARY bit16. Restrict Linux to exact original full-frame inputs: checking
 * only the integer bounds after viewport truncation would admit fractions.
 * The caller retains all array records and indices, even when this is true.
 */
static inline bool
hk_g17p_full_frame_scissor_can_elide(
   const struct hk_g17p_full_frame_scissor *s)
{
   return s && s->single_color_layer_sample && s->default_depth &&
          s->width && s->height && s->width <= 16384 && s->height <= 16384 &&
          s->viewport_count == 1 && s->scissor_count == 1 &&
          s->area_x == 0 && s->area_y == 0 &&
          s->area_width == s->width && s->area_height == s->height &&
          s->scissor_x == 0 && s->scissor_y == 0 &&
          s->scissor_width == s->width && s->scissor_height == s->height &&
          s->viewport_x == 0.0f && s->viewport_y == 0.0f &&
          s->viewport_width == (float)s->width &&
          s->viewport_height == (float)s->height &&
          s->min_depth == 0.0f && s->max_depth == 1.0f;
}
