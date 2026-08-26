/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_agx3_ppp_hal300_depth_bias_scissor_core.h"

#include "asahi/lib/agx_ppp_hal300_depth_bias_scissor_core.h"

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

static const enum agx_chip supported_chips[] = {
   AGX_CHIP_G17G,
   AGX_CHIP_G17S,
   AGX_CHIP_G17C,
};

static int
check_exact_shared_parity(void)
{
   const struct d12_agx3_ppp_hal300_depth_bias_record_fields depth = {
      UINT32_C(0x3f800000),
      UINT32_C(0xc0200000),
      UINT32_C(0x3eaaaaab),
   };
   const struct d12_agx3_ppp_hal300_scissor_record_fields scissor = {
      UINT16_C(0x1122), UINT16_C(0x3344),     UINT16_C(0x5566),
      UINT16_C(0x7788), UINT32_C(0x3dcccccd), UINT32_C(0x3f666666),
   };
   const struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields bases = {
      UINT64_C(0x0000123456789abc),
      UINT64_C(0x0000abcdef012340),
   };
   const struct agx_ppp_hal300_depth_bias_record_fields shared_depth = {
      UINT32_C(0x3f800000),
      UINT32_C(0xc0200000),
      UINT32_C(0x3eaaaaab),
   };
   const struct agx_ppp_hal300_scissor_record_fields shared_scissor = {
      UINT16_C(0x1122), UINT16_C(0x3344),     UINT16_C(0x5566),
      UINT16_C(0x7788), UINT32_C(0x3dcccccd), UINT32_C(0x3f666666),
   };
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields shared_bases = {
      UINT64_C(0x0000123456789abc),
      UINT64_C(0x0000abcdef012340),
   };
   static const uint8_t expected_bases[] = {
      0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x34, 0x12, 0xbc, 0x9a,
      0x78, 0x56, 0x00, 0x00, 0xcd, 0xab, 0x40, 0x23, 0x01, 0xef,
   };

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      struct d12_agx3_ppp_hal300_depth_bias_record d12_depth;
      struct d12_agx3_ppp_hal300_scissor_record d12_scissor;
      struct d12_agx3_ppp_hal300_depth_bias_scissor_bases d12_bases;
      struct agx_ppp_hal300_depth_bias_record direct_depth;
      struct agx_ppp_hal300_scissor_record direct_scissor;
      struct agx_ppp_hal300_depth_bias_scissor_bases direct_bases;
      struct d12_agx3_ppp_hal300_depth_bias_record_fields decoded_depth;
      struct d12_agx3_ppp_hal300_scissor_record_fields decoded_scissor;
      struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields decoded_bases;

      CHECK(d12_agx3_encode_ppp_hal300_depth_bias_record(supported_chips[i],
                                                         &depth, &d12_depth));
      CHECK(agx_ppp_hal300_depth_bias_record_pack(
         supported_chips[i], &shared_depth, &direct_depth));
      CHECK(d12_depth.length == direct_depth.length);
      CHECK(memcmp(d12_depth.bytes, direct_depth.bytes, direct_depth.length) ==
            0);
      CHECK(d12_agx3_decode_ppp_hal300_depth_bias_record(
         supported_chips[i], d12_depth.bytes, d12_depth.length,
         &decoded_depth));
      CHECK(memcmp(&decoded_depth, &depth, sizeof(depth)) == 0);

      CHECK(d12_agx3_encode_ppp_hal300_scissor_record(supported_chips[i],
                                                      &scissor, &d12_scissor));
      CHECK(agx_ppp_hal300_scissor_record_pack(
         supported_chips[i], &shared_scissor, &direct_scissor));
      CHECK(d12_scissor.length == direct_scissor.length);
      CHECK(memcmp(d12_scissor.bytes, direct_scissor.bytes,
                   direct_scissor.length) == 0);
      CHECK(d12_agx3_decode_ppp_hal300_scissor_record(
         supported_chips[i], d12_scissor.bytes, d12_scissor.length,
         &decoded_scissor));
      CHECK(memcmp(&decoded_scissor, &scissor, sizeof(scissor)) == 0);

      CHECK(d12_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
         supported_chips[i], &bases, &d12_bases));
      CHECK(agx_ppp_hal300_depth_bias_scissor_bases_pack(
         supported_chips[i], &shared_bases, &direct_bases));
      CHECK(d12_bases.length == sizeof(expected_bases));
      CHECK(memcmp(d12_bases.bytes, expected_bases, sizeof(expected_bases)) ==
            0);
      CHECK(memcmp(d12_bases.bytes, direct_bases.bytes, direct_bases.length) ==
            0);
      CHECK(d12_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
         supported_chips[i], expected_bases, sizeof(expected_bases),
         &decoded_bases));
      CHECK(decoded_bases.depth_bias_base == bases.depth_bias_base);
      CHECK(decoded_bases.scissor_base == bases.scissor_base);
   }
   return 0;
}

