/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Frontend-neutral CopyTextureRegion coordinate planning.  The Release 8
 * DDI keeps the original 0003 command ABI when the runtime enables texture
 * copies between resource dimensions, so both the COM and WDDM frontends
 * must reach the same dimension-independent bounds and block calculations.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct d12_texture_copy_surface_geometry {
   uint32_t width_px;
   uint32_t height_px;
   uint32_t depth_px;
   uint32_t width_el;
   uint32_t height_el;
   uint32_t block_width_px;
   uint32_t block_height_px;
};

struct d12_texture_copy_region_geometry {
   uint32_t destination_x_px;
   uint32_t destination_y_px;
   uint32_t destination_z_px;
   bool has_source_box;
   uint32_t source_left_px;
   uint32_t source_top_px;
   uint32_t source_front_px;
   uint32_t source_right_px;
   uint32_t source_bottom_px;
   uint32_t source_back_px;
};

struct d12_texture_copy_plan {
   uint32_t source_x_el;
   uint32_t source_y_el;
   uint32_t source_front_px;
   uint32_t destination_x_el;
   uint32_t destination_y_el;
   uint32_t destination_z_px;
   uint32_t width_el;
   uint32_t height_el;
   uint32_t depth_px;
};

enum d12_texture_copy_plan_status {
   D12_TEXTURE_COPY_PLAN_OK,
   D12_TEXTURE_COPY_PLAN_EMPTY,
   D12_TEXTURE_COPY_PLAN_INVALID_GEOMETRY,
   D12_TEXTURE_COPY_PLAN_SOURCE_X_UNALIGNED,
   D12_TEXTURE_COPY_PLAN_SOURCE_Y_UNALIGNED,
   D12_TEXTURE_COPY_PLAN_DESTINATION_X_UNALIGNED,
   D12_TEXTURE_COPY_PLAN_DESTINATION_Y_UNALIGNED,
   D12_TEXTURE_COPY_PLAN_OUT_OF_BOUNDS,
};

enum d12_texture_copy_plan_status d12_texture_copy_plan_region(
   const struct d12_texture_copy_surface_geometry *destination,
   const struct d12_texture_copy_surface_geometry *source,
   const struct d12_texture_copy_region_geometry *region,
   struct d12_texture_copy_plan *plan);
