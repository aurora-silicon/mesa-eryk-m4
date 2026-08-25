/* Copyright 2026 Aurora Silicon */
/* SPDX-License-Identifier: MIT */

#include "../agx_ioto_profile_core.h"

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

static int
round_trip(enum agx_chip chip, uint16_t payload0, uint16_t payload1)
{
   struct agx_ioto_profile_control input = {
      .chip = chip,
      .payload0 = payload0,
      .payload1 = payload1,
   };
   uint8_t bytes[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
   CHECK(agx_ioto_profile_control_pack(&input, bytes));

   const size_t expected_size =
      chip == AGX_CHIP_G17G || chip == AGX_CHIP_G17S || chip == AGX_CHIP_G17C
         ? 6
         : 4;
   CHECK(agx_ioto_profile_control_size(chip) == expected_size);
   CHECK(bytes[0] == (expected_size == 6 ? 0x07 : 0xf7));
   CHECK(bytes[1] == (expected_size == 6 ? 0x00 : 0x03));
   CHECK(bytes[2] == (uint8_t)payload0);
   CHECK(bytes[3] == (uint8_t)(payload0 >> 8));
   CHECK(bytes[4] == (expected_size == 6 ? (uint8_t)payload1 : 0));
   CHECK(bytes[5] == (expected_size == 6 ? (uint8_t)(payload1 >> 8) : 0));

   uint8_t stream[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE + 2];
   memcpy(stream, bytes, sizeof(bytes));
   stream[6] = 0xa5;
   stream[7] = 0x5a;
   struct agx_ioto_profile_control decoded;
   size_t consumed;
   CHECK(agx_ioto_profile_control_unpack(chip, stream, sizeof(stream), &decoded,
                                         &consumed));
   CHECK(consumed == expected_size);
   CHECK(decoded.chip == chip);
   CHECK(decoded.payload0 == payload0);
   CHECK(decoded.payload1 == payload1);
   return 0;
}

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
      for (unsigned value = 0; value <= UINT16_MAX; ++value) {
         if (round_trip(targets[t], value, hal300 ? 0xa55a : 0))
            return 1;
         if (hal300 && round_trip(targets[t], 0x5aa5, value))
            return 1;
      }

      struct agx_ioto_profile_control input = {
         .chip = targets[t],
         .payload0 = 0x1234,
         .payload1 = hal300 ? 0xabcd : 0,
      };
      uint8_t bytes[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
      CHECK(agx_ioto_profile_control_pack(&input, bytes));
      const size_t size = agx_ioto_profile_control_size(targets[t]);

      for (size_t truncated = 0; truncated < size; ++truncated) {
         struct agx_ioto_profile_control decoded;
         size_t consumed = 99;
         CHECK(!agx_ioto_profile_control_unpack(targets[t], bytes, truncated,
                                                &decoded, &consumed));
         CHECK(consumed == 0);
      }

      for (unsigned bit = 0; bit < 16; ++bit) {
         uint8_t mutated[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
         memcpy(mutated, bytes, sizeof(mutated));
         mutated[bit / 8] ^= (uint8_t)(1u << (bit % 8));
         struct agx_ioto_profile_control decoded;
         size_t consumed;
         CHECK(!agx_ioto_profile_control_unpack(targets[t], mutated, size,
                                                &decoded, &consumed));
      }
   }

   const enum agx_chip rejected[] = {
      AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
      AGX_CHIP_G15P, AGX_CHIP_G16P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   };
   for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
      struct agx_ioto_profile_control input = {
         .chip = rejected[i],
         .payload0 = 1,
      };
      uint8_t bytes[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
      memset(bytes, 0xff, sizeof(bytes));
      CHECK(!agx_ioto_profile_control_supported(rejected[i]));
      CHECK(agx_ioto_profile_control_size(rejected[i]) == 0);
      CHECK(!agx_ioto_profile_control_pack(&input, bytes));
      for (unsigned b = 0; b < sizeof(bytes); ++b)
         CHECK(bytes[b] == 0);
   }

   struct agx_ioto_profile_control classic_bad = {
      .chip = AGX_CHIP_G16G,
      .payload0 = 1,
      .payload1 = 1,
   };
   uint8_t bytes[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
   CHECK(!agx_ioto_profile_control_pack(&classic_bad, bytes));
   CHECK(!agx_ioto_profile_control_pack(NULL, bytes));
   CHECK(!agx_ioto_profile_control_pack(&classic_bad, NULL));
   CHECK(!agx_ioto_profile_control_unpack(AGX_CHIP_G15G, NULL, 4, NULL, NULL));

   printf("AGX3 Ioto profile core: %u checks passed\n", checks);
   return 0;
}
