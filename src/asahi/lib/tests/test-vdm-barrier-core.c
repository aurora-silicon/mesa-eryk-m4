/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_vdm_barrier_core.h"

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
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
   AGX_CHIP_G17G, AGX_CHIP_G17S, AGX_CHIP_G17C,
};

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X,    AGX_CHIP_G14G,
   AGX_CHIP_G14X, AGX_CHIP_G15P,    AGX_CHIP_G16P,
   AGX_CHIP_G17A, AGX_CHIP_UNKNOWN, (enum agx_chip)999,
};

static int
check_zero_encoding(const struct agx_vdm_barrier_core *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_witness(void)
{
   static const uint8_t expected[AGX_VDM_BARRIER_CORE_LENGTH] = {
      0x60,
      0x09,
      0x00,
      0x21,
   };

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      struct agx_vdm_barrier_core encoded;

      CHECK(agx_vdm_barrier_core_supported(supported_chips[i]));
      CHECK(agx_vdm_barrier_core_pack(supported_chips[i], &encoded));
      CHECK(encoded.length == sizeof(expected));
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(agx_vdm_barrier_core_unpack(supported_chips[i], expected,
                                        sizeof(expected)));
   }

   return 0;
}

static int
check_every_record_bit(void)
{
   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      struct agx_vdm_barrier_core baseline;
      CHECK(agx_vdm_barrier_core_pack(supported_chips[i], &baseline));

      for (unsigned bit = 0; bit < 32; ++bit) {
         uint8_t changed[AGX_VDM_BARRIER_CORE_LENGTH];
         memcpy(changed, baseline.bytes, sizeof(changed));
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
         CHECK(!agx_vdm_barrier_core_unpack(supported_chips[i], changed,
                                            sizeof(changed)));
      }
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   static const uint8_t witness[AGX_VDM_BARRIER_CORE_LENGTH] = {
      0x60,
      0x09,
      0x00,
      0x21,
   };
   static const uint8_t aggregate_multi_agc[AGX_VDM_BARRIER_CORE_LENGTH] = {
      0x7c,
      0x09,
      0x00,
      0x21,
   };
   struct agx_vdm_barrier_core encoded;

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      CHECK(!agx_vdm_barrier_core_supported(unsupported_chips[i]));
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_barrier_core_pack(unsupported_chips[i], &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      CHECK(!agx_vdm_barrier_core_unpack(unsupported_chips[i], witness,
                                         sizeof(witness)));
   }

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      CHECK(!agx_vdm_barrier_core_unpack(
         supported_chips[i], aggregate_multi_agc, sizeof(aggregate_multi_agc)));
   }

   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!agx_vdm_barrier_core_pack((enum agx_chip)999, &encoded));
   if (check_zero_encoding(&encoded))
      return 1;
   CHECK(!agx_vdm_barrier_core_pack(AGX_CHIP_G17S, NULL));

   for (size_t size = 0; size < sizeof(witness); ++size)
      CHECK(!agx_vdm_barrier_core_unpack(AGX_CHIP_G17S, witness, size));

   const size_t trailing_sizes[] = {5, 8, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      CHECK(!agx_vdm_barrier_core_unpack(AGX_CHIP_G17S, witness,
                                         trailing_sizes[i]));
   }

   uint8_t unaligned[AGX_VDM_BARRIER_CORE_LENGTH + 1];
   memcpy(&unaligned[1], witness, sizeof(witness));
   CHECK(agx_vdm_barrier_core_unpack(AGX_CHIP_G17S, &unaligned[1],
                                     sizeof(witness)));

   uint8_t appended[AGX_VDM_BARRIER_CORE_LENGTH + 1];
   memcpy(appended, witness, sizeof(witness));
   appended[sizeof(witness)] = 0xa5;
   CHECK(
      !agx_vdm_barrier_core_unpack(AGX_CHIP_G17S, appended, sizeof(appended)));
   CHECK(!agx_vdm_barrier_core_unpack(AGX_CHIP_G17S, NULL, sizeof(witness)));

   return 0;
}

int
main(void)
{
   if (check_exact_witness() || check_every_record_bit() ||
       check_rejections_and_clearing())
      return 1;

   printf("agx_vdm_barrier_core: %u checks passed\n", checks);
   return 0;
}
