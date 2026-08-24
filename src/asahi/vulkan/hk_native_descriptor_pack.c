/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_native_descriptor_pack.h"

#include "asahi/lib/agx_abi.h"

#include <string.h>

bool
hk_pack_native_texture(unsigned generation,
                       struct hk_native_texture_descriptor *out,
                       const struct AGX_TEXTURE *legacy)
{
   if (!out || !legacy || !hk_texture_descriptor_size(generation))
      return false;

   memset(out, 0, sizeof(*out));

   if (hk_descriptor_generation_is_agx2(generation)) {
      AGX_TEXTURE_pack(out->opaque, legacy);
      return true;
   }

   const bool multisampled =
      legacy->dimension == AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED ||
      legacy->dimension == AGX_TEXTURE_DIMENSION_2D_ARRAY_MULTISAMPLED;
   if (!hk_native_texture_supported(generation, multisampled,
                                    legacy->buffer_offset_sw))
      return false;

   const bool has_levels =
      legacy->first_level || legacy->last_level || legacy->unk_mipmapped;
   const bool extended = legacy->extended || has_levels;
   if ((legacy->address & 0xf) || legacy->address >> 48 ||
       (legacy->compressed &&
        ((legacy->acceleration_buffer & 0xf) ||
         legacy->acceleration_buffer >> 48)))
      return false;

   if (generation == 9) {
      if (!legacy->width || legacy->width > 16384 || !legacy->height ||
          legacy->height > 16384)
         return false;

      struct AGX_TEXTURE_GEN9 cfg = {
         .dimension = legacy->dimension,
         .layout = legacy->layout,
         .channels = legacy->channels,
         .type = legacy->type,
         .swizzle_r = legacy->swizzle_r,
         .swizzle_g = legacy->swizzle_g,
         .swizzle_b = legacy->swizzle_b,
         .swizzle_a = legacy->swizzle_a,
         .width = legacy->width,
         .height = legacy->height,
         .has_levels = has_levels,
         .state_control = legacy->mode | (legacy->compression << 2),
         .address = legacy->address,
         .srgb = legacy->srgb,
         .srgb_2_channel = legacy->srgb_2_channel,
         .stride = legacy->layout == AGX_LAYOUT_LINEAR ? legacy->stride : 0,
         .depth = legacy->layout == AGX_LAYOUT_LINEAR ? 1 : legacy->depth,
         .page_aligned_layers = legacy->page_aligned_layers,
         .extended = extended,
         .acceleration_buffer =
            legacy->compressed && legacy->extended
               ? legacy->acceleration_buffer
               : 0,
         .depth_linear = legacy->layout == AGX_LAYOUT_LINEAR &&
                               legacy->extended
                            ? legacy->depth_linear
                            : 1,
         .layer_stride_linear = legacy->layout == AGX_LAYOUT_LINEAR &&
                                       legacy->extended
                                    ? legacy->layer_stride_linear
                                    : 0,
         .first_level = legacy->first_level,
         .last_level = legacy->last_level,
      };

      AGX_TEXTURE_GEN9_pack(out->opaque, &cfg);
      return true;
   }

   if (generation == 10) {
      if (!legacy->width || legacy->width > 32768 || !legacy->height ||
          legacy->height > 32768)
         return false;

      struct AGX_TEXTURE_GEN10 cfg = {
         .dimension = legacy->dimension,
         .layout = legacy->layout,
         .channels = legacy->channels,
         .type = legacy->type,
         .swizzle_r = legacy->swizzle_r,
         .swizzle_g = legacy->swizzle_g,
         .swizzle_b = legacy->swizzle_b,
         .swizzle_a = legacy->swizzle_a,
         .width = legacy->width,
         .height = legacy->height,
         .samples = legacy->samples,
         .unk_mipmapped = legacy->unk_mipmapped,
         .compressed = legacy->compressed,
         .image_mode_enable = legacy->mode != AGX_IMAGE_MODE_NORMAL,
         .image_mode = legacy->mode,
         .address = legacy->address,
         .srgb = legacy->srgb,
         .srgb_2_channel = legacy->srgb_2_channel,
         .stride = legacy->layout == AGX_LAYOUT_LINEAR ? legacy->stride : 0,
         .depth = legacy->layout == AGX_LAYOUT_LINEAR ? 1 : legacy->depth,
         .page_aligned_layers = legacy->page_aligned_layers,
         .extended = extended,
         .acceleration_buffer =
            legacy->compressed && legacy->extended
               ? legacy->acceleration_buffer
               : 0,
         .depth_linear = legacy->layout == AGX_LAYOUT_LINEAR &&
                               legacy->extended
                            ? legacy->depth_linear
                            : 1,
         .layer_stride_linear = legacy->layout == AGX_LAYOUT_LINEAR &&
                                       legacy->extended
                                    ? legacy->layer_stride_linear
                                    : 0,
         .first_level = legacy->first_level,
         .last_level = legacy->last_level,
      };

      AGX_TEXTURE_GEN10_pack(out->opaque, &cfg);
      return true;
   }

   return false;
}

bool
hk_set_null_native_texture(unsigned generation,
                           struct hk_native_texture_descriptor *out)
{
   if (!out)
      return false;

   const struct AGX_TEXTURE semantic = {
      AGX_TEXTURE_header,
      .layout = AGX_LAYOUT_TWIDDLED,
      .channels = AGX_CHANNELS_R8,
      .type = AGX_TEXTURE_TYPE_UNORM,
      .swizzle_r = AGX_CHANNEL_0,
      .swizzle_g = AGX_CHANNEL_0,
      .swizzle_b = AGX_CHANNEL_0,
      .swizzle_a = AGX_CHANNEL_0,
      .address = AGX_ZERO_PAGE_ADDRESS,
   };

   return hk_pack_native_texture(generation, out, &semantic);
}

bool
hk_set_null_native_pbe(unsigned generation,
                       struct hk_native_pbe_descriptor *out)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!hk_native_pbe_supported(generation))
      return false;

   const struct AGX_PBE semantic = {
      AGX_PBE_header,
      .layout = AGX_LAYOUT_TWIDDLED,
      .channels = AGX_CHANNELS_R8,
      .type = AGX_TEXTURE_TYPE_UNORM,
      .swizzle_r = AGX_CHANNEL_R,
      .swizzle_g = AGX_CHANNEL_R,
      .swizzle_b = AGX_CHANNEL_R,
      .swizzle_a = AGX_CHANNEL_R,
      .buffer = AGX_SCRATCH_PAGE_ADDRESS,
   };
   AGX_PBE_pack(out->opaque, &semantic);
   return true;
}
