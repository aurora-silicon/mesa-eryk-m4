/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_discard_core.h"

#include <assert.h>
#include <stdio.h>

static struct d12_discard_resource_info
texture_2d(uint64_t width, uint32_t height, uint32_t mips,
           uint32_t layers, uint32_t planes)
{
   return (struct d12_discard_resource_info){
      .dimension = D12_DISCARD_DIMENSION_TEXTURE2D,
      .width = width,
      .height = height,
      .mip_levels = mips,
      .array_size = layers,
      .plane_count = planes,
   };
}

int
main(void)
{
   struct d12_discard_resource_info buffer = {
      .dimension = D12_DISCARD_DIMENSION_BUFFER,
   };
   struct d12_discard_resource_info tex = texture_2d(64, 32, 4, 3, 2);
   struct d12_discard_resource_info volume = {
      .dimension = D12_DISCARD_DIMENSION_TEXTURE3D,
      .width = 32,
      .height = 32,
      .mip_levels = 4,
      .array_size = 16,
      .plane_count = 1,
   };

   assert(d12_discard_total_subresources(&buffer) == 1);
   assert(d12_discard_total_subresources(&tex) == 24);
   assert(d12_discard_total_subresources(&volume) == 4);

   assert(d12_discard_validate(true, false, &tex, NULL) ==
          D12_DISCARD_VALID);

   /* Microsoft defines a NULL pRects as a whole-resource discard. The other
    * region fields must not turn that valid call into a range error. */
   struct d12_discard_region_info whole_resource = {
      .rectangle_count = UINT32_MAX,
      .rectangles_present = false,
      .first_subresource = UINT32_MAX,
      .subresource_count = UINT32_MAX,
   };
   assert(d12_discard_validate(true, false, &tex, &whole_resource) ==
          D12_DISCARD_VALID);
   assert(d12_discard_validate(false, false, &tex, NULL) ==
          D12_DISCARD_UNSUPPORTED_COMMAND_LIST);
   assert(d12_discard_validate(false, true, &tex, NULL) ==
          D12_DISCARD_INVALID_ARGUMENT);
   tex.allow_unordered_access = true;
   assert(d12_discard_validate(false, true, &tex, NULL) ==
          D12_DISCARD_VALID);

   struct d12_discard_region_info range = {
      .rectangles_present = true,
      .first_subresource = 20,
      .subresource_count = 4,
   };
   assert(d12_discard_validate(true, false, &tex, &range) ==
          D12_DISCARD_VALID);
   range.first_subresource = 24;
   range.subresource_count = 0;
   assert(d12_discard_validate(true, false, &tex, &range) ==
          D12_DISCARD_VALID);
   range.first_subresource = 25;
   assert(d12_discard_validate(true, false, &tex, &range) ==
          D12_DISCARD_INVALID_ARGUMENT);
   range.first_subresource = 23;
   range.subresource_count = 2;
   assert(d12_discard_validate(true, false, &tex, &range) ==
          D12_DISCARD_INVALID_ARGUMENT);

   range = (struct d12_discard_region_info){
      .rectangle_count = 1,
      .first_subresource = 0,
      .subresource_count = 1,
   };
   assert(d12_discard_validate(true, false, &tex, &range) ==
          D12_DISCARD_VALID);
   range.rectangles_present = true;
   assert(d12_discard_validate(true, false, &tex, &range) ==
          D12_DISCARD_VALID);

   /* Consecutive array/plane slices with one mip have equal dimensions. */
   struct d12_discard_resource_info array_tex = texture_2d(32, 16, 1, 3, 2);
   range.first_subresource = 0;
   range.subresource_count = 6;
   assert(d12_discard_validate(true, false, &array_tex, &range) ==
          D12_DISCARD_VALID);

   /* Different mip indices are accepted when their clamped extents match. */
   struct d12_discard_resource_info mip_tail = texture_2d(1, 1, 4, 1, 1);
   range.subresource_count = 4;
   assert(d12_discard_validate(true, false, &mip_tail, &range) ==
          D12_DISCARD_VALID);

   range.subresource_count = 2;
   assert(d12_discard_validate(true, false, &tex, &range) ==
          D12_DISCARD_INVALID_ARGUMENT);
   range.subresource_count = 1;
   assert(d12_discard_validate(true, false, &buffer, &range) ==
          D12_DISCARD_INVALID_ARGUMENT);
   assert(d12_discard_validate(true, false, &volume, &range) ==
          D12_DISCARD_INVALID_ARGUMENT);

   puts("d12_discard_core_tests: PASS");
   return 0;
}
