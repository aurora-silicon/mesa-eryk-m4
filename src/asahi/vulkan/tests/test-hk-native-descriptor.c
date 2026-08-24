/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_native_descriptor_pack.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct legacy_sampled_image_descriptor {
   uint8_t texture[24];
   uint8_t sampler[8];
   uint16_t sampler_index;
   int16_t clamp_0_sampler_index;
   uint16_t lod_bias;
   uint16_t min_lod;
   uint16_t min_lod_uint;
   uint16_t pad;
   uint32_t border[4];
   uint8_t pad2[4];
};

static void
check_layout(unsigned generation, unsigned texture_size,
             unsigned sampled_stride, unsigned storage_stride)
{
   const struct hk_sampled_image_descriptor_layout sampled =
      hk_sampled_image_descriptor_layout(generation);
   const struct hk_storage_image_descriptor_layout storage =
      hk_storage_image_descriptor_layout(generation);

   assert(hk_texture_descriptor_size(generation) == texture_size);
   assert(sampled.texture == 0);
   assert(sampled.sampler == texture_size);
   assert(sampled.sampler_index == texture_size + 8);
   assert(sampled.clamp_0_sampler_index == texture_size + 10);
   assert(sampled.lod_bias == texture_size + 12);
   assert(sampled.min_lod == texture_size + 14);
   assert(sampled.min_lod_uint == texture_size + 16);
   assert(sampled.border == texture_size + 20);
   assert(sampled.stride == sampled_stride);
   assert(storage.texture == 0);
   assert(storage.pbe == texture_size);
   assert(storage.stride == storage_stride);
}

