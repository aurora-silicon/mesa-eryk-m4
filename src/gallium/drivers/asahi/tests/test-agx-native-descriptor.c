/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_native_descriptor.h"
#include "asahi/genxml/agx_pack.h"
#include "asahi/lib/agx_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

#define CHECK(cond)                                                            \
   do {                                                                        \
      ++checks;                                                                \
      if (!(cond)) {                                                           \
         fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,   \
                 #cond);                                                       \
         exit(1);                                                              \
      }                                                                        \
   } while (0)

static void
test_chip_routing(void)
{
   static const struct {
      enum agx_chip chip;
      enum agx_descriptor_generation generation;
      unsigned size;
   } cases[] = {
      {AGX_CHIP_G13G, AGX_DESCRIPTOR_GENERATION_7, 24},
      {AGX_CHIP_G13X, AGX_DESCRIPTOR_GENERATION_7, 24},
      {AGX_CHIP_G14G, AGX_DESCRIPTOR_GENERATION_8, 24},
      {AGX_CHIP_G14X, AGX_DESCRIPTOR_GENERATION_8, 24},
      {AGX_CHIP_G15G, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G15S, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G15C, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G15D, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G16P, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G16G, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G16S, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G16C, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G17P, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G17A, AGX_DESCRIPTOR_GENERATION_9, 32},
      {AGX_CHIP_G17G, AGX_DESCRIPTOR_GENERATION_10, 32},
      {AGX_CHIP_G17S, AGX_DESCRIPTOR_GENERATION_10, 32},
      {AGX_CHIP_G17C, AGX_DESCRIPTOR_GENERATION_10, 32},
   };

   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      const enum agx_descriptor_generation generation =
         agx_gallium_descriptor_generation(cases[i].chip);
      CHECK(generation == cases[i].generation);
      CHECK(agx_gallium_texture_descriptor_size(generation) == cases[i].size);
      CHECK(agx_gallium_pbe_descriptor_size(generation) == cases[i].size);

      const struct agx_gallium_storage_descriptor_layout storage =
         agx_gallium_storage_descriptor_layout(generation);
      CHECK(storage.texture == 0);
      CHECK(storage.pbe == cases[i].size);
      CHECK(storage.stride == 2 * cases[i].size);
   }

   CHECK(agx_gallium_descriptor_generation(AGX_CHIP_UNKNOWN) ==
         AGX_DESCRIPTOR_GENERATION_UNKNOWN);
   CHECK(agx_gallium_descriptor_generation(AGX_CHIP_G15P) ==
         AGX_DESCRIPTOR_GENERATION_UNKNOWN);
   CHECK(agx_gallium_storage_descriptor_layout(
            agx_gallium_descriptor_generation(AGX_CHIP_G15P))
            .stride == 0);
   CHECK(agx_gallium_texture_descriptor_size(
            AGX_DESCRIPTOR_GENERATION_UNKNOWN) == 0);
   CHECK(agx_gallium_pbe_descriptor_size(
            AGX_DESCRIPTOR_GENERATION_UNKNOWN) == 0);
}

static void
test_feature_gates(void)
{
   CHECK(agx_gallium_native_texture_supported(
      AGX_DESCRIPTOR_GENERATION_7, true, 99));
   CHECK(agx_gallium_native_texture_supported(
      AGX_DESCRIPTOR_GENERATION_8, true, 99));
   CHECK(agx_gallium_native_texture_supported(
      AGX_DESCRIPTOR_GENERATION_9, false, 0));
   CHECK(!agx_gallium_native_texture_supported(
      AGX_DESCRIPTOR_GENERATION_9, true, 0));
   CHECK(!agx_gallium_native_texture_supported(
      AGX_DESCRIPTOR_GENERATION_9, false, 1));
   CHECK(agx_gallium_native_texture_supported(
      AGX_DESCRIPTOR_GENERATION_10, true, 0));
   CHECK(!agx_gallium_native_texture_supported(
      AGX_DESCRIPTOR_GENERATION_10, false, 1));
   CHECK(!agx_gallium_native_texture_supported(
      AGX_DESCRIPTOR_GENERATION_UNKNOWN, false, 0));

   CHECK(agx_gallium_native_texture_sample_count_representable(
      AGX_DESCRIPTOR_GENERATION_7, 2));
   CHECK(agx_gallium_native_texture_sample_count_representable(
      AGX_DESCRIPTOR_GENERATION_8, 4));
   CHECK(!agx_gallium_native_texture_sample_count_representable(
      AGX_DESCRIPTOR_GENERATION_9, 2));
   CHECK(agx_gallium_native_texture_sample_count_representable(
      AGX_DESCRIPTOR_GENERATION_10, 8));
   CHECK(!agx_gallium_native_texture_sample_count_representable(
      AGX_DESCRIPTOR_GENERATION_10, 16));
   CHECK(!agx_gallium_native_texture_sample_count_representable(
      AGX_DESCRIPTOR_GENERATION_UNKNOWN, 1));

   CHECK(agx_gallium_native_pbe_supported(AGX_DESCRIPTOR_GENERATION_7));
   CHECK(agx_gallium_native_pbe_supported(AGX_DESCRIPTOR_GENERATION_8));
   CHECK(!agx_gallium_native_pbe_supported(AGX_DESCRIPTOR_GENERATION_9));
   CHECK(!agx_gallium_native_pbe_supported(AGX_DESCRIPTOR_GENERATION_10));
}

