/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_native_descriptor_pack.h"

#include "asahi/lib/agx_abi.h"

#include <string.h>

bool
hk_translate_native_texture_sample_count(unsigned generation, unsigned samples,
                                         enum agx_sample_count *out)
{
   if (!out || samples == 1 ||
       !hk_native_texture_sample_count_representable(generation, samples))
      return false;

   switch (samples) {
   case 2:
      *out = AGX_SAMPLE_COUNT_2;
      return true;
   case 4:
      *out = AGX_SAMPLE_COUNT_4;
      return true;
   case 8:
      *out = AGX_SAMPLE_COUNT_8;
      return true;
   default:
      return false;
   }
}

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

   /* The Apple Gen9/10 encoders accept the three recovered two-bit selector
    * values only.  Reject malformed semantic input instead of truncating it.
    */
   if (multisampled &&
       (unsigned)legacy->samples > (unsigned)AGX_SAMPLE_COUNT_8)
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

      if (multisampled) {
         /* In four independently extracted G15/G16 encoders,
          * TextureGen4::encodeImageReadState takes bits 2:3 of the numeric
          * sample count and writes them to word0 bits 56:57.  For 2x/4x/8x
          * this is exactly AGX_SAMPLE_COUNT_2/4/8 (0/1/2).
          */
         uint64_t word0;
         memcpy(&word0, out->opaque, sizeof(word0));
         word0 &= ~(UINT64_C(3) << 56);
         word0 |= (uint64_t)legacy->samples << 56;
         memcpy(out->opaque, &word0, sizeof(word0));
      }

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
hk_pack_native_pbe(unsigned generation,
                   struct hk_native_pbe_descriptor *out,
                   const struct AGX_PBE *legacy)
{
   if (!out)
      return false;

   memset(out, 0, sizeof(*out));
   if (!legacy || !hk_pbe_descriptor_size(generation))
      return false;

   if (hk_descriptor_generation_is_agx2(generation)) {
      AGX_PBE_pack(out->opaque, legacy);
      return true;
   }

   if (!hk_descriptor_generation_is_agx3(generation) ||
       (legacy->buffer & 0xf) || legacy->buffer >> 48 ||
       (legacy->compressed &&
        ((legacy->acceleration_buffer & 0xf) ||
         legacy->acceleration_buffer >> 48)))
      return false;

   const bool linear = legacy->layout == AGX_LAYOUT_LINEAR;
   const bool has_levels = !linear &&
                           (legacy->level != 0 || legacy->levels > 1);
   const bool extended = legacy->extended || has_levels;

#define HK_INIT_AGX3_PBE(cfg_)                                                \
   do {                                                                        \
      (cfg_).dimension = legacy->dimension;                                    \
      (cfg_).layout = legacy->layout;                                          \
      (cfg_).channels = legacy->channels;                                      \
      (cfg_).type = legacy->type;                                              \
      (cfg_).swizzle_r = legacy->swizzle_r;                                    \
      (cfg_).swizzle_g = legacy->swizzle_g;                                    \
      (cfg_).swizzle_b = legacy->swizzle_b;                                    \
      (cfg_).swizzle_a = legacy->swizzle_a;                                    \
      (cfg_).width = legacy->width;                                            \
      (cfg_).height = legacy->height;                                          \
      (cfg_).samples = legacy->samples;                                        \
      (cfg_).unk_mipmapped = legacy->unk_mipmapped || has_levels;              \
      (cfg_).compressed = legacy->compressed;                                  \
      (cfg_).image_mode_enable = legacy->mode != AGX_IMAGE_MODE_NORMAL;        \
      (cfg_).image_mode = legacy->mode;                                        \
      (cfg_).buffer = legacy->buffer;                                          \
      (cfg_).stride = linear ? legacy->stride : 0;                             \
      (cfg_).layers = linear ? 1 : legacy->layers;                             \
      (cfg_).page_aligned_layers = legacy->page_aligned_layers;                \
      (cfg_).gamma = legacy->srgb;                                             \
      (cfg_).extended = extended;                                              \
      (cfg_).acceleration_buffer =                                             \
         legacy->compressed && legacy->extended                                \
            ? legacy->acceleration_buffer : 0;                                 \
      (cfg_).depth_linear = linear && legacy->extended                         \
                               ? legacy->depth_linear : 1;                     \
      (cfg_).layer_stride_linear = linear && legacy->extended                  \
                                      ? legacy->layer_stride_linear : 0;       \
      (cfg_).level = linear ? 0 : legacy->level;                               \
      (cfg_).levels = linear ? 1 : legacy->levels;                             \
   } while (0)

   if (generation == 9) {
      if (!legacy->width || legacy->width > 16384 || !legacy->height ||
          legacy->height > 16384)
         return false;

      struct AGX_PBE_GEN9 cfg = {AGX_PBE_GEN9_header};
      HK_INIT_AGX3_PBE(cfg);
      cfg.unk_52 = legacy->unk_52;
      AGX_PBE_GEN9_pack(out->opaque, &cfg);
      return true;
   }

   if (generation == 10) {
      if (!legacy->width || legacy->width > 32768 || !legacy->height ||
          legacy->height > 32768)
         return false;

      struct AGX_PBE_GEN10 cfg = {AGX_PBE_GEN10_header};
      HK_INIT_AGX3_PBE(cfg);
      cfg.unk_54 = legacy->unk_52;
      AGX_PBE_GEN10_pack(out->opaque, &cfg);
      return true;
   }

#undef HK_INIT_AGX3_PBE
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
