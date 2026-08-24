/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_ppp_w_clamp_cull2_core.h"

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

struct supported_target {
   enum agx_chip chip;
   uint32_t header;
};

static const struct supported_target supported_targets[] = {
   {AGX_CHIP_G15G, AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER},
   {AGX_CHIP_G15S, AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER},
   {AGX_CHIP_G15C, AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER},
   {AGX_CHIP_G15D, AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER},
   {AGX_CHIP_G16G, AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER},
   {AGX_CHIP_G16S, AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER},
   {AGX_CHIP_G16C, AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER},
   {AGX_CHIP_G17P, AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER},
   {AGX_CHIP_G17G, AGX_PPP_W_CLAMP_CULL2_GEN7_HEADER},
   {AGX_CHIP_G17S, AGX_PPP_W_CLAMP_CULL2_GEN7_HEADER},
   {AGX_CHIP_G17C, AGX_PPP_W_CLAMP_CULL2_GEN7_HEADER},
};

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G, AGX_CHIP_G13X,    AGX_CHIP_G14G,
   AGX_CHIP_G14X, AGX_CHIP_G15P,    AGX_CHIP_G16P,
   AGX_CHIP_G17A, AGX_CHIP_UNKNOWN, (enum agx_chip)999,
};

static void
store_le32(uint8_t *dst, uint32_t value)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static void
make_witness(uint32_t header, uint32_t w_clamp_raw, uint32_t cull2_raw,
             uint8_t bytes[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH])
{
   store_le32(&bytes[0], header);
   store_le32(&bytes[4], w_clamp_raw);
   store_le32(&bytes[8], cull2_raw);
}

static int
check_fields(const struct agx_ppp_w_clamp_cull2_fields *fields,
             uint32_t w_clamp_raw, uint32_t cull2_raw)
{
   CHECK(fields->w_clamp_raw == w_clamp_raw);
   CHECK(fields->cull2_raw == cull2_raw);
   return 0;
}

static int
check_zero_fields(const struct agx_ppp_w_clamp_cull2_fields *fields)
{
   return check_fields(fields, 0, 0);
}

static int
check_zero_encoding(const struct agx_ppp_w_clamp_cull2_core *encoded)
{
   for (unsigned i = 0; i < sizeof(encoded->bytes); ++i)
      CHECK(encoded->bytes[i] == 0);
   CHECK(encoded->length == 0);
   return 0;
}

static int
check_exact_default_witnesses(void)
{
   const struct agx_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = 0,
      .cull2_raw = UINT32_C(0x80),
   };

   for (unsigned i = 0;
        i < sizeof(supported_targets) / sizeof(supported_targets[0]); ++i) {
      const struct supported_target *target = &supported_targets[i];
      uint8_t expected[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH];
      struct agx_ppp_w_clamp_cull2_core encoded;
      struct agx_ppp_w_clamp_cull2_fields decoded;
      make_witness(target->header, 0, UINT32_C(0x80), expected);

      CHECK(agx_ppp_w_clamp_cull2_core_supported(target->chip));
      CHECK(agx_ppp_w_clamp_cull2_core_pack(target->chip, &fields, &encoded));
      CHECK(encoded.length == sizeof(expected));
      CHECK(memcmp(encoded.bytes, expected, sizeof(expected)) == 0);
      CHECK(agx_ppp_w_clamp_cull2_core_unpack(
         target->chip, expected, sizeof(expected), &decoded));
      if (check_fields(&decoded, 0, UINT32_C(0x80)))
         return 1;
   }

   return 0;
}

