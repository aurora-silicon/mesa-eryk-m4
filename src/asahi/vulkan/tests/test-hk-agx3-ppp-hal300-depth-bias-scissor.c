/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ppp_hal300_depth_bias_scissor.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      checks++;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                  \
         return 1;                                                             \
      }                                                                        \
   } while (0)

static int
check_exact_shared_bridge(void)
{
   const struct agx_ppp_hal300_depth_bias_record_fields depth = {
      UINT32_C(0x3f800000),
      UINT32_C(0xc0200000),
      UINT32_C(0x3eaaaaab),
   };
   const struct agx_ppp_hal300_scissor_record_fields scissor = {
      UINT16_C(0x1122), UINT16_C(0x3344),     UINT16_C(0x5566),
      UINT16_C(0x7788), UINT32_C(0x3dcccccd), UINT32_C(0x3f666666),
   };
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields bases = {
      UINT64_C(0x0000123456789abc),
      UINT64_C(0x0000abcdef012340),
   };
   static const uint8_t expected_depth[] = {
      0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x20, 0xc0, 0xab, 0xaa, 0xaa, 0x3e,
   };
   static const uint8_t expected_scissor[] = {
      0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77,
      0xcd, 0xcc, 0xcc, 0x3d, 0x66, 0x66, 0x66, 0x3f,
   };
   static const uint8_t expected_bases[] = {
      0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x34, 0x12, 0xbc, 0x9a,
      0x78, 0x56, 0x00, 0x00, 0xcd, 0xab, 0x40, 0x23, 0x01, 0xef,
   };
   const enum agx_chip chips[] = {
      AGX_CHIP_G17G,
      AGX_CHIP_G17S,
      AGX_CHIP_G17C,
   };

   for (unsigned i = 0; i < sizeof(chips) / sizeof(chips[0]); ++i) {
      struct agx_ppp_hal300_depth_bias_record direct_depth;
      struct agx_ppp_hal300_depth_bias_record bridged_depth;
      struct agx_ppp_hal300_scissor_record direct_scissor;
      struct agx_ppp_hal300_scissor_record bridged_scissor;
      struct agx_ppp_hal300_depth_bias_scissor_bases direct_bases;
      struct agx_ppp_hal300_depth_bias_scissor_bases bridged_bases;
      struct agx_ppp_hal300_depth_bias_record_fields decoded_depth;
      struct agx_ppp_hal300_scissor_record_fields decoded_scissor;
      struct agx_ppp_hal300_depth_bias_scissor_bases_fields decoded_bases;

      CHECK(agx_ppp_hal300_depth_bias_record_pack(chips[i], &depth,
                                                  &direct_depth));
      CHECK(hk_agx3_encode_ppp_hal300_depth_bias_record(chips[i], &depth,
                                                        &bridged_depth));
      CHECK(bridged_depth.length == sizeof(expected_depth));
      CHECK(memcmp(bridged_depth.bytes, expected_depth,
                   sizeof(expected_depth)) == 0);
      CHECK(memcmp(bridged_depth.bytes, direct_depth.bytes,
                   direct_depth.length) == 0);
      CHECK(hk_agx3_decode_ppp_hal300_depth_bias_record(
         chips[i], expected_depth, sizeof(expected_depth), &decoded_depth));
      CHECK(memcmp(&decoded_depth, &depth, sizeof(depth)) == 0);

      CHECK(agx_ppp_hal300_scissor_record_pack(chips[i], &scissor,
                                               &direct_scissor));
      CHECK(hk_agx3_encode_ppp_hal300_scissor_record(chips[i], &scissor,
                                                     &bridged_scissor));
      CHECK(bridged_scissor.length == sizeof(expected_scissor));
      CHECK(memcmp(bridged_scissor.bytes, expected_scissor,
                   sizeof(expected_scissor)) == 0);
      CHECK(memcmp(bridged_scissor.bytes, direct_scissor.bytes,
                   direct_scissor.length) == 0);
      CHECK(hk_agx3_decode_ppp_hal300_scissor_record(chips[i], expected_scissor,
                                                     sizeof(expected_scissor),
                                                     &decoded_scissor));
      CHECK(memcmp(&decoded_scissor, &scissor, sizeof(scissor)) == 0);

      CHECK(agx_ppp_hal300_depth_bias_scissor_bases_pack(chips[i], &bases,
                                                         &direct_bases));
      CHECK(hk_agx3_encode_ppp_hal300_depth_bias_scissor_bases(chips[i], &bases,
                                                               &bridged_bases));
      CHECK(bridged_bases.length == sizeof(expected_bases));
      CHECK(memcmp(bridged_bases.bytes, expected_bases,
                   sizeof(expected_bases)) == 0);
      CHECK(memcmp(bridged_bases.bytes, direct_bases.bytes,
                   direct_bases.length) == 0);
      CHECK(hk_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
         chips[i], expected_bases, sizeof(expected_bases), &decoded_bases));
      CHECK(decoded_bases.depth_bias_base == bases.depth_bias_base);
      CHECK(decoded_bases.scissor_base == bases.scissor_base);
   }
   return 0;
}

