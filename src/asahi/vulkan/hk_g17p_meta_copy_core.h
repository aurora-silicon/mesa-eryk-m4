/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asahi/lib/agx_chip.h"
#include "vulkan/vulkan_core.h"

#include <stdbool.h>
#include <stdint.h>

struct hk_g17p_buffer_to_image_copy {
   enum agx_chip chip;
   VkFormat format;
   VkImageLayout layout;
   VkImageType image_type;
   VkSampleCountFlagBits samples;
   uint8_t plane_count;
   uint32_t mip_levels;
   uint32_t array_layers;
   VkExtent3D image_extent;

   VkImageAspectFlags aspect;
   uint32_t mip_level;
   uint32_t base_array_layer;
   uint32_t layer_count;
   VkOffset3D image_offset;
   VkExtent3D copy_extent;
   uint32_t buffer_row_length;
   uint32_t buffer_image_height;
};

/* The initial HAL200 upload path renders one tightly packed RGBA8 staging
 * buffer into one complete, single-layer 2D image.
 */
bool hk_g17p_buffer_to_image_copy_supported(
   const struct hk_g17p_buffer_to_image_copy *copy);
