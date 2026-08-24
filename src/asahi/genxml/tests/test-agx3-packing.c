/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_pack.h"
#include "asahi/genxml/agx3_cmdstream.h"

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
   hal200.lod_bias = -1.015625f;
   hal200.border_colour = AGX_BORDER_COLOUR_OPAQUE_WHITE;
   hal200.seamful_cube_maps = true;
   AGX_SAMPLER_HAL200_pack(packed_hal200, &hal200);

   gen5.maximum_lod = 100.0f;
   gen5.lod_bias = -1.015625f;
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
   gen5.lod_bias = -100.0f;
   AGX_SAMPLER_GEN5_pack(packed_gen5, &gen5);
   CHECK_EQ((word64(packed_gen5, 0) >> 39) & 3,
            AGX_SAMPLER_REDUCTION_MODE_MAXIMUM);
   CHECK_EQ((word64(packed_gen5, 0) >> 43) & 0x7ff, 0x400);

   memset(packed_gen5, 0, sizeof(packed_gen5));
   gen5.lod_bias = 100.0f;
   AGX_SAMPLER_GEN5_pack(packed_gen5, &gen5);
   CHECK_EQ((word64(packed_gen5, 0) >> 43) & 0x7ff, 0x3ff);
}

static void
test_agx3_cdm_launch(void)
{
   const enum agx_chip grounded[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint8_t expected[AGX3_CDM_LAUNCH_SIZE] = {
      0x67, 0x45, 0x23, 0x01, 0xde, 0xbc, 0x0a, 0x01,
      0x23, 0x01, 0xef, 0x8d, 0xef, 0xbe, 0xad, 0xde,
      0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
      0x07, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
   };

   struct agx3_cdm_launch_info input = {
      .word0_payload = 0x01234567,
      .word1_payload = 0x000abcde,
      .pipeline = UINT64_C(0x1237bc048c0),
      .opaque_word3 = 0xdeadbeef,
      .global_size = {3, 5, 7},
      .local_size = {8, 16, 32},
   };

   for (unsigned i = 0; i < sizeof(grounded) / sizeof(grounded[0]); ++i) {
      uint8_t packed[AGX3_CDM_LAUNCH_SIZE];
      input.chip = grounded[i];
      CHECK_EQ(agx3_cdm_launch_target_grounded(input.chip), true);
      CHECK_EQ(agx3_cdm_launch_pack(&input, packed), true);
      ++checks;
      if (memcmp(packed, expected, sizeof(expected))) {
         fprintf(stderr, "AGX3 CDM launch mismatch for chip %u\n", input.chip);
         for (unsigned b = 0; b < sizeof(packed); ++b)
            fprintf(stderr, "%02x%s", packed[b],
                    b + 1 == sizeof(packed) ? "\n" : " ");
         exit(1);
      }

      struct agx3_cdm_launch_info decoded;
      CHECK_EQ(agx3_cdm_launch_unpack(input.chip, packed, sizeof(packed),
                                      &decoded),
               true);
      CHECK_EQ(decoded.chip, input.chip);
      CHECK_EQ(decoded.word0_payload, input.word0_payload);
      CHECK_EQ(decoded.word1_payload, input.word1_payload);
      CHECK_EQ(decoded.pipeline, input.pipeline);
      CHECK_EQ(decoded.opaque_word3, input.opaque_word3);
      for (unsigned axis = 0; axis < 3; ++axis) {
         CHECK_EQ(decoded.global_size[axis], input.global_size[axis]);
         CHECK_EQ(decoded.local_size[axis], input.local_size[axis]);
      }
   }

   const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A,
      AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      uint8_t packed[AGX3_CDM_LAUNCH_SIZE];
      input.chip = rejected[i];
      CHECK_EQ(agx3_cdm_launch_target_grounded(input.chip), false);
      CHECK_EQ(agx3_cdm_launch_pack(&input, packed), false);
      for (unsigned b = 0; b < sizeof(packed); ++b)
         CHECK_EQ(packed[b], 0);
   }

   uint8_t packed[AGX3_CDM_LAUNCH_SIZE];
   input.chip = AGX_CHIP_G17S;
   input.pipeline |= 1;
   CHECK_EQ(agx3_cdm_launch_pack(&input, packed), false);
   input.pipeline &= ~UINT64_C(0x3f);
   input.pipeline = UINT64_C(1) << 48;
   CHECK_EQ(agx3_cdm_launch_pack(&input, packed), false);
   input.pipeline = UINT64_C(0x1237bc048c0);
   input.word0_payload = UINT32_C(1) << 29;
   CHECK_EQ(agx3_cdm_launch_pack(&input, packed), false);
   input.word0_payload = 0x01234567;
   input.word1_payload = UINT32_C(1) << 22;
   CHECK_EQ(agx3_cdm_launch_pack(&input, packed), false);
   input.word1_payload = 0x000abcde;
   input.local_size[2] = 0x800;
   CHECK_EQ(agx3_cdm_launch_pack(&input, packed), false);
   input.local_size[2] = 32;
   CHECK_EQ(agx3_cdm_launch_pack(NULL, packed), false);
   CHECK_EQ(agx3_cdm_launch_pack(&input, NULL), false);

   CHECK_EQ(agx3_cdm_launch_pack(&input, packed), true);
   struct agx3_cdm_launch_info decoded;
   CHECK_EQ(agx3_cdm_launch_unpack(input.chip, packed, 39, &decoded), false);
   packed[3] |= 0x20;
   CHECK_EQ(agx3_cdm_launch_unpack(input.chip, packed, sizeof(packed),
                                   &decoded),
            false);
   CHECK_EQ(decoded.pipeline, 0);
}

