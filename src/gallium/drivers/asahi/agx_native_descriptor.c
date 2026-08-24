/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_native_descriptor.h"

#include "asahi/genxml/agx_pack.h"
#include "asahi/lib/agx_abi.h"

#include <string.h>

bool
agx_gallium_pack_native_texture(
   enum agx_descriptor_generation generation,
   struct agx_gallium_texture_descriptor *out,
   const struct AGX_TEXTURE *semantic)
{
   const unsigned descriptor_size =
      agx_gallium_texture_descriptor_size(generation);

   if (!out || !semantic || !descriptor_size)
      return false;

   memset(out, 0, descriptor_size);

   if (agx_gallium_descriptor_generation_is_agx2(generation)) {
      AGX_TEXTURE_pack(out->opaque, semantic);
      return true;
   }

   const bool multisampled =
      semantic->dimension == AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED ||
      semantic->dimension == AGX_TEXTURE_DIMENSION_2D_ARRAY_MULTISAMPLED;

   if (!agx_gallium_native_texture_supported(
          generation, multisampled, semantic->buffer_offset_sw))
      return false;

   if (multisampled) {
      unsigned samples;
      switch (semantic->samples) {
      case AGX_SAMPLE_COUNT_2:
         samples = 2;
         break;
      case AGX_SAMPLE_COUNT_4:
         samples = 4;
         break;
      case AGX_SAMPLE_COUNT_8:
         samples = 8;
         break;
      default:
         return false;
      }

      if (!agx_gallium_native_texture_sample_count_representable(
             generation, samples))
         return false;
   }

   const bool has_levels = semantic->first_level || semantic->last_level ||
                           semantic->unk_mipmapped;
   const bool extended = semantic->extended || has_levels;

   if ((semantic->address & 0xf) || semantic->address >> 48 ||
       (semantic->compressed &&
        ((semantic->acceleration_buffer & 0xf) ||
         semantic->acceleration_buffer >> 48)))
      return false;

   if (generation == AGX_DESCRIPTOR_GENERATION_9) {
      if (!semantic->width || semantic->width > 16384 || !semantic->height ||
          semantic->height > 16384)
         return false;

      struct AGX_TEXTURE_GEN9 cfg = {
         .dimension = semantic->dimension,
         .layout = semantic->layout,
         .channels = semantic->channels,
         .type = semantic->type,
         .swizzle_r = semantic->swizzle_r,
         .swizzle_g = semantic->swizzle_g,
         .swizzle_b = semantic->swizzle_b,
         .swizzle_a = semantic->swizzle_a,
         .width = semantic->width,
         .height = semantic->height,
         .has_levels = has_levels,
         .state_control = semantic->mode | (semantic->compression << 2),
         .address = semantic->address,
         .srgb = semantic->srgb,
         .srgb_2_channel = semantic->srgb_2_channel,
         .stride = semantic->layout == AGX_LAYOUT_LINEAR ? semantic->stride : 0,
         .depth = semantic->layout == AGX_LAYOUT_LINEAR ? 1 : semantic->depth,
         .page_aligned_layers = semantic->page_aligned_layers,
         .extended = extended,
         .acceleration_buffer =
            semantic->compressed && semantic->extended
               ? semantic->acceleration_buffer
               : 0,
         .depth_linear = semantic->layout == AGX_LAYOUT_LINEAR &&
                               semantic->extended
                            ? semantic->depth_linear
                            : 1,
         .layer_stride_linear = semantic->layout == AGX_LAYOUT_LINEAR &&
                                       semantic->extended
                                    ? semantic->layer_stride_linear
                                    : 0,
         .first_level = semantic->first_level,
         .last_level = semantic->last_level,
      };

      AGX_TEXTURE_GEN9_pack(out->opaque, &cfg);
      return true;
   }

   if (generation == AGX_DESCRIPTOR_GENERATION_10) {
      if (!semantic->width || semantic->width > 32768 || !semantic->height ||
          semantic->height > 32768)
         return false;

      struct AGX_TEXTURE_GEN10 cfg = {
         .dimension = semantic->dimension,
         .layout = semantic->layout,
         .channels = semantic->channels,
         .type = semantic->type,
         .swizzle_r = semantic->swizzle_r,
         .swizzle_g = semantic->swizzle_g,
         .swizzle_b = semantic->swizzle_b,
         .swizzle_a = semantic->swizzle_a,
         .width = semantic->width,
         .height = semantic->height,
         .samples = semantic->samples,
         .unk_mipmapped = semantic->unk_mipmapped,
         .compressed = semantic->compressed,
         .image_mode_enable = semantic->mode != AGX_IMAGE_MODE_NORMAL,
         .image_mode = semantic->mode,
         .address = semantic->address,
         .srgb = semantic->srgb,
         .srgb_2_channel = semantic->srgb_2_channel,
         .stride = semantic->layout == AGX_LAYOUT_LINEAR ? semantic->stride : 0,
         .depth = semantic->layout == AGX_LAYOUT_LINEAR ? 1 : semantic->depth,
         .page_aligned_layers = semantic->page_aligned_layers,
         .extended = extended,
         .acceleration_buffer =
            semantic->compressed && semantic->extended
               ? semantic->acceleration_buffer
               : 0,
         .depth_linear = semantic->layout == AGX_LAYOUT_LINEAR &&
                               semantic->extended
                            ? semantic->depth_linear
                            : 1,
         .layer_stride_linear = semantic->layout == AGX_LAYOUT_LINEAR &&
                                       semantic->extended
                                    ? semantic->layer_stride_linear
                                    : 0,
         .first_level = semantic->first_level,
         .last_level = semantic->last_level,
      };

      AGX_TEXTURE_GEN10_pack(out->opaque, &cfg);
      return true;
   }

   return false;
}

bool
agx_gallium_set_null_native_texture(
   enum agx_descriptor_generation generation,
   struct agx_gallium_texture_descriptor *out)
{
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

   return agx_gallium_pack_native_texture(generation, out, &semantic);
}

bool
agx_gallium_pack_native_pbe(
   enum agx_descriptor_generation generation,
   struct agx_gallium_pbe_descriptor *out,
   const struct agx_pbe_packed *legacy)
{
   const unsigned descriptor_size =
      agx_gallium_pbe_descriptor_size(generation);

   if (!out || !legacy || !descriptor_size)
      return false;

   memset(out, 0, descriptor_size);
   if (!agx_gallium_native_pbe_supported(generation))
      return false;

   memcpy(out, legacy, AGX_PBE_LENGTH);
   return true;
}

bool
agx_gallium_set_null_native_pbe(
   enum agx_descriptor_generation generation,
   struct agx_gallium_pbe_descriptor *out)
{
   struct agx_pbe_packed legacy;

   agx_pack(&legacy, PBE, cfg) {
      cfg.width = 1;
      cfg.height = 1;
      cfg.levels = 1;
      cfg.layout = AGX_LAYOUT_TWIDDLED;
      cfg.channels = AGX_CHANNELS_R8;
      cfg.type = AGX_TEXTURE_TYPE_UNORM;
      cfg.swizzle_r = AGX_CHANNEL_R;
      cfg.swizzle_g = AGX_CHANNEL_R;
      cfg.swizzle_b = AGX_CHANNEL_R;
      cfg.swizzle_a = AGX_CHANNEL_R;
      cfg.buffer = AGX_SCRATCH_PAGE_ADDRESS;
   }

   return agx_gallium_pack_native_pbe(generation, out, &legacy);
}