int
main(void)
{
   static const struct {
      const char *chip;
      unsigned generation;
      unsigned texture_size;
      unsigned sampled_stride;
      unsigned storage_stride;
   } matrix[] = {
      {"G13 (M1/M2 class)", 7, 24, 64, 48},
      {"G14 (M2 class)", 8, 24, 64, 48},
      {"G15G (M3)", 9, 32, 72, 64},
      {"G15S (M3 Pro)", 9, 32, 72, 64},
      {"G15C/G15D (M3 Max/Ultra)", 9, 32, 72, 64},
      {"G16P (phone mapping ungrounded)", 9, 32, 72, 64},
      {"G16G (M4)", 9, 32, 72, 64},
      {"G16S (M4 Pro)", 9, 32, 72, 64},
      {"G16C (M4 Max)", 9, 32, 72, 64},
      {"G17P (A18 Pro) / G17A", 9, 32, 72, 64},
      {"G17G (M5)", 10, 32, 72, 64},
      {"G17S (M5 Pro)", 10, 32, 72, 64},
      {"G17C (M5 Max)", 10, 32, 72, 64},
   };

   for (unsigned i = 0; i < sizeof(matrix) / sizeof(matrix[0]); ++i) {
      assert(matrix[i].chip != NULL);
      check_layout(matrix[i].generation, matrix[i].texture_size,
                   matrix[i].sampled_stride, matrix[i].storage_stride);
   }

   /* The A16/G15P descriptor ABI is absent from the measured corpus. */
   assert(hk_texture_descriptor_size(0) == 0);
   assert(hk_sampled_image_descriptor_layout(0).stride == 0);
   assert(!hk_native_texture_supported(0, false, 0));
   assert(!hk_native_pbe_supported(0));

   /* Every byte of the existing AGX2 ABI remains at its old offset. */
   const struct hk_sampled_image_descriptor_layout legacy =
      hk_sampled_image_descriptor_layout(8);
   assert(sizeof(struct legacy_sampled_image_descriptor) == legacy.stride);
   assert(offsetof(struct legacy_sampled_image_descriptor, sampler) ==
          legacy.sampler);
   assert(offsetof(struct legacy_sampled_image_descriptor, sampler_index) ==
          legacy.sampler_index);
   assert(offsetof(struct legacy_sampled_image_descriptor,
                   clamp_0_sampler_index) == legacy.clamp_0_sampler_index);
   assert(offsetof(struct legacy_sampled_image_descriptor, lod_bias) ==
          legacy.lod_bias);
   assert(offsetof(struct legacy_sampled_image_descriptor, min_lod) ==
          legacy.min_lod);
   assert(offsetof(struct legacy_sampled_image_descriptor, min_lod_uint) ==
          legacy.min_lod_uint);
   assert(offsetof(struct legacy_sampled_image_descriptor, border) ==
          legacy.border);

   assert(hk_native_texture_supported(7, true, 12));
   assert(hk_native_texture_supported(9, true, 0));
   assert(hk_native_texture_supported(9, false, 0));
   assert(!hk_native_texture_supported(9, false, 4));
   assert(hk_native_texture_supported(10, true, 0));
   assert(!hk_native_texture_supported(10, false, 12));
   assert(hk_native_texture_sample_count_representable(7, 2));
   assert(hk_native_texture_sample_count_representable(8, 4));
   assert(hk_native_texture_sample_count_representable(9, 2));
   assert(hk_native_texture_sample_count_representable(9, 4));
   assert(hk_native_texture_sample_count_representable(9, 8));
   assert(!hk_native_texture_sample_count_representable(9, 16));
   assert(hk_native_texture_sample_count_representable(10, 8));
   assert(!hk_native_texture_sample_count_representable(10, 16));
   assert(!hk_native_texture_sample_count_representable(0, 1));
   assert(hk_native_pbe_supported(7));
   assert(hk_native_pbe_supported(8));
   assert(!hk_native_pbe_supported(9));
   assert(!hk_native_pbe_supported(10));

   struct hk_native_texture_descriptor null9, null10;
   assert(hk_set_null_native_texture(9, &null9));
   assert(hk_set_null_native_texture(10, &null10));
   assert(memcmp(&null9, &(struct hk_native_texture_descriptor){0},
                 sizeof(null9)) != 0);
   assert(memcmp(&null10, &(struct hk_native_texture_descriptor){0},
                 sizeof(null10)) != 0);

   struct AGX_TEXTURE semantic = {
      AGX_TEXTURE_header,
      .dimension = AGX_TEXTURE_DIMENSION_2D,
      .layout = AGX_LAYOUT_GPU,
      .channels = AGX_CHANNELS_R8G8B8A8,
      .type = AGX_TEXTURE_TYPE_UNORM,
      .swizzle_r = AGX_CHANNEL_R,
      .swizzle_g = AGX_CHANNEL_G,
      .swizzle_b = AGX_CHANNEL_B,
      .swizzle_a = AGX_CHANNEL_A,
      .width = 256,
      .height = 128,
      .address = 0x80000,
      .compressed = true,
      .extended = true,
      .acceleration_buffer = 0xa0000,
   };
   struct agx_texture_packed legacy_packed;
   struct hk_native_texture_descriptor native_packed;
   AGX_TEXTURE_pack((uint32_t *)&legacy_packed, &semantic);
   assert(hk_pack_native_texture(8, &native_packed, &semantic));
   assert(memcmp(&legacy_packed, &native_packed, sizeof(legacy_packed)) == 0);
   for (unsigned i = sizeof(legacy_packed); i < sizeof(native_packed); ++i)
      assert(((const uint8_t *)&native_packed)[i] == 0);

   /* M5 native bytes are grounded in a live AGXG17XFamilyTexture readout. */
   static const uint8_t m5_texture[32] = {
      0x22, 0x0a, 0x88, 0xf6, 0x0f, 0xf8, 0x03, 0x20,
      0x00, 0x80, 0x00, 0x00, 0x10, 0x00, 0x00, 0x80,
      0x00, 0xa0, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   semantic.address = 0x10000080000ull;
   semantic.acceleration_buffer = 0x100000a0000ull;
   assert(hk_pack_native_texture(10, &native_packed, &semantic));
   assert(memcmp(&native_packed, m5_texture, sizeof(m5_texture)) == 0);

   enum agx_sample_count samples;
   assert(hk_translate_native_texture_sample_count(9, 2, &samples));
   assert(samples == AGX_SAMPLE_COUNT_2);
   assert(hk_translate_native_texture_sample_count(9, 4, &samples));
   assert(samples == AGX_SAMPLE_COUNT_4);
   assert(hk_translate_native_texture_sample_count(9, 8, &samples));
   assert(samples == AGX_SAMPLE_COUNT_8);
   assert(hk_translate_native_texture_sample_count(10, 8, &samples));
   assert(samples == AGX_SAMPLE_COUNT_8);
   assert(!hk_translate_native_texture_sample_count(10, 1, &samples));

   semantic.dimension = AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED;
   static const struct {
      enum agx_sample_count samples;
      unsigned selector;
   } gen9_msaa[] = {
      {AGX_SAMPLE_COUNT_2, 0},
      {AGX_SAMPLE_COUNT_4, 1},
      {AGX_SAMPLE_COUNT_8, 2},
   };
   for (unsigned i = 0; i < sizeof(gen9_msaa) / sizeof(gen9_msaa[0]); ++i) {
      semantic.samples = gen9_msaa[i].samples;
      assert(hk_pack_native_texture(9, &native_packed, &semantic));

      uint64_t gen9_msaa_word0;
      memcpy(&gen9_msaa_word0, native_packed.opaque,
             sizeof(gen9_msaa_word0));
      assert(((gen9_msaa_word0 >> 56) & 3) == gen9_msaa[i].selector);

      struct AGX_TEXTURE_GEN9 unpacked = {0};
      assert(AGX_TEXTURE_GEN9_unpack(NULL,
                                     (const uint8_t *)native_packed.opaque,
                                     &unpacked));
      assert(unpacked.samples == gen9_msaa[i].samples);
   }

   memset(&native_packed, 0xff, sizeof(native_packed));
   semantic.samples = (enum agx_sample_count)3;
   assert(!hk_pack_native_texture(9, &native_packed, &semantic));
   assert(memcmp(&native_packed, &(struct hk_native_texture_descriptor){0},
                 sizeof(native_packed)) == 0);
   memset(&native_packed, 0xff, sizeof(native_packed));
   assert(!hk_pack_native_texture(10, &native_packed, &semantic));
   assert(memcmp(&native_packed, &(struct hk_native_texture_descriptor){0},
                 sizeof(native_packed)) == 0);

   semantic.samples = AGX_SAMPLE_COUNT_8;
   assert(hk_pack_native_texture(10, &native_packed, &semantic));
   uint64_t m5_msaa_word0;
   memcpy(&m5_msaa_word0, native_packed.opaque, sizeof(m5_msaa_word0));
   assert(((m5_msaa_word0 >> 58) & 3) == AGX_SAMPLE_COUNT_8);

   semantic.dimension = AGX_TEXTURE_DIMENSION_2D;
   memset(&native_packed, 0xff, sizeof(native_packed));
   assert(!hk_pack_native_texture(9, &native_packed, &semantic));
   assert(memcmp(&native_packed, &(struct hk_native_texture_descriptor){0},
                 sizeof(native_packed)) == 0);
   memset(&native_packed, 0xff, sizeof(native_packed));
   assert(!hk_pack_native_texture(10, &native_packed, &semantic));
   assert(memcmp(&native_packed, &(struct hk_native_texture_descriptor){0},
                 sizeof(native_packed)) == 0);

   semantic.samples = AGX_SAMPLE_COUNT_2;
   semantic.buffer_offset_sw = 1;
   assert(!hk_pack_native_texture(10, &native_packed, &semantic));
   for (unsigned i = 0; i < sizeof(native_packed); ++i)
      assert(((const uint8_t *)&native_packed)[i] == 0);

   struct AGX_PBE pbe = {
      AGX_PBE_header,
      .dimension = AGX_TEXTURE_DIMENSION_2D,
      .layout = AGX_LAYOUT_GPU,
      .channels = AGX_CHANNELS_R8G8B8A8,
      .type = AGX_TEXTURE_TYPE_UNORM,
      .swizzle_r = AGX_CHANNEL_R,
      .swizzle_g = AGX_CHANNEL_G,
      .swizzle_b = AGX_CHANNEL_B,
      .swizzle_a = AGX_CHANNEL_A,
      .width = 256,
      .height = 128,
      .buffer = 0x80000,
      .compressed = true,
      .extended = true,
      .acceleration_buffer = 0xa0000,
   };
   struct hk_native_pbe_descriptor native_pbe;

   struct agx_pbe_packed legacy_pbe;
   AGX_PBE_pack(legacy_pbe.opaque, &pbe);
   assert(hk_pack_native_pbe(8, &native_pbe, &pbe));
   assert(memcmp(&legacy_pbe, &native_pbe, sizeof(legacy_pbe)) == 0);
   for (unsigned i = sizeof(legacy_pbe); i < sizeof(native_pbe); ++i)
      assert(((const uint8_t *)&native_pbe)[i] == 0);

   /* This is the paired live AGXG17XFamilyTexture PBE word.  Packing the
    * hardware descriptor does not open Honeykrisp's software-addressing gate.
    */
   static const uint8_t m5_pbe[32] = {
      0x22, 0x0a, 0xe4, 0xff, 0x80, 0x3f, 0x00, 0x20,
      0x00, 0x80, 0x00, 0x00, 0x10, 0x00, 0x00, 0x80,
      0x00, 0xa0, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   };
   pbe.buffer = 0x10000080000ull;
   pbe.acceleration_buffer = 0x100000a0000ull;
   assert(hk_pack_native_pbe(10, &native_pbe, &pbe));
   assert(memcmp(&native_pbe, m5_pbe, sizeof(m5_pbe)) == 0);
   assert(!hk_native_pbe_supported(10));

   struct AGX_PBE_GEN9 unpacked9 = {0};
   pbe.width = 16384;
   pbe.height = 0x2345;
   pbe.level = 3;
   pbe.levels = 7;
   pbe.unk_52 = true;
   assert(hk_pack_native_pbe(9, &native_pbe, &pbe));
   assert(AGX_PBE_GEN9_unpack(NULL, (const uint8_t *)native_pbe.opaque,
                              &unpacked9));
   assert(unpacked9.width == pbe.width && unpacked9.height == pbe.height);
   assert(unpacked9.level == 3 && unpacked9.levels == 7);
   assert(unpacked9.unk_mipmapped && unpacked9.extended && unpacked9.unk_52);

   memset(&native_pbe, 0xff, sizeof(native_pbe));
   pbe.buffer |= 1;
   assert(!hk_pack_native_pbe(9, &native_pbe, &pbe));
   assert(memcmp(&native_pbe, &(struct hk_native_pbe_descriptor){0},
                 sizeof(native_pbe)) == 0);

   puts("honeykrisp native descriptor geometry/policy: passed");
   return 0;
}
