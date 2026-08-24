/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_vdm_stream_terminate_core.h"

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

struct terminate_witness {
   enum agx_chip chip;
   uint32_t constructor_offset;
   uint32_t emit_offset;
};

/*
 * G15/G16/G17P constructors copy the pair at +0x7500 and the emitter reads
 * +0x7504.  HAL300 constructors use +0x74f0/+0x74f4.  The aggregate family
 * plugins directly serve the listed S/C/D identities.
 */
static const struct terminate_witness supported_witnesses[] = {
   {AGX_CHIP_G15G, 0x7500, 0x7504}, {AGX_CHIP_G15S, 0x7500, 0x7504},
   {AGX_CHIP_G15C, 0x7500, 0x7504}, {AGX_CHIP_G15D, 0x7500, 0x7504},
   {AGX_CHIP_G16G, 0x7500, 0x7504}, {AGX_CHIP_G16S, 0x7500, 0x7504},
   {AGX_CHIP_G16C, 0x7500, 0x7504}, {AGX_CHIP_G17P, 0x7500, 0x7504},
   {AGX_CHIP_G17G, 0x74f0, 0x74f4}, {AGX_CHIP_G17S, 0x74f0, 0x74f4},
   {AGX_CHIP_G17C, 0x74f0, 0x74f4},
};

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X,    AGX_CHIP_G14G,
   AGX_CHIP_G14X, AGX_CHIP_G15P,    AGX_CHIP_G16P,
   AGX_CHIP_G17A, AGX_CHIP_UNKNOWN, (enum agx_chip)999,
};

static int
check_zero_encoding(const struct agx_vdm_stream_terminate_core *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_constructor_witnesses(void)
{
   static const uint8_t constructor_pair[8] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0,
   };
   static const uint8_t expected[AGX_VDM_STREAM_TERMINATE_CORE_LENGTH] = {
      0x00,
      0x00,
      0x00,
      0xc0,
   };

   CHECK(AGX_VDM_STREAM_TERMINATE_CORE_WORD == UINT32_C(0xc0000000));
   for (unsigned i = 0;
        i < sizeof(supported_witnesses) / sizeof(supported_witnesses[0]); ++i) {
      const struct terminate_witness *witness = &supported_witnesses[i];
      struct agx_vdm_stream_terminate_core encoded;

      CHECK(witness->emit_offset == witness->constructor_offset + 4);
      CHECK(agx_vdm_stream_terminate_core_supported(witness->chip));
      CHECK(agx_vdm_stream_terminate_core_pack(witness->chip, &encoded));
      CHECK(encoded.length == sizeof(expected));
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(memcmp(encoded.bytes, &constructor_pair[4], sizeof(expected)) == 0);
      CHECK(agx_vdm_stream_terminate_core_unpack(
         witness->chip, &constructor_pair[4], sizeof(expected)));
   }

   return 0;
}

static int
check_every_record_bit(void)
{
   for (unsigned i = 0;
        i < sizeof(supported_witnesses) / sizeof(supported_witnesses[0]); ++i) {
      struct agx_vdm_stream_terminate_core baseline;
      CHECK(agx_vdm_stream_terminate_core_pack(supported_witnesses[i].chip,
                                               &baseline));

      for (unsigned bit = 0; bit < 32; ++bit) {
         uint8_t changed[AGX_VDM_STREAM_TERMINATE_CORE_LENGTH];
         memcpy(changed, baseline.bytes, sizeof(changed));
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
         CHECK(!agx_vdm_stream_terminate_core_unpack(
            supported_witnesses[i].chip, changed, sizeof(changed)));
      }
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   static const uint8_t witness[AGX_VDM_STREAM_TERMINATE_CORE_LENGTH] = {
      0x00,
      0x00,
      0x00,
      0xc0,
   };
   static const uint8_t
      ungrounded_return[AGX_VDM_STREAM_TERMINATE_CORE_LENGTH] = {
         0x00,
         0x00,
         0x00,
         0xe0,
      };
   struct agx_vdm_stream_terminate_core encoded;

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      CHECK(!agx_vdm_stream_terminate_core_supported(unsupported_chips[i]));
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(
         !agx_vdm_stream_terminate_core_pack(unsupported_chips[i], &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
      CHECK(!agx_vdm_stream_terminate_core_unpack(unsupported_chips[i], witness,
                                                  sizeof(witness)));
   }

   for (unsigned i = 0;
        i < sizeof(supported_witnesses) / sizeof(supported_witnesses[0]); ++i) {
      CHECK(!agx_vdm_stream_terminate_core_unpack(supported_witnesses[i].chip,
                                                  ungrounded_return,
                                                  sizeof(ungrounded_return)));
   }

   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!agx_vdm_stream_terminate_core_pack((enum agx_chip)999, &encoded));
   if (check_zero_encoding(&encoded))
      return 1;
   CHECK(!agx_vdm_stream_terminate_core_pack(AGX_CHIP_G17S, NULL));

   for (size_t size = 0; size < sizeof(witness); ++size)
      CHECK(
         !agx_vdm_stream_terminate_core_unpack(AGX_CHIP_G17S, witness, size));

   const size_t trailing_sizes[] = {5, 8, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      CHECK(!agx_vdm_stream_terminate_core_unpack(AGX_CHIP_G17S, witness,
                                                  trailing_sizes[i]));
   }

   uint8_t unaligned[AGX_VDM_STREAM_TERMINATE_CORE_LENGTH + 1];
   memcpy(&unaligned[1], witness, sizeof(witness));
   CHECK(agx_vdm_stream_terminate_core_unpack(AGX_CHIP_G17S, &unaligned[1],
                                              sizeof(witness)));

   uint8_t appended[AGX_VDM_STREAM_TERMINATE_CORE_LENGTH + 1];
   memcpy(appended, witness, sizeof(witness));
   appended[sizeof(witness)] = 0xa5;
   CHECK(!agx_vdm_stream_terminate_core_unpack(AGX_CHIP_G17S, appended,
                                               sizeof(appended)));
   CHECK(!agx_vdm_stream_terminate_core_unpack(AGX_CHIP_G17S, NULL,
                                               sizeof(witness)));

   return 0;
}

int
main(void)
{
   if (check_exact_constructor_witnesses() || check_every_record_bit() ||
       check_rejections_and_clearing())
      return 1;

   printf("agx_vdm_stream_terminate_core: %u checks passed\n", checks);
   return 0;
}
