/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_texture_copy_math.h"

#include <stdio.h>
#include <stdlib.h>

static void
require_result(bool condition, const char *message)
{
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

static struct d12_texture_copy_surface_geometry
surface(uint32_t width, uint32_t height, uint32_t depth)
{
   return (struct d12_texture_copy_surface_geometry){
      .width_px = width,
      .height_px = height,
      .depth_px = depth,
      .width_el = width,
      .height_el = height,
      .block_width_px = 1,
      .block_height_px = 1,
   };
}

static void
test_dimension_pairs(void)
{
   const struct d12_texture_copy_surface_geometry dimensions[] = {
      surface(16, 1, 1), /* Texture1D. */
      surface(16, 4, 1), /* Texture2D. */
      surface(16, 4, 3), /* Texture3D. */
   };
   const struct d12_texture_copy_region_geometry region = {
      .has_source_box = true,
      .source_right_px = 16,
      .source_bottom_px = 1,
      .source_back_px = 1,
   };

   for (unsigned source_dimension = 0; source_dimension < 3;
        ++source_dimension) {
      for (unsigned destination_dimension = 0; destination_dimension < 3;
           ++destination_dimension) {
         struct d12_texture_copy_plan plan;
         require_result(
            d12_texture_copy_plan_region(&dimensions[destination_dimension],
                                         &dimensions[source_dimension],
                                         &region, &plan) ==
               D12_TEXTURE_COPY_PLAN_OK &&
               plan.width_el == 16 && plan.height_el == 1 &&
               plan.depth_px == 1,
            "1D/2D/3D texture-copy pair is accepted");
      }
   }

   const struct d12_texture_copy_region_geometry full_source = {
      .destination_z_px = 1,
   };
   struct d12_texture_copy_plan plan;
   require_result(d12_texture_copy_plan_region(&dimensions[2], &dimensions[0],
                                                &full_source, &plan) ==
                     D12_TEXTURE_COPY_PLAN_OK && plan.width_el == 16 &&
                     plan.height_el == 1 && plan.depth_px == 1,
                  "full 1D source copies into a 3D destination slice");

   require_result(d12_texture_copy_plan_region(&dimensions[2], &dimensions[1],
                                                &full_source, &plan) ==
                     D12_TEXTURE_COPY_PLAN_OK && plan.width_el == 16 &&
                     plan.height_el == 4 && plan.depth_px == 1,
                  "full 2D source copies into a 3D destination slice");

   const struct d12_texture_copy_region_geometry two_d_destination = {0};
   require_result(d12_texture_copy_plan_region(&dimensions[1], &dimensions[2],
                                                &two_d_destination, &plan) ==
                     D12_TEXTURE_COPY_PLAN_OUT_OF_BOUNDS,
                  "full 3D source cannot overrun a 2D destination");
}

static void
test_depth_and_bounds(void)
{
   const struct d12_texture_copy_surface_geometry source = surface(16, 8, 4);
   const struct d12_texture_copy_surface_geometry destination =
      surface(32, 16, 5);
   struct d12_texture_copy_region_geometry region = {
      .destination_x_px = 4,
      .destination_y_px = 5,
      .destination_z_px = 1,
      .has_source_box = true,
      .source_left_px = 2,
      .source_top_px = 1,
      .source_front_px = 1,
      .source_right_px = 10,
      .source_bottom_px = 7,
      .source_back_px = 4,
   };
   struct d12_texture_copy_plan plan;

   require_result(d12_texture_copy_plan_region(&destination, &source, &region,
                                                &plan) ==
                     D12_TEXTURE_COPY_PLAN_OK &&
                     plan.source_x_el == 2 && plan.source_y_el == 1 &&
                     plan.source_front_px == 1 &&
                     plan.destination_x_el == 4 &&
                     plan.destination_y_el == 5 &&
                     plan.destination_z_px == 1 && plan.width_el == 8 &&
                     plan.height_el == 6 && plan.depth_px == 3,
                  "3D sub-box produces exact source and destination plan");

   region.destination_z_px = 3;
   require_result(d12_texture_copy_plan_region(&destination, &source, &region,
                                                &plan) ==
                     D12_TEXTURE_COPY_PLAN_OUT_OF_BOUNDS,
                  "3D copy that overruns destination depth is rejected");

   region.destination_z_px = 0;
   region.source_back_px = source.depth_px + 1;
   require_result(d12_texture_copy_plan_region(&destination, &source, &region,
                                                &plan) ==
                     D12_TEXTURE_COPY_PLAN_OUT_OF_BOUNDS,
                  "source box that overruns source depth is rejected");
}

static void
test_block_geometry(void)
{
   const struct d12_texture_copy_surface_geometry source = {
      .width_px = 15,
      .height_px = 9,
      .depth_px = 1,
      .width_el = 4,
      .height_el = 3,
      .block_width_px = 4,
      .block_height_px = 4,
   };
   const struct d12_texture_copy_surface_geometry destination = source;
   struct d12_texture_copy_region_geometry region = {
      .has_source_box = true,
      .source_left_px = 4,
      .source_top_px = 4,
      .source_right_px = 15,
      .source_bottom_px = 9,
      .source_back_px = 1,
   };
   struct d12_texture_copy_plan plan;

   require_result(d12_texture_copy_plan_region(&destination, &source, &region,
                                                &plan) ==
                     D12_TEXTURE_COPY_PLAN_OK &&
                     plan.source_x_el == 1 && plan.source_y_el == 1 &&
                     plan.width_el == 3 && plan.height_el == 2,
                  "partial final compression blocks round up at texture edge");

   region.source_left_px = 1;
   require_result(d12_texture_copy_plan_region(&destination, &source, &region,
                                                &plan) ==
                     D12_TEXTURE_COPY_PLAN_SOURCE_X_UNALIGNED,
                  "unaligned compressed source x is rejected");
   region.source_left_px = 4;
   region.destination_y_px = 1;
   require_result(d12_texture_copy_plan_region(&destination, &source, &region,
                                                &plan) ==
                     D12_TEXTURE_COPY_PLAN_DESTINATION_Y_UNALIGNED,
                  "unaligned compressed destination y is rejected");
}

static void
test_empty_and_invalid(void)
{
   const struct d12_texture_copy_surface_geometry valid = surface(8, 2, 1);
   struct d12_texture_copy_surface_geometry invalid = valid;
   struct d12_texture_copy_region_geometry region = {
      .has_source_box = true,
      .source_right_px = 8,
      .source_bottom_px = 2,
      .source_back_px = 1,
   };
   struct d12_texture_copy_plan plan;

   region.source_right_px = 0;
   require_result(d12_texture_copy_plan_region(&valid, &valid, &region,
                                                &plan) ==
                     D12_TEXTURE_COPY_PLAN_EMPTY,
                  "empty source box remains a no-op");

   invalid.block_width_px = 0;
   require_result(d12_texture_copy_plan_region(&valid, &invalid, &region,
                                                &plan) ==
                     D12_TEXTURE_COPY_PLAN_INVALID_GEOMETRY,
                  "zero block geometry is rejected");
   require_result(d12_texture_copy_plan_region(NULL, &valid, &region, &plan) ==
                     D12_TEXTURE_COPY_PLAN_INVALID_GEOMETRY &&
                     d12_texture_copy_plan_region(&valid, &valid, NULL,
                                                  &plan) ==
                        D12_TEXTURE_COPY_PLAN_INVALID_GEOMETRY &&
                     d12_texture_copy_plan_region(&valid, &valid, &region,
                                                  NULL) ==
                        D12_TEXTURE_COPY_PLAN_INVALID_GEOMETRY,
                  "null planner arguments are rejected");
}

int
main(void)
{
   test_dimension_pairs();
   test_depth_and_bounds();
   test_block_geometry();
   test_empty_and_invalid();
   puts("d12_texture_copy_math_tests: PASS");
   return 0;
}