static int
check_bridge_rejections(void)
{
   const struct d12_agx3_ppp_hal300_depth_bias_record_fields depth = {1, 2, 3};
   const struct d12_agx3_ppp_hal300_scissor_record_fields scissor = {
      1, 2, 3, 4, 5, 6,
   };
   const struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields bases = {
      UINT64_C(0x1000),
      UINT64_C(0x2000),
   };
   struct d12_agx3_ppp_hal300_depth_bias_record depth_payload;
   struct d12_agx3_ppp_hal300_scissor_record scissor_payload;
   struct d12_agx3_ppp_hal300_depth_bias_scissor_bases bases_payload;
   CHECK(d12_agx3_encode_ppp_hal300_depth_bias_record(AGX_CHIP_G17G, &depth,
                                                      &depth_payload));
   CHECK(d12_agx3_encode_ppp_hal300_scissor_record(AGX_CHIP_G17G, &scissor,
                                                   &scissor_payload));
   CHECK(d12_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, &bases, &bases_payload));

   uint8_t malformed[D12_AGX3_PPP_HAL300_DEPTH_BIAS_SCISSOR_BASES_LENGTH];
   struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields decoded_bases;
   memcpy(malformed, bases_payload.bytes, sizeof(malformed));
   malformed[0] ^= 1;
   memset(&decoded_bases, 0xa5, sizeof(decoded_bases));
   CHECK(!d12_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, malformed, sizeof(malformed), &decoded_bases));
   CHECK(decoded_bases.depth_bias_base == 0);
   CHECK(decoded_bases.scissor_base == 0);

   memcpy(malformed, bases_payload.bytes, sizeof(malformed));
   malformed[4] = 1;
   CHECK(!d12_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, malformed, sizeof(malformed), &decoded_bases));
   memcpy(malformed, bases_payload.bytes, sizeof(malformed));
   malformed[16] |= 1;
   CHECK(!d12_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, malformed, sizeof(malformed), &decoded_bases));

   struct d12_agx3_ppp_hal300_depth_bias_scissor_bases invalid;
   struct d12_agx3_ppp_hal300_depth_bias_scissor_bases_fields invalid_bases = {
      UINT64_C(0x1001),
      UINT64_C(0x2000),
   };
   memset(&invalid, 0xa5, sizeof(invalid));
   CHECK(!d12_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, &invalid_bases, &invalid));
   for (unsigned i = 0; i < sizeof(invalid.bytes); ++i)
      CHECK(invalid.bytes[i] == 0);
   CHECK(invalid.length == 0);
   invalid_bases.depth_bias_base = 0;
   invalid_bases.scissor_base = UINT64_C(0x0001000000000000);
   CHECK(!d12_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, &invalid_bases, &invalid));

   struct d12_agx3_ppp_hal300_depth_bias_record bad_depth;
   struct d12_agx3_ppp_hal300_scissor_record bad_scissor;
   CHECK(!d12_agx3_encode_ppp_hal300_depth_bias_record(AGX_CHIP_G16G, &depth,
                                                       &bad_depth));
   CHECK(!d12_agx3_encode_ppp_hal300_scissor_record(AGX_CHIP_G16G, &scissor,
                                                    &bad_scissor));
   CHECK(!d12_agx3_encode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G16G, &bases, &invalid));

   struct d12_agx3_ppp_hal300_depth_bias_record_fields decoded_depth;
   struct d12_agx3_ppp_hal300_scissor_record_fields decoded_scissor;
   memset(&decoded_depth, 0xa5, sizeof(decoded_depth));
   memset(&decoded_scissor, 0xa5, sizeof(decoded_scissor));
   CHECK(!d12_agx3_decode_ppp_hal300_depth_bias_record(
      AGX_CHIP_G17G, depth_payload.bytes, depth_payload.length - 1u,
      &decoded_depth));
   CHECK(!d12_agx3_decode_ppp_hal300_scissor_record(
      AGX_CHIP_G17G, scissor_payload.bytes, scissor_payload.length + 1u,
      &decoded_scissor));
   CHECK(decoded_depth.depth_bias_raw == 0);
   CHECK(decoded_depth.slope_scale_raw == 0);
   CHECK(decoded_depth.clamp_raw == 0);
   CHECK(decoded_scissor.max_x == 0);
   CHECK(decoded_scissor.min_x == 0);
   CHECK(decoded_scissor.max_y == 0);
   CHECK(decoded_scissor.min_y == 0);
   CHECK(decoded_scissor.min_z_raw == 0);
   CHECK(decoded_scissor.max_z_raw == 0);

   CHECK(!d12_agx3_encode_ppp_hal300_depth_bias_record(AGX_CHIP_G17G, NULL,
                                                       &bad_depth));
   CHECK(!d12_agx3_encode_ppp_hal300_scissor_record(AGX_CHIP_G17G, NULL,
                                                    &bad_scissor));
   CHECK(!d12_agx3_encode_ppp_hal300_depth_bias_scissor_bases(AGX_CHIP_G17G,
                                                              NULL, &invalid));
   CHECK(!d12_agx3_decode_ppp_hal300_depth_bias_record(
      AGX_CHIP_G17G, NULL, depth_payload.length, &decoded_depth));
   CHECK(!d12_agx3_decode_ppp_hal300_scissor_record(
      AGX_CHIP_G17G, NULL, scissor_payload.length, &decoded_scissor));
   CHECK(!d12_agx3_decode_ppp_hal300_depth_bias_scissor_bases(
      AGX_CHIP_G17G, NULL, bases_payload.length, &decoded_bases));
   return 0;
}

