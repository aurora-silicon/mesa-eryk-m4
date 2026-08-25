/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "hk_agx3_ioto_profile_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                  \
         return 1;                                                             \
      }                                                                        \
   } while (0)

int
main(void)
{
   const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool hal300 = targets[t] == AGX_CHIP_G17G ||
                          targets[t] == AGX_CHIP_G17S ||
                          targets[t] == AGX_CHIP_G17C;
      struct hk_agx3_ioto_profile_encoding encoded;
      const uint16_t payload0_in = hal300 ? 0x9088 : 0x0234;
      const uint16_t payload1_in = hal300 ? 0xa800 : 0;
      CHECK(hk_agx3_ioto_profile_generate(targets[t], payload0_in, payload1_in,
                                          &encoded));
      CHECK(encoded.length == (hal300 ? 6 : 4));
      const uint8_t expected[] = {
         hal300 ? 0x07 : 0xf7, hal300 ? 0x00 : 0x03,
         hal300 ? 0x88 : 0x34, hal300 ? 0x9f : 0x02,
         hal300 ? 0x00 : 0x00, hal300 ? 0xa8 : 0x00,
      };
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);

      uint16_t payload0, payload1;
      CHECK(hk_agx3_ioto_profile_decode(targets[t], encoded.bytes,
                                        encoded.length, &payload0, &payload1));
      CHECK(payload0 == (hal300 ? 0x9088 : 0x0234));
      CHECK(payload1 == (hal300 ? 0xa800 : 0));
      for (size_t truncated = 0; truncated < encoded.length; ++truncated) {
         CHECK(!hk_agx3_ioto_profile_decode(targets[t], encoded.bytes,
                                            truncated, &payload0, &payload1));
      }
      CHECK(!hk_agx3_ioto_profile_decode(
         targets[t], encoded.bytes, encoded.length + 1, &payload0, &payload1));
   }

   const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G14X, AGX_CHIP_G15P,
      AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      struct hk_agx3_ioto_profile_encoding encoded;
      memset(&encoded, 0xff, sizeof(encoded));
      CHECK(!hk_agx3_ioto_profile_generate(rejected[i], 0, 0, &encoded));
      CHECK(encoded.length == 0);
      for (unsigned b = 0; b < sizeof(encoded.bytes); ++b)
         CHECK(encoded.bytes[b] == 0);
   }

   struct hk_agx3_ioto_profile_encoding encoded;
   CHECK(!hk_agx3_ioto_profile_generate(AGX_CHIP_G15G, 0, 1, &encoded));
   CHECK(!hk_agx3_ioto_profile_generate(AGX_CHIP_G17S, 1, 0, &encoded));
   CHECK(!hk_agx3_ioto_profile_generate(AGX_CHIP_G17S, 0, 0, NULL));
   CHECK(!hk_agx3_ioto_profile_decode(AGX_CHIP_G17S, NULL, 6, NULL, NULL));

   printf("Honeykrisp AGX3 Ioto profile: %u checks passed\n", checks);
   return 0;
}
