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

static bool
profile_clear(const struct agx_ioto_profile_control *profile)
{
   const uint8_t *bytes = (const uint8_t *)profile;
   for (size_t i = 0; i < sizeof(*profile); ++i) {
      if (bytes[i] != 0)
         return false;
   }

   return true;
}

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
   const uint16_t encoded_payload0 =
      payload0 | (expected_size == 6 ? UINT16_C(0x0f00) : 0);
   CHECK(bytes[2] == (uint8_t)encoded_payload0);
   CHECK(bytes[3] == (uint8_t)(encoded_payload0 >> 8));
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
   static const uint8_t g17s_minimal_loader[] = {
      0x1c, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x34, 0x81, 0x01, 0x00, 0x20, 0x00, 0xa0, 0x02,
      0x0c, 0x80, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x27, 0x04, 0x04, 0x1a, 0x21, 0x00, 0xa3, 0x02,
      0x88, 0x80, 0x00, 0x00,
      0x1c, 0x80, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00,
      0x27, 0x04, 0x04, 0x00, 0x30, 0x00, 0xa4, 0x12,
   };
   uint8_t loader[sizeof(g17s_minimal_loader)];
   memset(loader, 0xa5, sizeof(loader));
   CHECK(agx_ioto_g17s_minimal_profile_loader_pack(loader, sizeof(loader)));
   CHECK(memcmp(loader, g17s_minimal_loader, sizeof(loader)) == 0);
   loader[0] = 0x5a;
   CHECK(!agx_ioto_g17s_minimal_profile_loader_pack(
      loader, sizeof(loader) - 1));
   CHECK(loader[0] == 0x5a);
   CHECK(!agx_ioto_g17s_minimal_profile_loader_pack(NULL, sizeof(loader)));

   const struct {
      uint8_t value;
      bool mode_table_bit;
      bool high_bit;
      const uint8_t bytes[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
   } direct_profiles[] = {
      {1, false, false, {0x07, 0x00, 0x08, 0x0f, 0x00, 0x00}},
      {1, false, true,  {0x07, 0x00, 0x08, 0x1f, 0x00, 0x00}},
      {2, false, false, {0x07, 0x00, 0x10, 0x0f, 0x00, 0x00}},
      {1, true,  false, {0x07, 0x00, 0x88, 0x0f, 0x00, 0x00}},
   };
   for (unsigned i = 0;
        i < sizeof(direct_profiles) / sizeof(direct_profiles[0]); ++i) {
      struct agx_ioto_profile_control profile;
      uint8_t bytes[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
      CHECK(agx_ioto_g17s_profile_control_build(
         direct_profiles[i].value, direct_profiles[i].mode_table_bit,
         direct_profiles[i].high_bit, &profile));
      CHECK(agx_ioto_profile_control_pack(&profile, bytes));
      CHECK(memcmp(bytes, direct_profiles[i].bytes, sizeof(bytes)) == 0);
   }
   struct agx_ioto_profile_control invalid_profile = {.chip = AGX_CHIP_G15G};
   CHECK(!agx_ioto_g17s_profile_control_build(4, false, false,
                                               &invalid_profile));
   CHECK(invalid_profile.chip == AGX_CHIP_G15G);
   CHECK(!agx_ioto_g17s_profile_control_build(0, false, false, NULL));

   const enum agx_chip targets[] = {
      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
      AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
      AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
   };

   for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
      const bool hal300 = targets[t] == AGX_CHIP_G17G ||
                          targets[t] == AGX_CHIP_G17S ||
                          targets[t] == AGX_CHIP_G17C;
      if (hal300) {
         for (unsigned value0 = 0; value0 < 128; ++value0) {
            const uint16_t payload0 = (uint16_t)(
               ((value0 & 0x03) << 3) | ((value0 & 0x04) << 5) |
               ((value0 & 0x78) << 9));
            for (unsigned value1 = 0; value1 < 64; ++value1) {
               if (round_trip(targets[t], payload0,
                              (uint16_t)(value1 << 10)))
                  return 1;
            }
         }
      } else {
         for (unsigned value = 0; value <= 0x07ff; ++value) {
            if (round_trip(targets[t], value, 0))
               return 1;
         }
      }

      struct agx_ioto_profile_control input = {
         .chip = targets[t],
         .payload0 = hal300 ? 0x9088 : 0x0234,
         .payload1 = hal300 ? 0xa800 : 0,
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
         memset(&decoded, 0xff, sizeof(decoded));
         size_t consumed = 99;
         CHECK(!agx_ioto_profile_control_unpack(targets[t], mutated, size,
                                                &decoded, &consumed));
         CHECK(profile_clear(&decoded));
         CHECK(consumed == 0);
      }

      for (unsigned bit = 16; bit < size * 8; ++bit) {
         const unsigned word = bit / 16;
         const uint16_t bit_mask = UINT16_C(1) << (bit % 16);
         const uint16_t operand_mask =
            !hal300 ? UINT16_C(0x07ff)
                    : (word == 1 ? UINT16_C(0xf098) : UINT16_C(0xfc00));
         if (bit_mask & operand_mask)
            continue;

         uint8_t mutated[AGX_IOTO_PROFILE_CONTROL_MAX_SIZE];
         memcpy(mutated, bytes, sizeof(mutated));
         mutated[bit / 8] ^= (uint8_t)(1u << (bit % 8));
         struct agx_ioto_profile_control decoded;
         memset(&decoded, 0xff, sizeof(decoded));
         size_t consumed = 99;
         CHECK(!agx_ioto_profile_control_unpack(targets[t], mutated, size,
                                                &decoded, &consumed));
         CHECK(profile_clear(&decoded));
         CHECK(consumed == 0);
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
   const struct agx_ioto_profile_control classic_reserved = {
      .chip = AGX_CHIP_G16G,
      .payload0 = 0x0800,
   };
   CHECK(!agx_ioto_profile_control_pack(&classic_reserved, bytes));
   const struct agx_ioto_profile_control hal300_reserved0 = {
      .chip = AGX_CHIP_G17S,
      .payload0 = 0x0001,
   };
   CHECK(!agx_ioto_profile_control_pack(&hal300_reserved0, bytes));
   const struct agx_ioto_profile_control hal300_reserved1 = {
      .chip = AGX_CHIP_G17S,
      .payload1 = 0x0001,
   };
   CHECK(!agx_ioto_profile_control_pack(&hal300_reserved1, bytes));
   CHECK(!agx_ioto_profile_control_pack(NULL, bytes));
   CHECK(!agx_ioto_profile_control_pack(&classic_bad, NULL));
   CHECK(!agx_ioto_profile_control_unpack(AGX_CHIP_G15G, NULL, 4, NULL, NULL));

   printf("AGX3 Ioto profile core: %u checks passed\n", checks);
   return 0;
}