static int
check_raw_payload_patterns(void)
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
        target < sizeof(supported_targets) / sizeof(supported_targets[0]);
        ++target) {
      for (unsigned w = 0; w < sizeof(patterns) / sizeof(patterns[0]); ++w) {
         for (unsigned c = 0; c < sizeof(patterns) / sizeof(patterns[0]); ++c) {
            const struct agx_ppp_w_clamp_cull2_fields fields = {
               .w_clamp_raw = patterns[w],
               .cull2_raw = patterns[c],
            };
            struct agx_ppp_w_clamp_cull2_fields decoded;
            struct agx_ppp_w_clamp_cull2_core encoded;
            struct agx_ppp_w_clamp_cull2_core repacked;

            CHECK(agx_ppp_w_clamp_cull2_core_pack(
               supported_targets[target].chip, &fields, &encoded));
            CHECK(agx_ppp_w_clamp_cull2_core_unpack(
               supported_targets[target].chip, encoded.bytes, encoded.length,
               &decoded));
            if (check_fields(&decoded, patterns[w], patterns[c]))
               return 1;
            CHECK(agx_ppp_w_clamp_cull2_core_pack(
               supported_targets[target].chip, &decoded, &repacked));
            CHECK(repacked.length == encoded.length);
            CHECK(memcmp(repacked.bytes, encoded.bytes, encoded.length) == 0);
         }
      }
   }

   return 0;
}

static int
check_every_serialized_bit(void)
{
   const struct agx_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = UINT32_C(0x13579bdf),
      .cull2_raw = UINT32_C(0x2468ace0),
   };

   for (unsigned target = 0;
        target < sizeof(supported_targets) / sizeof(supported_targets[0]);
        ++target) {
      struct agx_ppp_w_clamp_cull2_core baseline;
      CHECK(agx_ppp_w_clamp_cull2_core_pack(
         supported_targets[target].chip, &fields, &baseline));

      for (unsigned bit = 0; bit < baseline.length * 8u; ++bit) {
         uint8_t changed[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH];
         struct agx_ppp_w_clamp_cull2_fields decoded;
         memcpy(changed, baseline.bytes, sizeof(changed));
         changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));

         if (bit < 32) {
            memset(&decoded, 0xa5, sizeof(decoded));
            CHECK(!agx_ppp_w_clamp_cull2_core_unpack(
               supported_targets[target].chip, changed, sizeof(changed),
               &decoded));
            if (check_zero_fields(&decoded))
               return 1;
         } else {
            struct agx_ppp_w_clamp_cull2_core repacked;
            CHECK(agx_ppp_w_clamp_cull2_core_unpack(
               supported_targets[target].chip, changed, sizeof(changed),
               &decoded));
            CHECK(agx_ppp_w_clamp_cull2_core_pack(
               supported_targets[target].chip, &decoded, &repacked));
            CHECK(memcmp(repacked.bytes, changed, sizeof(changed)) == 0);
         }
      }
   }

   return 0;
}

static int
check_generation_boundary(void)
{
   const struct agx_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = UINT32_C(0x11223344),
      .cull2_raw = UINT32_C(0x55667788),
   };
   struct agx_ppp_w_clamp_cull2_core gen6;
   struct agx_ppp_w_clamp_cull2_core gen7;

   CHECK(agx_ppp_w_clamp_cull2_core_pack(AGX_CHIP_G15G, &fields, &gen6));
   CHECK(agx_ppp_w_clamp_cull2_core_pack(AGX_CHIP_G17S, &fields, &gen7));
   CHECK(memcmp(gen6.bytes, gen7.bytes, gen6.length) != 0);

   for (unsigned old = 0; old < 8; ++old) {
      for (unsigned newer = 8;
           newer < sizeof(supported_targets) / sizeof(supported_targets[0]);
           ++newer) {
         struct agx_ppp_w_clamp_cull2_fields decoded;

         memset(&decoded, 0xa5, sizeof(decoded));
         CHECK(!agx_ppp_w_clamp_cull2_core_unpack(
            supported_targets[old].chip, gen7.bytes, gen7.length, &decoded));
         if (check_zero_fields(&decoded))
            return 1;

         memset(&decoded, 0xa5, sizeof(decoded));
         CHECK(!agx_ppp_w_clamp_cull2_core_unpack(
            supported_targets[newer].chip, gen6.bytes, gen6.length, &decoded));
         if (check_zero_fields(&decoded))
            return 1;
      }
   }

   return 0;
}

