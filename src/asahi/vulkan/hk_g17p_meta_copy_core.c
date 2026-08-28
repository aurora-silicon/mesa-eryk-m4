/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_g17p_meta_copy_core.h"

bool
hk_g17p_buffer_to_image_copy_supported(
   const struct hk_g17p_buffer_to_image_copy *copy)
{
   if (!copy || copy->chip != AGX_CHIP_G17P ||
       copy->format != VK_FORMAT_R8G8B8A8_UNORM ||
       copy->layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
       copy->image_type != VK_IMAGE_TYPE_2D ||
       copy->samples != VK_SAMPLE_COUNT_1_BIT || copy->plane_count != 1 ||
       copy->mip_levels != 1 || copy->array_layers != 1 ||
       copy->aspect != VK_IMAGE_ASPECT_COLOR_BIT || copy->mip_level != 0 ||
       copy->base_array_layer != 0 || copy->layer_count != 1 ||
       copy->image_offset.x != 0 || copy->image_offset.y != 0 ||
       copy->image_offset.z != 0 || copy->image_extent.depth != 1 ||
       copy->copy_extent.depth != 1 ||
       copy->copy_extent.width != copy->image_extent.width ||
       copy->copy_extent.height != copy->image_extent.height)
      return false;

   return (copy->buffer_row_length == 0 ||
           copy->buffer_row_length == copy->copy_extent.width) &&
          (copy->buffer_image_height == 0 ||
           copy->buffer_image_height == copy->copy_extent.height);
}