static int
check_bridge_rejections(void)
{
   const struct agx_ppp_hal300_depth_bias_record_fields depth = {1, 2, 3};
   const struct agx_ppp_hal300_scissor_record_fields scissor = {
      1, 2, 3, 4, 5, 6,
   };
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields bases = {
      UINT64_C(0x1000),
      UINT64_C(0x2000),
   };
   struct agx_ppp_hal300_depth_bias_record depth_payload;
   struct agx_ppp_hal300_scissor_record scissor_payload;
   struct agx_ppp_hal300_depth_bias_scissor_bases bases_payload;
   CHECK(hk_agx3_encode_ppp_hal300_depth_bias_record(AGX_CHIP_G17G, &depth,
                                                     &depth_payload));
   CHECK(hk_agx3_encode_ppp_hal300_scissor_record(AGX_CHIP_G17G, &scissor,
                                                  &scissor_payload));
   CHECK(hk_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, &bases, &bases_payload));

   struct agx_ppp_hal300_depth_bias_record_fields decoded_depth;
   struct agx_ppp_hal300_scissor_record_fields decoded_scissor;
   struct agx_ppp_hal300_depth_bias_scissor_bases_fields decoded_bases;
   memset(&decoded_depth, 0xa5, sizeof(decoded_depth));
   memset(&decoded_scissor, 0xa5, sizeof(decoded_scissor));
   memset(&decoded_bases, 0xa5, sizeof(decoded_bases));
   CHECK(!hk_agx3_decode_ppp_hal300_depth_bias_record(
      AGX_CHIP_G16G, depth_payload.bytes, depth_payload.length,
      &decoded_depth));
   CHECK(!hk_agx3_decode_ppp_hal300_scissor_record(
      AGX_CHIP_G16G, scissor_payload.bytes, scissor_payload.length,
      &decoded_scissor));
   CHECK(!hk_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G16G, bases_payload.bytes, bases_payload.length,
      &decoded_bases));
   CHECK(decoded_depth.depth_bias_raw == 0);
   CHECK(decoded_depth.slope_scale_raw == 0);
   CHECK(decoded_depth.clamp_raw == 0);
   CHECK(decoded_scissor.max_x == 0);
   CHECK(decoded_scissor.min_x == 0);
   CHECK(decoded_scissor.max_y == 0);
   CHECK(decoded_scissor.min_y == 0);
   CHECK(decoded_scissor.min_z_raw == 0);
   CHECK(decoded_scissor.max_z_raw == 0);
   CHECK(decoded_bases.depth_bias_base == 0);
   CHECK(decoded_bases.scissor_base == 0);

   uint8_t malformed[AGX_PPP_HAL300_DEPTH_BIAS_SCISSOR_BASES_LENGTH];
   memcpy(malformed, bases_payload.bytes, sizeof(malformed));
   malformed[1] ^= 1;
   CHECK(!hk_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, malformed, sizeof(malformed), &decoded_bases));
   memcpy(malformed, bases_payload.bytes, sizeof(malformed));
   malformed[12] = 1;
   CHECK(!hk_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, malformed, sizeof(malformed), &decoded_bases));

   struct agx_ppp_hal300_depth_bias_scissor_bases invalid_payload;
   struct agx_ppp_hal300_depth_bias_scissor_bases_fields invalid_bases = {
      UINT64_C(0x1000),
      UINT64_C(0x2001),
   };
   CHECK(!hk_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, &invalid_bases, &invalid_payload));
   invalid_bases.scissor_base = UINT64_C(0x0001000000000000);
   CHECK(!hk_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, &invalid_bases, &invalid_payload));

   CHECK(!hk_agx3_decode_ppp_hal300_depth_bias_record(
      AGX_CHIP_G17G, depth_payload.bytes, depth_payload.length - 1u,
      &decoded_depth));
   CHECK(!hk_agx3_decode_ppp_hal300_scissor_record(
      AGX_CHIP_G17G, scissor_payload.bytes, scissor_payload.length + 1u,
      &decoded_scissor));
   CHECK(!hk_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, bases_payload.bytes, bases_payload.length - 1u,
      &decoded_bases));
   CHECK(!hk_agx3_encode_ppp_hal300_depth_bias_record(AGX_CHIP_G17G, NULL,
                                                      &depth_payload));
   CHECK(!hk_agx3_encode_ppp_hal300_scissor_record(AGX_CHIP_G17G, NULL,
                                                   &scissor_payload));
   CHECK(!hk_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, NULL, &bases_payload));
   CHECK(!hk_agx3_decode_ppp_hal300_depth_bias_record(
      AGX_CHIP_G17G, NULL, sizeof(depth_payload.bytes), &decoded_depth));
   CHECK(!hk_agx3_decode_ppp_hal300_scissor_record(
      AGX_CHIP_G17G, NULL, sizeof(scissor_payload.bytes), &decoded_scissor));
   CHECK(!hk_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, NULL, sizeof(bases_payload.bytes), &decoded_bases));
   return 0;
}

int
main(void)
{
   if (check_exact_shared_bridge() || check_bridge_rejections())
      return 1;

   printf("hk_agx3_ppp_hal300_depth_bias_scissor: %u checks passed\n", checks);
   return 0;
}
