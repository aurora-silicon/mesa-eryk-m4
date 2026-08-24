/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_agx3_ppp_w_clamp_cull2_core.h"

#include "asahi/lib/agx_ppp_w_clamp_cull2_core.h"

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

_Static_assert(sizeof(((struct d12_agx3_ppp_w_clamp_cull2 *)0)->bytes) ==
                  sizeof(((struct agx_ppp_w_clamp_cull2_core *)0)->bytes),
               "D3D and shared PPP W-clamp/Cull-2 test storage must agree");

static int
check_fields(const struct d12_agx3_ppp_w_clamp_cull2_fields *fields,
             uint32_t w_clamp_raw, uint32_t cull2_raw)
{
   CHECK(fields->w_clamp_raw == w_clamp_raw);
   CHECK(fields->cull2_raw == cull2_raw);
   return 0;
}

static int
check_zero_fields(const struct d12_agx3_ppp_w_clamp_cull2_fields *fields)
{
   return check_fields(fields, 0, 0);
}

static int
check_zero_encoding(const struct d12_agx3_ppp_w_clamp_cull2 *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_shared_equality_and_raw_payloads(void)
{
   static const uint32_t patterns[] = {
      0,
      UINT32_MAX,
      UINT32_C(0x80000000),
      UINT32_C(0x7fc00001),
      UINT32_C(0xdeadbeef),
      UINT32_C(0x01234567),
   };

   for (unsigned target = 0;
        target < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++target) {
      for (unsigned w = 0; w < sizeof(patterns) / sizeof(patterns[0]); ++w) {
         for (unsigned c = 0; c < sizeof(patterns) / sizeof(patterns[0]); ++c) {
            const struct d12_agx3_ppp_w_clamp_cull2_fields fields = {
               .w_clamp_raw = patterns[w],
               .cull2_raw = patterns[c],
            };
            const struct agx_ppp_w_clamp_cull2_fields shared_fields = {
               .w_clamp_raw = patterns[w],
               .cull2_raw = patterns[c],
            };
            struct d12_agx3_ppp_w_clamp_cull2 encoded;
            struct d12_agx3_ppp_w_clamp_cull2_fields decoded;
            struct agx_ppp_w_clamp_cull2_core shared;

            CHECK(d12_agx3_encode_ppp_w_clamp_cull2(
               supported_chips[target], &fields, &encoded));
            CHECK(agx_ppp_w_clamp_cull2_core_pack(
               supported_chips[target], &shared_fields, &shared));
            CHECK(encoded.length == shared.length);
            CHECK(memcmp(encoded.bytes, shared.bytes, shared.length) == 0);
            CHECK(d12_agx3_decode_ppp_w_clamp_cull2(
               supported_chips[target], shared.bytes, shared.length, &decoded));
            if (check_fields(&decoded, patterns[w], patterns[c]))
               return 1;
         }
      }
   }

   return 0;
}

static int
check_exact_witnesses(void)
{
   static const uint8_t expected_gen6[] = {
      0x00, 0x00, 0x41, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
   };
   static const uint8_t expected_gen7[] = {
      0x02, 0x00, 0x84, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
   };
   const struct d12_agx3_ppp_w_clamp_cull2_fields fields = {
      .cull2_raw = UINT32_C(0x80),
   };

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      const bool gen7 = i >= 8;
      const uint8_t *expected = gen7 ? expected_gen7 : expected_gen6;
      struct d12_agx3_ppp_w_clamp_cull2 encoded;
      struct d12_agx3_ppp_w_clamp_cull2_fields decoded;

      CHECK(d12_agx3_encode_ppp_w_clamp_cull2(supported_chips[i], &fields,
                                              &encoded));
      CHECK(encoded.length == sizeof(expected_gen6));
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected_gen6)) == 0);
      CHECK(d12_agx3_decode_ppp_w_clamp_cull2(
         supported_chips[i], expected, sizeof(expected_gen6), &decoded));
      if (check_fields(&decoded, 0, UINT32_C(0x80)))
         return 1;
   }

   return 0;
}

static int
check_every_serialized_bit(void)
{
   const struct d12_agx3_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = UINT32_C(0x13579bdf),
      .cull2_raw = UINT32_C(0x2468ace0),
   };

   for (unsigned target = 0;
        target < sizeof(supported_chips) / sizeof(supported_chips[0]);
        ++target) {
      struct d12_agx3_ppp_w_clamp_cull2 baseline;
      CHECK(d12_agx3_encode_ppp_w_clamp_cull2(supported_chips[target], &fields,
                                              &baseline));

      for (unsigned bit = 0; bit < baseline.length * 8u; ++bit) {
         uint8_t changed[D12_AGX3_PPP_W_CLAMP_CULL2_LENGTH];
         struct d12_agx3_ppp_w_clamp_cull2_fields decoded;
         memcpy(changed, baseline.bytes, sizeof(changed));
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

         if (bit < 32) {
            memset(&decoded, 0xa5, sizeof(decoded));
            CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
               supported_chips[target], changed, sizeof(changed), &decoded));
            if (check_zero_fields(&decoded))
               return 1;
         } else {
            struct d12_agx3_ppp_w_clamp_cull2 repacked;
            CHECK(d12_agx3_decode_ppp_w_clamp_cull2(
               supported_chips[target], changed, sizeof(changed), &decoded));
            CHECK(d12_agx3_encode_ppp_w_clamp_cull2(
               supported_chips[target], &decoded, &repacked));
            CHECK(memcmp(repacked.bytes, changed, sizeof(changed)) == 0);
         }
      }
   }

   return 0;
}

