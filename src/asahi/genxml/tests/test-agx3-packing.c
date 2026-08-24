/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

#define CHECK_EQ(actual, expected)                                             \
   do {                                                                        \
      uint64_t a_ = (actual);                                                   \
      uint64_t e_ = (expected);                                                 \
      ++checks;                                                                 \
      if (a_ != e_) {                                                           \
         fprintf(stderr, "%s:%d: got 0x%llx, expected 0x%llx\n", __FILE__,   \
                 __LINE__, (unsigned long long)a_, (unsigned long long)e_);     \
         exit(1);                                                               \
      }                                                                         \
   } while (0)

static uint64_t
word64(const uint32_t *packed, unsigned index)
{
   uint64_t value;
   memcpy(&value, &packed[index * 2], sizeof(value));
   return value;
}

static void
test_m5_live_descriptors(void)
{
   /* Live AGXG17XFamilyTexture readout for a 256x128 RGBA8 texture. */
   static const uint8_t expected_texture[32] = {
      0x22, 0x0a, 0x88, 0xf6, 0x0f, 0xf8, 0x03, 0x20,
      0x00, 0x80, 0x00, 0x00, 0x10, 0x00, 0x00, 0x80,
      0x00, 0xa0, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   struct AGX_TEXTURE_GEN10 texture = {AGX_TEXTURE_GEN10_header};
   uint32_t texture_packed[AGX_TEXTURE_GEN10_LENGTH / 4] = {0};

   texture.dimension = AGX_TEXTURE_DIMENSION_2D;
   texture.layout = AGX_LAYOUT_GPU;
   texture.channels = AGX_CHANNELS_R8G8B8A8;
   texture.type = AGX_TEXTURE_TYPE_UNORM;
   texture.swizzle_r = AGX_CHANNEL_R;
   texture.swizzle_g = AGX_CHANNEL_G;
   texture.swizzle_b = AGX_CHANNEL_B;
   texture.swizzle_a = AGX_CHANNEL_A;
   texture.width = 256;
   texture.height = 128;
   texture.compressed = true;
   texture.address = 0x10000080000ull;
   texture.extended = true;
   texture.acceleration_buffer = 0x100000a0000ull;

   AGX_TEXTURE_GEN10_pack(texture_packed, &texture);
   ++checks;
   if (memcmp(texture_packed, expected_texture, sizeof(expected_texture))) {
      fprintf(stderr, "M5 live texture descriptor mismatch\n");
      exit(1);
   }

   static const uint8_t expected_pbe[32] = {
      0x22, 0x0a, 0xe4, 0xff, 0x80, 0x3f, 0x00, 0x20,
      0x00, 0x80, 0x00, 0x00, 0x10, 0x00, 0x00, 0x80,
      0x00, 0xa0, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   struct AGX_PBE_GEN10 pbe = {AGX_PBE_GEN10_header};
   uint32_t pbe_packed[AGX_PBE_GEN10_LENGTH / 4] = {0};

   pbe.dimension = AGX_TEXTURE_DIMENSION_2D;
   pbe.layout = AGX_LAYOUT_GPU;
   pbe.channels = AGX_CHANNELS_R8G8B8A8;
   pbe.type = AGX_TEXTURE_TYPE_UNORM;
   pbe.swizzle_r = AGX_CHANNEL_R;
   pbe.swizzle_g = AGX_CHANNEL_G;
   pbe.swizzle_b = AGX_CHANNEL_B;
   pbe.swizzle_a = AGX_CHANNEL_A;
   pbe.width = 256;
   pbe.height = 128;
   pbe.compressed = true;
   pbe.buffer = 0x10000080000ull;
   pbe.extended = true;
   pbe.acceleration_buffer = 0x100000a0000ull;

   AGX_PBE_GEN10_pack(pbe_packed, &pbe);
   ++checks;
   if (memcmp(pbe_packed, expected_pbe, sizeof(expected_pbe))) {
      fprintf(stderr, "M5 live PBE descriptor mismatch\n");
      exit(1);
   }
}

static void
test_generation_boundaries(void)
{
   struct AGX_PBE_GEN9 gen9 = {AGX_PBE_GEN9_header};
   struct AGX_PBE_GEN10 gen10 = {AGX_PBE_GEN10_header};
   uint32_t packed9[AGX_PBE_GEN9_LENGTH / 4] = {0};
   uint32_t packed10[AGX_PBE_GEN10_LENGTH / 4] = {0};

   gen9.width = gen10.width = 0x4000;
   gen9.height = gen10.height = 0x2345;
   gen9.samples = gen10.samples = AGX_SAMPLE_COUNT_8;
   gen9.buffer = gen10.buffer = 0xabcde123450ull;
   gen9.level = gen10.level = 3;
   gen9.levels = gen10.levels = 7;
   gen9.extended = gen10.extended = true;

   AGX_PBE_GEN9_pack(packed9, &gen9);
   AGX_PBE_GEN10_pack(packed10, &gen10);

   CHECK_EQ((word64(packed9, 0) >> 56) & 3, AGX_SAMPLE_COUNT_8);
   CHECK_EQ((word64(packed10, 0) >> 58) & 3, AGX_SAMPLE_COUNT_8);
   CHECK_EQ(word64(packed9, 1) & ((1ull << 44) - 1), gen9.buffer >> 4);
   CHECK_EQ(word64(packed10, 1) & ((1ull << 44) - 1), gen10.buffer >> 4);
   CHECK_EQ((word64(packed9, 2) >> 44) & 0xff, 0x63);
   CHECK_EQ((word64(packed10, 2) >> 44) & 0xff, 0x63);
   CHECK_EQ(word64(packed9, 3), 0);
   CHECK_EQ(word64(packed10, 3), 0);

   /* Width 0x4000 fits Gen9 exactly; Gen10 retains a fifteenth bit. */
   CHECK_EQ((word64(packed9, 0) >> 24) & 0x3fff, 0x3fff);
   CHECK_EQ((word64(packed10, 0) >> 24) & 0x7fff, 0x3fff);
}

static void
test_sampler_generations(void)
{
   struct AGX_SAMPLER_HAL200 hal200 = {AGX_SAMPLER_HAL200_header};
   struct AGX_SAMPLER_GEN5 gen5 = {AGX_SAMPLER_GEN5_header};
   uint32_t packed_hal200[AGX_SAMPLER_HAL200_LENGTH / 4] = {0};
   uint32_t packed_gen5[AGX_SAMPLER_GEN5_LENGTH / 4] = {0};

   hal200.maximum_lod = 100.0f;
   hal200.lod_bias = -65;
   hal200.border_colour = AGX_BORDER_COLOUR_OPAQUE_WHITE;
   hal200.seamful_cube_maps = true;
   AGX_SAMPLER_HAL200_pack(packed_hal200, &hal200);

   gen5.maximum_lod = 100.0f;
   gen5.lod_bias = -65;
   gen5.reduction_mode = AGX_SAMPLER_REDUCTION_MODE_MINIMUM;
   gen5.border_colour = AGX_BORDER_COLOUR_OPAQUE_WHITE;
   gen5.seamful_cube_maps = true;
   AGX_SAMPLER_GEN5_pack(packed_gen5, &gen5);

   CHECK_EQ((word64(packed_hal200, 0) >> 10) & 0x3ff, 0x380);
   CHECK_EQ((word64(packed_gen5, 0) >> 10) & 0x3ff, 0x3c0);
   CHECK_EQ((word64(packed_hal200, 0) >> 43) & 0x7ff, 0x7bf);
   CHECK_EQ((word64(packed_gen5, 0) >> 43) & 0x7ff, 0x7bf);
   CHECK_EQ((word64(packed_gen5, 0) >> 39) & 3,
            AGX_SAMPLER_REDUCTION_MODE_MINIMUM);
   CHECK_EQ((word64(packed_hal200, 0) >> 61) & 3,
            AGX_BORDER_COLOUR_OPAQUE_WHITE);
   CHECK_EQ((word64(packed_gen5, 0) >> 61) & 3,
            AGX_BORDER_COLOUR_OPAQUE_WHITE);
   CHECK_EQ(word64(packed_hal200, 0) >> 63, 1);
   CHECK_EQ(word64(packed_gen5, 0) >> 63, 1);

   memset(packed_gen5, 0, sizeof(packed_gen5));
   gen5.reduction_mode = AGX_SAMPLER_REDUCTION_MODE_MAXIMUM;
   AGX_SAMPLER_GEN5_pack(packed_gen5, &gen5);
   CHECK_EQ((word64(packed_gen5, 0) >> 39) & 3,
            AGX_SAMPLER_REDUCTION_MODE_MAXIMUM);
}

int
main(void)
{
   CHECK_EQ(AGX_TEXTURE_GEN9_LENGTH, 32);
   CHECK_EQ(AGX_TEXTURE_GEN10_LENGTH, 32);
   CHECK_EQ(AGX_PBE_GEN9_LENGTH, 32);
   CHECK_EQ(AGX_PBE_GEN10_LENGTH, 32);
   CHECK_EQ(AGX_SAMPLER_HAL200_LENGTH, 8);
   CHECK_EQ(AGX_SAMPLER_GEN5_LENGTH, 8);

   test_m5_live_descriptors();
   test_generation_boundaries();
   test_sampler_generations();

   printf("agx3 packing: all %u checks passed\n", checks);
   return 0;
}
