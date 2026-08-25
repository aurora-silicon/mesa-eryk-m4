/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_raytracing_core.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;

#define CHECK(condition)                                                       \
   do {                                                                        \
      ++checks;                                                                \
      if (!(condition)) {                                                      \
         fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,    \
                 #condition);                                                  \
         return 1;                                                             \
      }                                                                        \
   } while (0)

static const enum agx_chip supported_chips[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15D,
   AGX_CHIP_G16P, AGX_CHIP_G16G, AGX_CHIP_G16S,
   AGX_CHIP_G17P, AGX_CHIP_G17G, AGX_CHIP_G17S,
};

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
   AGX_CHIP_G15P, AGX_CHIP_G15C, AGX_CHIP_G16C, AGX_CHIP_G17A,
   AGX_CHIP_G17C, AGX_CHIP_UNKNOWN, (enum agx_chip)999,
};

int
main(void)
{
   static const uint16_t registers_b[] = {0, 2, 4, 6, 8, 10};
   static const uint8_t bytes_b[][AGX3_CONTINUE_RAY_MAX_LENGTH] = {
      {0x5f, 0x11, 0x54, 0x00, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x02, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x04, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x06, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x08, 0x00, 0x00},
      {0x5f, 0x11, 0x54, 0x0a, 0x00, 0x00},
   };
   static const uint16_t registers_c[] = {0, 2, 8, 10, 12, 16, 18};
   static const uint8_t bytes_c[][AGX3_CONTINUE_RAY_MAX_LENGTH] = {
      {0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x02},
      {0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x42},
      {0x27, 0x04, 0x09, 0x00, 0x32, 0xc0, 0xaf, 0x02},
      {0x27, 0x04, 0x09, 0x00, 0x32, 0xc0, 0xaf, 0x42},
      {0x27, 0x04, 0x09, 0x00, 0x3a, 0xc0, 0xaf, 0x02},
      {0x27, 0x04, 0x09, 0x00, 0xa2, 0xc0, 0xaf, 0x02},
      {0x27, 0x04, 0x09, 0x00, 0xa2, 0xc0, 0xaf, 0x42},
   };

   for (unsigned c = 0;
        c < sizeof(supported_chips) / sizeof(supported_chips[0]); ++c) {
      const enum agx_chip chip = supported_chips[c];
      const enum agx_isa_encoding encoding =
         agx3_continue_ray_witnessed_chip_encoding(chip);
      const uint16_t *registers = encoding == AGX_ISA_ENCODING_AGX3B
                                     ? registers_b : registers_c;
      const uint8_t (*bytes)[AGX3_CONTINUE_RAY_MAX_LENGTH] =
         encoding == AGX_ISA_ENCODING_AGX3B ? bytes_b : bytes_c;
      const unsigned count = encoding == AGX_ISA_ENCODING_AGX3B
                                ? sizeof(registers_b) / sizeof(registers_b[0])
                                : sizeof(registers_c) / sizeof(registers_c[0]);
      const uint8_t length = encoding == AGX_ISA_ENCODING_AGX3B ? 6 : 8;

      for (unsigned v = 0; v < count; ++v) {
         struct hk_agx3_continue_ray_instruction generated;
         memset(&generated, 0xff, sizeof(generated));
         CHECK(hk_agx3_continue_ray_instruction_generate(
            chip, registers[v], &generated));
         CHECK(generated.length == length);
         CHECK(memcmp(generated.bytes, bytes[v], length) == 0);
         for (unsigned i = length; i < sizeof(generated.bytes); ++i)
            CHECK(generated.bytes[i] == 0);

         uint16_t decoded = UINT16_MAX;
         CHECK(hk_agx3_continue_ray_instruction_decode(
            chip, generated.bytes, generated.length, &decoded));
         CHECK(decoded == registers[v]);
      }
   }

   for (unsigned c = 0;
        c < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++c) {
      struct hk_agx3_continue_ray_instruction rejected;
      memset(&rejected, 0xff, sizeof(rejected));
      CHECK(!hk_agx3_continue_ray_instruction_generate(
         unsupported_chips[c], 0, &rejected));
      CHECK(rejected.length == 0);
      for (unsigned i = 0; i < sizeof(rejected.bytes); ++i)
         CHECK(rejected.bytes[i] == 0);
      uint16_t decoded = 0;
      CHECK(!hk_agx3_continue_ray_instruction_decode(
         unsupported_chips[c], bytes_c[0], 8, &decoded));
   }

   struct hk_agx3_continue_ray_instruction rejected;
   memset(&rejected, 0xff, sizeof(rejected));
   CHECK(!hk_agx3_continue_ray_instruction_generate(AGX_CHIP_G15G, 12,
                                                     &rejected));
   CHECK(rejected.length == 0);
   memset(&rejected, 0xff, sizeof(rejected));
   CHECK(!hk_agx3_continue_ray_instruction_generate(AGX_CHIP_G17G, 4,
                                                     &rejected));
   CHECK(rejected.length == 0);

   uint16_t decoded = 0;
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G15G, bytes_c[0],
                                                   8, &decoded));
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, bytes_b[0],
                                                   6, &decoded));
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, bytes_c[0],
                                                   7, &decoded));
   uint8_t appended[AGX3_CONTINUE_RAY_MAX_LENGTH + 1];
   memcpy(appended, bytes_c[0], 8);
   appended[8] = 0;
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, appended,
                                                   sizeof(appended),
                                                   &decoded));

   uint8_t malformed[AGX3_CONTINUE_RAY_MAX_LENGTH];
   memcpy(malformed, bytes_c[2], sizeof(malformed));
   malformed[1] ^= 0x80;
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, malformed,
                                                   sizeof(malformed),
                                                   &decoded));
   CHECK(!hk_agx3_continue_ray_instruction_generate(AGX_CHIP_G17G, 0, NULL));
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, NULL, 8,
                                                   &decoded));
   CHECK(!hk_agx3_continue_ray_instruction_decode(AGX_CHIP_G17G, bytes_c[0],
                                                   8, NULL));

   printf("hk_agx3_raytracing_core: PASS (%u checks)\n", checks);
   return 0;
}