static int
check_rejections_and_clearing(void)
{
   const struct agx_ppp_w_clamp_cull2_fields fields = {
      .w_clamp_raw = UINT32_C(0x11223344),
      .cull2_raw = UINT32_C(0x55667788),
   };
   uint8_t gen6[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH];
   uint8_t gen7[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH];
   struct agx_ppp_w_clamp_cull2_core encoded;
   make_witness(AGX_PPP_W_CLAMP_CULL2_GEN6_HEADER, fields.w_clamp_raw,
                fields.cull2_raw, gen6);
   make_witness(AGX_PPP_W_CLAMP_CULL2_GEN7_HEADER, fields.w_clamp_raw,
                fields.cull2_raw, gen7);

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct agx_ppp_w_clamp_cull2_fields decoded;
      CHECK(!agx_ppp_w_clamp_cull2_core_supported(unsupported_chips[i]));

      memset(&encoded, 0xa5, sizeof(encoded));
      CHECK(!agx_ppp_w_clamp_cull2_core_pack(unsupported_chips[i], &fields,
                                             &encoded));
      if (check_zero_encoding(&encoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_w_clamp_cull2_core_unpack(
         unsupported_chips[i], gen6, sizeof(gen6), &decoded));
      if (check_zero_fields(&decoded))
         return 1;

      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_w_clamp_cull2_core_unpack(
         unsupported_chips[i], gen7, sizeof(gen7), &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   memset(&encoded, 0xa5, sizeof(encoded));
   CHECK(!agx_ppp_w_clamp_cull2_core_pack(AGX_CHIP_G17S, NULL, &encoded));
   if (check_zero_encoding(&encoded))
      return 1;
   CHECK(!agx_ppp_w_clamp_cull2_core_pack(AGX_CHIP_G17S, &fields, NULL));

   for (size_t size = 0; size < sizeof(gen7); ++size) {
      struct agx_ppp_w_clamp_cull2_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_w_clamp_cull2_core_unpack(AGX_CHIP_G17S, gen7, size,
                                               &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   const size_t trailing_sizes[] = {sizeof(gen7) + 1u, 64u, SIZE_MAX};
   for (unsigned i = 0; i < sizeof(trailing_sizes) / sizeof(trailing_sizes[0]);
        ++i) {
      struct agx_ppp_w_clamp_cull2_fields decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_w_clamp_cull2_core_unpack(
         AGX_CHIP_G17S, gen7, trailing_sizes[i], &decoded));
      if (check_zero_fields(&decoded))
         return 1;
   }

   uint8_t unaligned[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH + 1];
   memcpy(&unaligned[1], gen7, sizeof(gen7));
   struct agx_ppp_w_clamp_cull2_fields decoded;
   CHECK(agx_ppp_w_clamp_cull2_core_unpack(AGX_CHIP_G17S, &unaligned[1],
                                           sizeof(gen7), &decoded));
   if (check_fields(&decoded, fields.w_clamp_raw, fields.cull2_raw))
      return 1;

   uint8_t appended[AGX_PPP_W_CLAMP_CULL2_CORE_LENGTH + 1];
   memcpy(appended, gen7, sizeof(gen7));
   appended[sizeof(gen7)] = 0xa5;
   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!agx_ppp_w_clamp_cull2_core_unpack(
      AGX_CHIP_G17S, appended, sizeof(appended), &decoded));
   if (check_zero_fields(&decoded))
      return 1;

   memset(&decoded, 0xa5, sizeof(decoded));
   CHECK(!agx_ppp_w_clamp_cull2_core_unpack(AGX_CHIP_G17S, NULL, sizeof(gen7),
                                            &decoded));
   if (check_zero_fields(&decoded))
      return 1;
   CHECK(!agx_ppp_w_clamp_cull2_core_unpack(AGX_CHIP_G17S, gen7, sizeof(gen7),
                                            NULL));
   return 0;
}

int
main(void)
{
   if (check_exact_default_witnesses() || check_raw_payload_patterns() ||
       check_every_serialized_bit() || check_generation_boundary() ||
       check_rejections_and_clearing())
      return 1;

   printf("agx_ppp_w_clamp_cull2_core: %u checks passed\n", checks);
   return 0;
}
