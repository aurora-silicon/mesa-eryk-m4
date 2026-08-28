/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_g17p_meta_copy_core.h"

#include <stdio.h>

#define CHECK(c)                                                              \
   do {                                                                       \
      if (!(c)) {                                                             \
         fprintf(stderr, "check failed at line %u: %s\n", __LINE__, #c);     \
         return 1;                                                            \
      }                                                                       \
   } while (0)

int
main(void)
{
   const struct hk_g17p_buffer_to_image_copy vkcube = {
      .chip = AGX_CHIP_G17P,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .image_type = VK_IMAGE_TYPE_2D,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .plane_count = 1,
      .mip_levels = 1,
      .array_layers = 1,
      .image_extent = {256, 256, 1},
      .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
      .layer_count = 1,
      .copy_extent = {256, 256, 1},
   };
   CHECK(hk_g17p_buffer_to_image_copy_supported(&vkcube));
   struct hk_g17p_buffer_to_image_copy explicit_stride = vkcube;
   explicit_stride.buffer_row_length = explicit_stride.copy_extent.width;
   explicit_stride.buffer_image_height = explicit_stride.copy_extent.height;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&explicit_stride));

   struct hk_g17p_buffer_to_image_copy bad = vkcube;
   bad.format = VK_FORMAT_B8G8R8A8_UNORM;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.copy_extent.width--;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.layer_count = 2;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.buffer_row_length = 257;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.chip = AGX_CHIP_G17S;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.mip_levels = 2;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.image_offset.x = 1;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.layout = VK_IMAGE_LAYOUT_GENERAL;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));

   puts("Honeykrisp G17P bounded meta copy: 10 checks passed");
   return 0;
}
