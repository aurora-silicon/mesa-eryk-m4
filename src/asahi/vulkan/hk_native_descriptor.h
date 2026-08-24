/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Keep this header independent of Vulkan and generated GenXML headers.  Apart
 * from making the descriptor ABI easy to test on the host, this prevents a
 * recognised-but-disabled GPU generation from accidentally selecting the
 * legacy C struct layout.
 */
/* G13 uses Gen7, G14 uses Gen8, G15G/S/C/D, G16P/G/S/C, and G17P/A use
 * Gen9, while G17G/S/C uses Gen10.  G15P remains deliberately unknown. */
#define HK_TEXTURE_DESCRIPTOR_AGX2_SIZE 24
#define HK_TEXTURE_DESCRIPTOR_AGX3_SIZE 32
#define HK_PBE_DESCRIPTOR_AGX2_SIZE     24
#define HK_PBE_DESCRIPTOR_AGX3_SIZE     32
#define HK_SAMPLED_IMAGE_SW_SIZE        40

struct hk_sampled_image_descriptor_layout {
   uint8_t texture;
   uint8_t sampler;
   uint8_t sampler_index;
   uint8_t clamp_0_sampler_index;
   uint8_t lod_bias;
   uint8_t min_lod;
   uint8_t min_lod_uint;
   uint8_t border;
   uint8_t stride;
};

struct hk_storage_image_descriptor_layout {
   uint8_t texture;
   uint8_t pbe;
   uint8_t stride;
};

static inline bool
hk_descriptor_generation_is_agx2(unsigned generation)
{
   return generation == 7 || generation == 8;
}

static inline bool
hk_descriptor_generation_is_agx3(unsigned generation)
{
   return generation == 9 || generation == 10;
}

static inline unsigned
hk_texture_descriptor_size(unsigned generation)
{
   return hk_descriptor_generation_is_agx2(generation)
             ? HK_TEXTURE_DESCRIPTOR_AGX2_SIZE
          : hk_descriptor_generation_is_agx3(generation)
             ? HK_TEXTURE_DESCRIPTOR_AGX3_SIZE
             : 0;
}

static inline unsigned
hk_pbe_descriptor_size(unsigned generation)
{
   return hk_descriptor_generation_is_agx2(generation)
             ? HK_PBE_DESCRIPTOR_AGX2_SIZE
          : hk_descriptor_generation_is_agx3(generation)
             ? HK_PBE_DESCRIPTOR_AGX3_SIZE
             : 0;
}

static inline struct hk_sampled_image_descriptor_layout
hk_sampled_image_descriptor_layout(unsigned generation)
{
   const unsigned texture_size = hk_texture_descriptor_size(generation);

   if (!texture_size)
      return (struct hk_sampled_image_descriptor_layout){0};

   return (struct hk_sampled_image_descriptor_layout){
      .texture = 0,
      .sampler = texture_size,
      .sampler_index = texture_size + 8,
      .clamp_0_sampler_index = texture_size + 10,
      .lod_bias = texture_size + 12,
      .min_lod = texture_size + 14,
      .min_lod_uint = texture_size + 16,
      .border = texture_size + 20,
      .stride = texture_size + HK_SAMPLED_IMAGE_SW_SIZE,
   };
}

static inline struct hk_storage_image_descriptor_layout
hk_storage_image_descriptor_layout(unsigned generation)
{
   const unsigned texture_size = hk_texture_descriptor_size(generation);
   const unsigned pbe_size = hk_pbe_descriptor_size(generation);

   if (!texture_size || !pbe_size)
      return (struct hk_storage_image_descriptor_layout){0};

   return (struct hk_storage_image_descriptor_layout){
      .texture = 0,
      .pbe = texture_size,
      .stride = texture_size + pbe_size,
   };
}

/* The Gen9 multisample selector has not been identified.  AGX3 buffer
 * descriptors likewise have no recovered equivalent for AGX2's software tail
 * offset.  Both cases must remain unavailable rather than aliasing unknown
 * bits in a native descriptor.
 */
static inline bool
hk_native_texture_supported(unsigned generation, bool multisampled,
                            unsigned buffer_tail_offset)
{
   if (hk_descriptor_generation_is_agx2(generation))
      return true;

   if (!hk_descriptor_generation_is_agx3(generation))
      return false;

   if (generation == 9 && multisampled)
      return false;

   return buffer_tail_offset == 0;
}

/* Gen9/10 PBE words are structurally recovered, but Honeykrisp still relies
 * on AGX2 software-image sideband fields which have no proven AGX3 decode.
 */
static inline bool
hk_native_pbe_supported(unsigned generation)
{
   return hk_descriptor_generation_is_agx2(generation);
}