static int
check_live_draw_record_bytes(void)
{
   const struct d12_agx3_ppp_hal300_depth_bias_record_fields depth = {
      UINT32_C(0xbe800000), UINT32_C(0x3fc00000), UINT32_C(0x3e000000),
   };
   const struct d12_agx3_ppp_hal300_scissor_record_fields scissor = {
      UINT16_C(1920), UINT16_C(16),         UINT16_C(1080),
      UINT16_C(8),    UINT32_C(0x00000000), UINT32_C(0x3f800000),
   };
   static const uint8_t expected_depth[] = {
      0x00, 0x00, 0x80, 0xbe, 0x00, 0x00, 0xc0, 0x3f, 0x00, 0x00, 0x00, 0x3e,
   };
   static const uint8_t expected_scissor[] = {
      0x80, 0x07, 0x10, 0x00, 0x38, 0x04, 0x08, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
   };

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      struct d12_agx3_ppp_hal300_depth_bias_record packed_depth;
      struct d12_agx3_ppp_hal300_scissor_record packed_scissor;
      CHECK(d12_agx3_encode_ppp_hal300_depth_bias_record(
         supported_chips[i], &depth, &packed_depth));
      CHECK(packed_depth.length == sizeof(expected_depth));
      CHECK(memcmp(packed_depth.bytes, expected_depth,
                   sizeof(expected_depth)) == 0);
      CHECK(d12_agx3_encode_ppp_hal300_scissor_record(
         supported_chips[i], &scissor, &packed_scissor));
      CHECK(packed_scissor.length == sizeof(expected_scissor));
      CHECK(memcmp(packed_scissor.bytes, expected_scissor,
                   sizeof(expected_scissor)) == 0);
   }
   return 0;
}

int
main(void)
{
   if (check_exact_shared_parity() || check_live_draw_record_bytes() ||
       check_bridge_rejections())
      return 1;

   printf("d12_agx3_ppp_hal300_depth_bias_scissor_core: %u checks passed\n",
          checks);
   return 0;
}