static int
check_generation_and_target_rejections(void)
{
   const struct d12_agx3_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = UINT32_C(0x11223344),
      .cull2_raw = UINT32_C(0x55667788),
   };
   struct d12_agx3_ppp_w_clamp_cull2 gen6;
   struct d12_agx3_ppp_w_clamp_cull2 gen7;
   CHECK(d12_agx3_encode_ppp_w_clamp_cull2(AGX_CHIP_G15G, &fields, &gen6));
   CHECK(d12_agx3_encode_ppp_w_clamp_cull2(AGX_CHIP_G17S, &fields, &gen7));

   for (unsigned old = 0; old < 8; ++old) {
      for (unsigned newer = 8;
           newer < sizeof(supported_chips) / sizeof(supported_chips[0]);
           ++newer) {
         struct d12_agx3_ppp_w_clamp_cull2_fields decoded;
         memset(&decoded, 0xa5, sizeof(decoded));
         CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
            supported_chips[old], gen7.bytes, gen7.length, &decoded));
         if (check_zero_fields(&decoded))
            return 1;

         memset(&decoded, 0xa5, sizeof(decoded));
         CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
            supported_chips[newer], gen6.bytes, gen6.length, &decoded));
         if (check_zero_fields(&decoded))
            return 1;
      }
   }

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct d12_agx3_ppp_w_clamp_cull2 encoded;
      struct d12_agx3_ppp_w_clamp_cull2_fields decoded;
      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!d12_agx3_encode_ppp_w_clamp_cull2(unsupported_chips[i], &fields,
                                               &encoded));
      if (check_zero_encoding(&encoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
         unsupported_chips[i], gen6.bytes, gen6.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
         unsupported_chips[i], gen7.bytes, gen7.length, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   return 0;
}

static int
check_sizes_null_unaligned_and_clearing(void)
{
   const struct d12_agx3_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = UINT32_C(0x11223344),
      .cull2_raw = UINT32_C(0x55667788),
   };
   struct d12_agx3_ppp_w_clamp_cull2 witness;
   struct d12_agx3_ppp_w_clamp_cull2 encoded;
   CHECK(d12_agx3_encode_ppp_w_clamp_cull2(AGX_CHIP_G17S, &fields, &witness));

   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!d12_agx3_encode_ppp_w_clamp_cull2(AGX_CHIP_G17S, NULL, &encoded));
   if (check_zero_encoding(&encoded))
      return 1;
   CHECK(!d12_agx3_encode_ppp_w_clamp_cull2(AGX_CHIP_G17S, &fields, NULL));

   for (uint32_t size = 0; size < witness.length; ++size) {
      struct d12_agx3_ppp_w_clamp_cull2_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
         AGX_CHIP_G17S, witness.bytes, size, &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   const uint32_t trailing_sizes[] = {witness.length + 1u, 64u, UINT32_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      struct d12_agx3_ppp_w_clamp_cull2_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
         AGX_CHIP_G17S, witness.bytes, trailing_sizes[i], &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   uint8_t unaligned[D12_AGX3_PPP_W_CLAMP_CULL2_LENGTH + 1];
   memcpy(&unaligned[1], witness.bytes, witness.length);
   struct d12_agx3_ppp_w_clamp_cull2_fields decoded;
   CHECK(d12_agx3_decode_ppp_w_clamp_cull2(
      AGX_CHIP_G17S, &unaligned[1], witness.length, &decoded));
   if (check_fields(&decoded, fields.w_clamp_raw, fields.cull2_raw))
      return 1;

   uint8_t appended[D12_AGX3_PPP_W_CLAMP_CULL2_LENGTH + 1];
   memcpy(appended, witness.bytes, witness.length);
   appended[witness.length] = 0xa5;
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
      AGX_CHIP_G17S, appended, sizeof(appended), &decoded));
   if (check_zero_fields(&decoded))
      return 1;

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
      AGX_CHIP_G17S, NULL, witness.length, &decoded));
   if (check_zero_fields(&decoded))
      return 1;
   CHECK(!d12_agx3_decode_ppp_w_clamp_cull2(
      AGX_CHIP_G17S, witness.bytes, witness.length, NULL));
   return 0;
}

int
main(void)
{
   if (check_shared_equality_and_raw_payloads() || check_exact_witnesses() ||
       check_every_serialized_bit() ||
       check_generation_and_target_rejections() ||
       check_sizes_null_unaligned_and_clearing())
      return 1;

   printf("d12_agx3_ppp_w_clamp_cull2_core: %u checks passed\n", checks);
   return 0;
}