static void
test_native_packers(void)
{
   struct AGX_TEXTURE semantic = {AGX_TEXTURE_header};
   semantic.dimension = AGX_TEXTURE_DIMENSION_2D;
   semantic.layout = AGX_LAYOUT_TWIDDLED;
   semantic.channels = AGX_CHANNELS_R8G8B8A8;
   semantic.type = AGX_TEXTURE_TYPE_UNORM;
   semantic.swizzle_r = AGX_CHANNEL_R;
   semantic.swizzle_g = AGX_CHANNEL_G;
   semantic.swizzle_b = AGX_CHANNEL_B;
   semantic.swizzle_a = AGX_CHANNEL_A;
   semantic.width = 256;
   semantic.height = 128;
   semantic.address = 0x1000008000ull;

   struct agx_gallium_texture_descriptor native;
   struct agx_texture_packed legacy = {0};
   AGX_TEXTURE_pack(legacy.opaque, &semantic);

   memset(&native, 0xa5, sizeof(native));
   CHECK(agx_gallium_pack_native_texture(
      AGX_DESCRIPTOR_GENERATION_7, &native, &semantic));
   CHECK(memcmp(&native, &legacy, sizeof(legacy)) == 0);
   CHECK(native.opaque[6] == 0xa5a5a5a5 && native.opaque[7] == 0xa5a5a5a5);

   memset(&native, 0xa5, sizeof(native));
   CHECK(agx_gallium_pack_native_texture(
      AGX_DESCRIPTOR_GENERATION_8, &native, &semantic));
   CHECK(memcmp(&native, &legacy, sizeof(legacy)) == 0);
   CHECK(native.opaque[6] == 0xa5a5a5a5 && native.opaque[7] == 0xa5a5a5a5);

   const struct AGX_TEXTURE null_legacy_semantic = {
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
   struct agx_texture_packed expected_null_texture = {0};
   AGX_TEXTURE_pack(expected_null_texture.opaque, &null_legacy_semantic);

   for (enum agx_descriptor_generation generation =
           AGX_DESCRIPTOR_GENERATION_7;
        generation <= AGX_DESCRIPTOR_GENERATION_8; ++generation) {
      memset(&native, 0xa5, sizeof(native));
      CHECK(agx_gallium_set_null_native_texture(generation, &native));
      CHECK(memcmp(&native, &expected_null_texture,
                   sizeof(expected_null_texture)) == 0);
      CHECK(native.opaque[6] == 0xa5a5a5a5 &&
            native.opaque[7] == 0xa5a5a5a5);
   }

   memset(&native, 0xa5, sizeof(native));
   CHECK(agx_gallium_pack_native_texture(
      AGX_DESCRIPTOR_GENERATION_9, &native, &semantic));
   CHECK(native.opaque[6] == 0 && native.opaque[7] == 0);

   CHECK(agx_gallium_pack_native_texture(
      AGX_DESCRIPTOR_GENERATION_10, &native, &semantic));
   CHECK(native.opaque[6] == 0 && native.opaque[7] == 0);

   struct agx_gallium_texture_descriptor null9 = {0}, null9_dirty;
   struct agx_gallium_texture_descriptor null10 = {0}, null10_dirty;
   struct AGX_TEXTURE_GEN9 null9_unpacked = {0};
   struct AGX_TEXTURE_GEN10 null10_unpacked = {0};

   CHECK(agx_gallium_set_null_native_texture(
      AGX_DESCRIPTOR_GENERATION_9, &null9));
   memset(&null9_dirty, 0xa5, sizeof(null9_dirty));
   CHECK(agx_gallium_set_null_native_texture(
      AGX_DESCRIPTOR_GENERATION_9, &null9_dirty));
   CHECK(memcmp(&null9, &null9_dirty, sizeof(null9)) == 0);
   CHECK(AGX_TEXTURE_GEN9_unpack(NULL, (const uint8_t *)null9.opaque,
                                 &null9_unpacked));
   CHECK(null9_unpacked.width == 1 && null9_unpacked.height == 1 &&
         null9_unpacked.depth == 1);
   CHECK(null9_unpacked.address == AGX_ZERO_PAGE_ADDRESS);
   CHECK(null9.opaque[6] == 0 && null9.opaque[7] == 0);

   CHECK(agx_gallium_set_null_native_texture(
      AGX_DESCRIPTOR_GENERATION_10, &null10));
   memset(&null10_dirty, 0xa5, sizeof(null10_dirty));
   CHECK(agx_gallium_set_null_native_texture(
      AGX_DESCRIPTOR_GENERATION_10, &null10_dirty));
   CHECK(memcmp(&null10, &null10_dirty, sizeof(null10)) == 0);
   CHECK(AGX_TEXTURE_GEN10_unpack(NULL, (const uint8_t *)null10.opaque,
                                  &null10_unpacked));
   CHECK(null10_unpacked.width == 1 && null10_unpacked.height == 1 &&
         null10_unpacked.depth == 1);
   CHECK(null10_unpacked.address == AGX_ZERO_PAGE_ADDRESS);
   CHECK(null10.opaque[6] == 0 && null10.opaque[7] == 0);

   struct AGX_TEXTURE unsupported = semantic;
   unsupported.dimension = AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED;
   memset(&native, 0xff, sizeof(native));
   CHECK(!agx_gallium_pack_native_texture(
      AGX_DESCRIPTOR_GENERATION_9, &native, &unsupported));
   CHECK(native.opaque[0] == 0 && native.opaque[7] == 0);

   unsupported = semantic;
   unsupported.dimension = AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED;
   unsupported.samples = AGX_SAMPLE_COUNT_8;
   CHECK(agx_gallium_pack_native_texture(
      AGX_DESCRIPTOR_GENERATION_10, &native, &unsupported));
   uint64_t m5_msaa_word0;
   memcpy(&m5_msaa_word0, native.opaque, sizeof(m5_msaa_word0));
   CHECK(((m5_msaa_word0 >> 58) & 3) == AGX_SAMPLE_COUNT_8);

   unsupported.samples = (enum agx_sample_count)3;
   memset(&native, 0xff, sizeof(native));
   CHECK(!agx_gallium_pack_native_texture(
      AGX_DESCRIPTOR_GENERATION_10, &native, &unsupported));
   CHECK(native.opaque[0] == 0 && native.opaque[7] == 0);

   unsupported = semantic;
   unsupported.buffer_offset_sw = 1;
   memset(&native, 0xff, sizeof(native));
   CHECK(!agx_gallium_pack_native_texture(
      AGX_DESCRIPTOR_GENERATION_10, &native, &unsupported));
   CHECK(native.opaque[0] == 0 && native.opaque[7] == 0);

   struct agx_pbe_packed pbe = {.opaque = {1, 2, 3, 4, 5, 6}};
   struct agx_gallium_pbe_descriptor native_pbe;
   memset(&native_pbe, 0xa5, sizeof(native_pbe));
   CHECK(agx_gallium_pack_native_pbe(
      AGX_DESCRIPTOR_GENERATION_8, &native_pbe, &pbe));
   CHECK(memcmp(&native_pbe, &pbe, sizeof(pbe)) == 0);
   CHECK(native_pbe.opaque[6] == 0xa5a5a5a5 &&
         native_pbe.opaque[7] == 0xa5a5a5a5);

   struct agx_pbe_packed expected_null_pbe;
   agx_pack(&expected_null_pbe, PBE, cfg) {
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

   memset(&native_pbe, 0xa5, sizeof(native_pbe));
   CHECK(agx_gallium_set_null_native_pbe(
      AGX_DESCRIPTOR_GENERATION_7, &native_pbe));
   CHECK(memcmp(&native_pbe, &expected_null_pbe, sizeof(expected_null_pbe)) ==
         0);
   CHECK(native_pbe.opaque[6] == 0xa5a5a5a5 &&
         native_pbe.opaque[7] == 0xa5a5a5a5);

   memset(&native_pbe, 0xff, sizeof(native_pbe));
   CHECK(!agx_gallium_pack_native_pbe(
      AGX_DESCRIPTOR_GENERATION_10, &native_pbe, &pbe));
   CHECK(native_pbe.opaque[0] == 0 && native_pbe.opaque[7] == 0);
}

int
main(void)
{
   test_chip_routing();
   test_feature_gates();
   test_native_packers();
   printf("Gallium AGX native descriptors: %u checks passed\n", checks);
   return 0;
}