static void
test_agx3_cdm_stream_link(void)
{
   const enum agx_chip grounded[] = {
      AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D, AGX_CHIP_G16S,
      AGX_CHIP_G16C, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint8_t expected[AGX3_CDM_STREAM_LINK_SIZE] = {
      0xbc, 0x9a, 0x00, 0x30, 0x78, 0x56, 0x34, 0x12,
   };
   struct agx3_cdm_stream_link_info input = {
      .target = UINT64_C(0x9abc12345678),
      .with_return = true,
   };

   for (unsigned i = 0; i < sizeof(grounded) / sizeof(grounded[0]); ++i) {
      uint8_t packed[AGX3_CDM_STREAM_LINK_SIZE];
      input.chip = grounded[i];
      CHECK_EQ(agx3_cdm_stream_link_target_grounded(input.chip), true);
      CHECK_EQ(agx3_cdm_stream_link_pack(&input, packed), true);
      ++checks;
      if (memcmp(packed, expected, sizeof(expected))) {
         fprintf(stderr, "AGX3 CDM link mismatch for chip %u\n", input.chip);
         for (unsigned b = 0; b < sizeof(packed); ++b)
            fprintf(stderr, "%02x%s", packed[b],
                    b + 1 == sizeof(packed) ? "\n" : " ");
         exit(1);
      }

      struct agx3_cdm_stream_link_info decoded;
      CHECK_EQ(agx3_cdm_stream_link_unpack(input.chip, packed, sizeof(packed),
                                           &decoded),
               true);
      CHECK_EQ(decoded.target, input.target);
      CHECK_EQ(decoded.with_return, input.with_return);
   }

   const enum agx_chip rejected[] = {
      AGX_CHIP_G15G, AGX_CHIP_G16G, AGX_CHIP_G16P, AGX_CHIP_G17P,
      AGX_CHIP_G17A, AGX_CHIP_G17G, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      uint8_t packed[AGX3_CDM_STREAM_LINK_SIZE];
      input.chip = rejected[i];
      CHECK_EQ(agx3_cdm_stream_link_target_grounded(input.chip), false);
      CHECK_EQ(agx3_cdm_stream_link_pack(&input, packed), false);
      for (unsigned b = 0; b < sizeof(packed); ++b)
         CHECK_EQ(packed[b], 0);
   }

   uint8_t packed[AGX3_CDM_STREAM_LINK_SIZE];
   input.chip = AGX_CHIP_G17S;
   input.target = UINT64_C(1) << 48;
   CHECK_EQ(agx3_cdm_stream_link_pack(&input, packed), false);
   input.target = UINT64_C(0x9abc12345678);
   CHECK_EQ(agx3_cdm_stream_link_pack(NULL, packed), false);
   CHECK_EQ(agx3_cdm_stream_link_pack(&input, NULL), false);

   CHECK_EQ(agx3_cdm_stream_link_pack(&input, packed), true);
   struct agx3_cdm_stream_link_info decoded;
   CHECK_EQ(agx3_cdm_stream_link_unpack(input.chip, packed, 7, &decoded),
            false);
   packed[2] = 1;
   CHECK_EQ(agx3_cdm_stream_link_unpack(input.chip, packed, sizeof(packed),
                                        &decoded),
            false);
   CHECK_EQ(decoded.target, 0);
}

static void
test_agx3_cdm_barrier(void)
{
   const enum agx_chip pre_gen7[] = {
      AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D, AGX_CHIP_G16G,
      AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   };
   const uint8_t expected0[AGX3_CDM_BARRIER_SIZE] = {0x60, 0x01, 0x00, 0x60};
   const uint8_t expected5[AGX3_CDM_BARRIER_SIZE] = {0x60, 0x15, 0x00, 0x60};

   for (unsigned i = 0; i < sizeof(pre_gen7) / sizeof(pre_gen7[0]); ++i) {
      for (unsigned selection = 0; selection < 2; ++selection) {
         struct agx3_cdm_barrier_info info = {
            .chip = pre_gen7[i],
            .flags = selection ? 5 : 0,
         };
         uint8_t packed[AGX3_CDM_BARRIER_SIZE];
         struct agx3_cdm_barrier_info decoded;
         CHECK_EQ(agx3_cdm_barrier_pack(&info, packed), true);
         ++checks;
         if (memcmp(packed, selection ? expected5 : expected0,
                    sizeof(packed))) {
            fprintf(stderr, "AGX3 CDM pre-Gen7 barrier mismatch\n");
            exit(1);
         }
         CHECK_EQ(agx3_cdm_barrier_unpack(info.chip, packed, sizeof(packed),
                                          &decoded),
                  true);
         CHECK_EQ(decoded.chip, info.chip);
         CHECK_EQ(decoded.flags, info.flags);
      }

      for (uint8_t flags = 0; flags < 16; ++flags) {
         if (flags == 0 || flags == 5)
            continue;
         struct agx3_cdm_barrier_info rejected = {
            .chip = pre_gen7[i],
            .flags = flags,
         };
         uint8_t packed[AGX3_CDM_BARRIER_SIZE];
         CHECK_EQ(agx3_cdm_barrier_pack(&rejected, packed), false);
         CHECK_EQ(agx3_cmdstream_load_le32(packed), 0);
      }
   }

   const enum agx_chip gen7[] = {
      AGX_CHIP_G17G,
      AGX_CHIP_G17S,
      AGX_CHIP_G17C,
   };
   for (unsigned i = 0; i < sizeof(gen7) / sizeof(gen7[0]); ++i) {
      for (uint8_t flags = 0; flags < 16; ++flags) {
         struct agx3_cdm_barrier_info info = {
            .chip = gen7[i],
            .flags = flags,
         };
         uint8_t packed[AGX3_CDM_BARRIER_SIZE];
         struct agx3_cdm_barrier_info decoded;
         CHECK_EQ(agx3_cdm_barrier_pack(&info, packed), true);
         CHECK_EQ(agx3_cmdstream_load_le32(packed),
                  agx3_cdm_barrier_word(flags));
         CHECK_EQ(agx3_cdm_barrier_unpack(info.chip, packed, sizeof(packed),
                                          &decoded),
                  true);
         CHECK_EQ(decoded.flags, flags);
      }
   }

   const enum agx_chip unsupported[] = {
      AGX_CHIP_G13G, AGX_CHIP_G15P, AGX_CHIP_G15G, AGX_CHIP_G16P,
      AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
      struct agx3_cdm_barrier_info info = {
         .chip = unsupported[i],
         .flags = 0,
      };
      uint8_t packed[AGX3_CDM_BARRIER_SIZE];
      CHECK_EQ(agx3_cdm_barrier_target_grounded(info.chip), false);
      CHECK_EQ(agx3_cdm_barrier_pack(&info, packed), false);
      CHECK_EQ(agx3_cmdstream_load_le32(packed), 0);
   }

   uint8_t packed[AGX3_CDM_BARRIER_SIZE];
   struct agx3_cdm_barrier_info info = {
      .chip = AGX_CHIP_G17S,
      .flags = 0,
   };
   CHECK_EQ(agx3_cdm_barrier_pack(&info, packed), true);
   struct agx3_cdm_barrier_info decoded;
   CHECK_EQ(agx3_cdm_barrier_unpack(info.chip, packed, 3, &decoded), false);
   packed[0] |= 1;
   CHECK_EQ(agx3_cdm_barrier_unpack(info.chip, packed, sizeof(packed),
                                    &decoded),
            false);
   CHECK_EQ(decoded.flags, 0);
   CHECK_EQ(agx3_cdm_barrier_pack(NULL, packed), false);
   CHECK_EQ(agx3_cdm_barrier_pack(&info, NULL), false);
}

static void
test_agx3_cdm_stream_terminate(void)
{
   const enum agx_chip grounded[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };
   const uint8_t expected[AGX3_CDM_STREAM_TERMINATE_SIZE] = {
      0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
   };

   for (unsigned i = 0; i < sizeof(grounded) / sizeof(grounded[0]); ++i) {
      uint8_t packed[AGX3_CDM_STREAM_TERMINATE_SIZE];
      CHECK_EQ(agx3_cdm_stream_terminate_pack(grounded[i], packed), true);
      ++checks;
      if (memcmp(packed, expected, sizeof(expected))) {
         fprintf(stderr, "AGX3 CDM terminate mismatch\n");
         exit(1);
      }
      CHECK_EQ(agx3_cdm_stream_terminate_validate(
                  grounded[i], packed, sizeof(packed)),
               true);
   }

   const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A,
      AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      uint8_t packed[AGX3_CDM_STREAM_TERMINATE_SIZE];
      CHECK_EQ(agx3_cdm_stream_terminate_pack(rejected[i], packed), false);
      for (unsigned b = 0; b < sizeof(packed); ++b)
         CHECK_EQ(packed[b], 0);
   }

   uint8_t packed[AGX3_CDM_STREAM_TERMINATE_SIZE];
   CHECK_EQ(agx3_cdm_stream_terminate_pack(AGX_CHIP_G17S, packed), true);
   CHECK_EQ(agx3_cdm_stream_terminate_validate(AGX_CHIP_G17S, packed, 7),
            false);
   packed[4] = 1;
   CHECK_EQ(agx3_cdm_stream_terminate_validate(
               AGX_CHIP_G17S, packed, sizeof(packed)),
            false);
   CHECK_EQ(agx3_cdm_stream_terminate_pack(AGX_CHIP_G17S, NULL), false);
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
   test_agx3_cdm_launch();
   test_agx3_cdm_stream_link();
   test_agx3_cdm_barrier();
   test_agx3_cdm_stream_terminate();

   printf("agx3 packing: all %u checks passed\n", checks);
   return 0;
}
