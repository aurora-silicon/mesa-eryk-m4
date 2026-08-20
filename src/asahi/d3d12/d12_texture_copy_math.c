/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_texture_copy_math.h"

static bool
d12_texture_copy_surface_geometry_valid(
   const struct d12_texture_copy_surface_geometry *surface)
{
   return surface && surface->width_px && surface->height_px &&
          surface->depth_px && surface->width_el && surface->height_el &&
          surface->block_width_px && surface->block_height_px;
}

enum d12_texture_copy_plan_status
d12_texture_copy_plan_region(
   const struct d12_texture_copy_surface_geometry *destination,
   const struct d12_texture_copy_surface_geometry *source,
   const struct d12_texture_copy_region_geometry *region,
   struct d12_texture_copy_plan *plan)
{
   if (!plan)
      return D12_TEXTURE_COPY_PLAN_INVALID_GEOMETRY;

   if (!d12_texture_copy_surface_geometry_valid(destination) ||
       !d12_texture_copy_surface_geometry_valid(source) || !region)
      return D12_TEXTURE_COPY_PLAN_INVALID_GEOMETRY;

   const uint32_t source_left =
      region->has_source_box ? region->source_left_px : 0;
   const uint32_t source_top =
      region->has_source_box ? region->source_top_px : 0;
   const uint32_t source_front =
      region->has_source_box ? region->source_front_px : 0;
   const uint32_t source_right =
      region->has_source_box ? region->source_right_px : source->width_px;
   const uint32_t source_bottom =
      region->has_source_box ? region->source_bottom_px : source->height_px;
   const uint32_t source_back =
      region->has_source_box ? region->source_back_px : source->depth_px;

   if (source_left >= source_right || source_top >= source_bottom ||
       source_front >= source_back)
      return D12_TEXTURE_COPY_PLAN_EMPTY;

   if (source_left % source->block_width_px)
      return D12_TEXTURE_COPY_PLAN_SOURCE_X_UNALIGNED;
   if (source_top % source->block_height_px)
      return D12_TEXTURE_COPY_PLAN_SOURCE_Y_UNALIGNED;
   if (region->destination_x_px % destination->block_width_px)
      return D12_TEXTURE_COPY_PLAN_DESTINATION_X_UNALIGNED;
   if (region->destination_y_px % destination->block_height_px)
      return D12_TEXTURE_COPY_PLAN_DESTINATION_Y_UNALIGNED;

   const uint32_t source_x_el = source_left / source->block_width_px;
   const uint32_t source_y_el = source_top / source->block_height_px;
   const uint32_t source_right_el =
      source_right / source->block_width_px +
      (source_right % source->block_width_px != 0);
   const uint32_t source_bottom_el =
      source_bottom / source->block_height_px +
      (source_bottom % source->block_height_px != 0);
   const uint32_t destination_x_el =
      region->destination_x_px / destination->block_width_px;
   const uint32_t destination_y_el =
      region->destination_y_px / destination->block_height_px;
   const uint32_t width_el = source_right_el - source_x_el;
   const uint32_t height_el = source_bottom_el - source_y_el;
   const uint32_t depth_px = source_back - source_front;

   *plan = (struct d12_texture_copy_plan){
      .source_x_el = source_x_el,
      .source_y_el = source_y_el,
      .source_front_px = source_front,
      .destination_x_el = destination_x_el,
      .destination_y_el = destination_y_el,
      .destination_z_px = region->destination_z_px,
      .width_el = width_el,
      .height_el = height_el,
      .depth_px = depth_px,
   };

   if (source_right_el > source->width_el ||
       source_bottom_el > source->height_el || source_back > source->depth_px ||
       destination_x_el > destination->width_el ||
       destination_y_el > destination->height_el ||
       region->destination_z_px > destination->depth_px ||
       width_el > destination->width_el - destination_x_el ||
       height_el > destination->height_el - destination_y_el ||
       depth_px > destination->depth_px - region->destination_z_px)
      return D12_TEXTURE_COPY_PLAN_OUT_OF_BOUNDS;

   return D12_TEXTURE_COPY_PLAN_OK;
}
