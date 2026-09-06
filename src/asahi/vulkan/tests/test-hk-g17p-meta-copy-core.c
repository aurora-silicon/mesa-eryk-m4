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

   explicit_stride.buffer_row_length += 8;
   explicit_stride.buffer_image_height += 4;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&explicit_stride));

   struct hk_g17p_buffer_to_image_copy bad = vkcube;
   bad.format = VK_FORMAT_B8G8R8A8_UNORM;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&bad));
   bad.format = VK_FORMAT_R8_UNORM;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&bad));
   bad.format = VK_FORMAT_R16G16B16A16_SFLOAT;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.copy_extent.width--;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.layer_count = 2;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.buffer_row_length = 255;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.chip = AGX_CHIP_G17S;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.mip_levels = 2;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.image_offset.x = 1;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = vkcube;
   bad.layout = VK_IMAGE_LAYOUT_GENERAL;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&bad));
   bad.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&bad));
   bad.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&bad));
   bad.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));


   /* Atlas uploads must preserve arbitrary offsets and padded source rows. */
   struct hk_g17p_buffer_to_image_copy atlas = vkcube;
   atlas.image_offset = (VkOffset3D){11, 13, 0};
   atlas.copy_extent = (VkExtent3D){37, 29, 1};
   atlas.buffer_row_length = 80;
   atlas.buffer_image_height = 72;
   CHECK(hk_g17p_buffer_to_image_copy_supported(&atlas));
   unsigned tested = 0;
   for (unsigned mip = 0; mip < 9; ++mip) {
      struct hk_g17p_buffer_to_image_copy level = atlas;
      level.mip_levels = 9;
      level.mip_level = mip;
      const unsigned dim = 256 >> mip;
      level.image_offset = (VkOffset3D){dim - 1, dim - 1, 0};
      level.copy_extent = (VkExtent3D){1, 1, 1};
      for (unsigned layers = 1; layers <= 16; ++layers) {
         level.array_layers = layers;
         for (unsigned first = 0; first < layers; ++first) {
            level.base_array_layer = first;
            level.layer_count = layers - first;
            CHECK(hk_g17p_buffer_to_image_copy_supported(&level));
            level.layer_count = VK_REMAINING_ARRAY_LAYERS;
            CHECK(hk_g17p_buffer_to_image_copy_supported(&level));
            level.layer_count = layers - first + 1;
            CHECK(!hk_g17p_buffer_to_image_copy_supported(&level));
            tested += 3;
         }
      }
      level.layer_count = 1;
      level.copy_extent.width = 2;
      CHECK(!hk_g17p_buffer_to_image_copy_supported(&level));
   }
   bad = atlas;
   bad.image_offset.x = -1;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = atlas;
   bad.image_offset.x = INT32_MAX;
   bad.copy_extent.width = UINT32_MAX;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = atlas;
   bad.mip_level = 32;
   bad.mip_levels = 33;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   bad = atlas;
   bad.array_layers = UINT32_MAX;
   bad.base_array_layer = UINT32_MAX - 1;
   bad.layer_count = 2;
   CHECK(!hk_g17p_buffer_to_image_copy_supported(&bad));
   printf("Honeykrisp G17P meta copy region/mip/layer checks passed (%u exhaustive layer cases)\n", tested);
   return 0;
}
