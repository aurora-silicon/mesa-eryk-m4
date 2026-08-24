/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_vdm_stream_link_core.h"

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
check_zero_encoding(const struct agx_vdm_stream_link_core *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_witness(void)
{
   static const uint8_t expected[AGX_VDM_STREAM_LINK_CORE_LENGTH] = {
      0x12, 0x00, 0x00, 0x80, 0x9a, 0x78, 0x56, 0x34,
   };

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      struct agx_vdm_stream_link_core encoded;
      uint64_t target = UINT64_MAX;

      CHECK(agx_vdm_stream_link_core_supported(supported_chips[i]));
      CHECK(agx_vdm_stream_link_core_pack(supported_chips[i],
                                          UINT64_C(0x123456789a), &encoded));
      CHECK(encoded.length == sizeof(expected));
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(agx_vdm_stream_link_core_unpack(supported_chips[i], expected,
                                            sizeof(expected), &target));
      CHECK(target == UINT64_C(0x123456789a));
   }

   return 0;
}

static int
check_target_domain(void)
{
   static const uint64_t targets[] = {
      0,
      1,
      UINT64_C(0x123456789a),
      UINT64_C(0xffffffffff),
   };

   for (unsigned c = 0;
        c < sizeof(supported_chips) / sizeof(supported_chips[0]); ++c) {
      for (unsigned t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
         struct agx_vdm_stream_link_core encoded;
         uint64_t decoded = UINT64_MAX;

         CHECK(agx_vdm_stream_link_core_pack(supported_chips[c], targets[t],
                                             &encoded));
         CHECK(encoded.length == AGX_VDM_STREAM_LINK_CORE_LENGTH);
         CHECK(agx_vdm_stream_link_core_unpack(
            supported_chips[c], encoded.bytes, encoded.length, &decoded));
         CHECK(decoded == targets[t]);
      }
   }

   /* Every address bit is modeled; no alignment restriction is invented. */
   for (unsigned bit = 0; bit < 40; ++bit) {
      struct agx_vdm_stream_link_core encoded;
      uint64_t decoded = 0;
      const uint64_t target = UINT64_C(1) << bit;

      CHECK(agx_vdm_stream_link_core_pack(AGX_CHIP_G17S, target, &encoded));
      CHECK(agx_vdm_stream_link_core_unpack(AGX_CHIP_G17S, encoded.bytes,
                                            encoded.length, &decoded));
      CHECK(decoded == target);
   }

   return 0;
}

static int
check_every_record_bit(void)
{
   struct agx_vdm_stream_link_core baseline;
   CHECK(agx_vdm_stream_link_core_pack(AGX_CHIP_G15G, 0, &baseline));

   for (unsigned bit = 0; bit < 64; ++bit) {
      uint8_t changed[AGX_VDM_STREAM_LINK_CORE_LENGTH];
      uint64_t target = UINT64_MAX;

      memcpy(changed, baseline.bytes, sizeof(changed));
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

      if (bit < 8 || bit >= 32) {
         struct agx_vdm_stream_link_core repacked;
         const uint64_t expected =
            bit < 8 ? UINT64_C(1) << (bit + 32) : UINT64_C(1) << (bit - 32);

         CHECK(agx_vdm_stream_link_core_unpack(AGX_CHIP_G15G, changed,
                                               sizeof(changed), &target));
         CHECK(target == expected);
         CHECK(agx_vdm_stream_link_core_pack(AGX_CHIP_G15G, target, &repacked));
         CHECK(memcmp(changed, repacked.bytes, sizeof(changed)) == 0);
      } else {
         /* Bits 8..27 are reserved, bit 28 is the unmodeled return form, and
          * bits 29..31 are the fixed Stream Link block type. */
         CHECK(!agx_vdm_stream_link_core_unpack(AGX_CHIP_G15G, changed,
                                                sizeof(changed), &target));
         CHECK(target == 0);
      }
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   static const uint8_t witness[AGX_VDM_STREAM_LINK_CORE_LENGTH] = {
      0x12, 0x00, 0x00, 0x80, 0x9a, 0x78, 0x56, 0x34,
   };
   struct agx_vdm_stream_link_core encoded;

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      uint64_t target = UINT64_MAX;

      CHECK(!agx_vdm_stream_link_core_supported(unsupported_chips[i]));
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_stream_link_core_pack(unsupported_chips[i],
                                           UINT64_C(0x123456789a), &encoded));
      if (check_zero_encoding(&encoded))
         return 1;

      CHECK(!agx_vdm_stream_link_core_unpack(unsupported_chips[i], witness,
                                             sizeof(witness), &target));
      CHECK(target == 0);
   }

   static const uint64_t invalid_targets[] = {
      UINT64_C(1) << 40,
      (UINT64_C(1) << 40) | 1,
      UINT64_MAX,
   };
   for (unsigned i = 0;
        i < sizeof(invalid_targets) / sizeof(invalid_targets[0]); ++i) {
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_vdm_stream_link_core_pack(AGX_CHIP_G17S, invalid_targets[i],
                                           &encoded));
      if (check_zero_encoding(&encoded))
         return 1;
   }

   CHECK(!agx_vdm_stream_link_core_pack(AGX_CHIP_G17S, 0, NULL));

   for (size_t size = 0; size < sizeof(witness); ++size) {
      uint64_t target = UINT64_MAX;
      CHECK(!agx_vdm_stream_link_core_unpack(AGX_CHIP_G17S, witness, size,
                                             &target));
      CHECK(target == 0);
   }

   const size_t trailing_sizes[] = {9, 16, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      uint64_t target = UINT64_MAX;
      CHECK(!agx_vdm_stream_link_core_unpack(AGX_CHIP_G17S, witness,
                                             trailing_sizes[i], &target));
      CHECK(target == 0);
   }

   uint8_t unaligned[AGX_VDM_STREAM_LINK_CORE_LENGTH + 1];
   memcpy(&unaligned[1], witness, sizeof(witness));
   uint64_t target = UINT64_MAX;
   CHECK(agx_vdm_stream_link_core_unpack(AGX_CHIP_G17S, &unaligned[1],
                                         sizeof(witness), &target));
   CHECK(target == UINT64_C(0x123456789a));

   uint8_t appended[AGX_VDM_STREAM_LINK_CORE_LENGTH + 1];
   memcpy(appended, witness, sizeof(witness));
   appended[sizeof(witness)] = 0xa5;
   target = UINT64_MAX;
   CHECK(!agx_vdm_stream_link_core_unpack(AGX_CHIP_G17S, appended,
                                          sizeof(appended), &target));
   CHECK(target == 0);

   target = UINT64_MAX;
   CHECK(!agx_vdm_stream_link_core_unpack(AGX_CHIP_G17S, NULL, sizeof(witness),
                                          &target));
   CHECK(target == 0);
   CHECK(!agx_vdm_stream_link_core_unpack(AGX_CHIP_G17S, witness,
                                          sizeof(witness), NULL));

   return 0;
}

int
main(void)
{
   if (check_exact_witness() || check_target_domain() ||
       check_every_record_bit() || check_rejections_and_clearing())
      return 1;

   printf("agx_vdm_stream_link_core: %u checks passed\n", checks);
   return 0;
}
