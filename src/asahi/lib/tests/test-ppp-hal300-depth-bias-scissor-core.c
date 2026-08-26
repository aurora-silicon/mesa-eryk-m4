/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_ppp_hal300_depth_bias_scissor_core.h"

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

static const enum agx_chip unsupported_chips[] = {
   AGX_CHIP_G13G,      AGX_CHIP_G13X, AGX_CHIP_G14G, AGX_CHIP_G14X,
   AGX_CHIP_G15P,      AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C,
   AGX_CHIP_G15D,      AGX_CHIP_G16P, AGX_CHIP_G16G, AGX_CHIP_G16S,
   AGX_CHIP_G16C,      AGX_CHIP_G17P, AGX_CHIP_G17A, AGX_CHIP_UNKNOWN,
   (enum agx_chip)999,
};

static int
check_depth_fields(
   const struct agx_ppp_hal300_depth_bias_record_fields *actual,
   const struct agx_ppp_hal300_depth_bias_record_fields *expected)
{
   CHECK(actual->depth_bias_raw == expected->depth_bias_raw);
   CHECK(actual->slope_scale_raw == expected->slope_scale_raw);
   CHECK(actual->clamp_raw == expected->clamp_raw);
   return 0;
}

static int
check_scissor_fields(const struct agx_ppp_hal300_scissor_record_fields *actual,
                     const struct agx_ppp_hal300_scissor_record_fields *expected)
{
   CHECK(actual->max_x == expected->max_x);
   CHECK(actual->min_x == expected->min_x);
   CHECK(actual->max_y == expected->max_y);
   CHECK(actual->min_y == expected->min_y);
   CHECK(actual->min_z_raw == expected->min_z_raw);
   CHECK(actual->max_z_raw == expected->max_z_raw);
   return 0;
}

static int
check_bases_fields(
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields *actual,
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields *expected)
{
   CHECK(actual->depth_bias_base == expected->depth_bias_base);
   CHECK(actual->scissor_base == expected->scissor_base);
   return 0;
}

static int
check_zero_bytes(const uint8_t *bytes, size_t size, uint8_t length)
{
   for (size_t i = 0; i < size; ++i)
      CHECK(bytes[i] == 0);
   CHECK(length == 0);
   return 0;
}

static int
check_exact_records_and_initial_update_order(void)
{
   const struct agx_ppp_hal300_depth_bias_record_fields depth = {
      .depth_bias_raw = UINT32_C(0x3f800000),
      .slope_scale_raw = UINT32_C(0xc0200000),
      .clamp_raw = UINT32_C(0x3eaaaaab),
   };
   const struct agx_ppp_hal300_scissor_record_fields scissor = {
      .max_x = UINT16_C(0x1122),
      .min_x = UINT16_C(0x3344),
      .max_y = UINT16_C(0x5566),
      .min_y = UINT16_C(0x7788),
      .min_z_raw = UINT32_C(0x3dcccccd),
      .max_z_raw = UINT32_C(0x3f666666),
   };
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields bases = {
      .depth_bias_base = UINT64_C(0x0000123456789abc),
      .scissor_base = UINT64_C(0x0000abcdef012340),
   };
   static const uint8_t expected_depth[] = {
      0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x20, 0xc0, 0xab, 0xaa, 0xaa, 0x3e,
   };
   static const uint8_t expected_scissor[] = {
      0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77,
      0xcd, 0xcc, 0xcc, 0x3d, 0x66, 0x66, 0x66, 0x3f,
   };
   /* Initial and FlexiblePSO update paths both emit this exact word order. */
   static const uint8_t expected_bases[] = {
      0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x34, 0x12, 0xbc, 0x9a,
      0x78, 0x56, 0x00, 0x00, 0xcd, 0xab, 0x40, 0x23, 0x01, 0xef,
   };

   for (unsigned i = 0;
        i < sizeof(supported_chips) / sizeof(supported_chips[0]); ++i) {
      struct agx_ppp_hal300_depth_bias_record packed_depth;
      struct agx_ppp_hal300_scissor_record packed_scissor;
      struct agx_ppp_hal300_depth_bias_scissor_bases packed_bases;
      struct agx_ppp_hal300_depth_bias_record_fields unpacked_depth;
      struct agx_ppp_hal300_scissor_record_fields unpacked_scissor;
      struct agx_ppp_hal300_depth_bias_scissor_bases_fields unpacked_bases;

      CHECK(agx_ppp_hal300_depth_bias_record_pack(supported_chips[i], &depth,
                                                  &packed_depth));
      CHECK(packed_depth.length == sizeof(expected_depth));
      CHECK(memcmp(packed_depth.bytes, expected_depth,
                   sizeof(expected_depth)) == 0);
      CHECK(agx_ppp_hal300_depth_bias_record_unpack(
         supported_chips[i], expected_depth, sizeof(expected_depth),
         &unpacked_depth));
      if (check_depth_fields(&unpacked_depth, &depth))
         return 1;

      CHECK(agx_ppp_hal300_scissor_record_pack(supported_chips[i], &scissor,
                                               &packed_scissor));
      CHECK(packed_scissor.length == sizeof(expected_scissor));
      CHECK(memcmp(packed_scissor.bytes, expected_scissor,
                   sizeof(expected_scissor)) == 0);
      CHECK(agx_ppp_hal300_scissor_record_unpack(
         supported_chips[i], expected_scissor, sizeof(expected_scissor),
         &unpacked_scissor));
      if (check_scissor_fields(&unpacked_scissor, &scissor))
         return 1;

      CHECK(agx_ppp_hal300_depth_bias_scissor_bases_pack(
         supported_chips[i], &bases, &packed_bases));
      CHECK(packed_bases.length == sizeof(expected_bases));
      CHECK(memcmp(packed_bases.bytes, expected_bases,
                   sizeof(expected_bases)) == 0);
      CHECK(agx_ppp_hal300_depth_bias_scissor_bases_unpack(
         supported_chips[i], expected_bases, sizeof(expected_bases),
         &unpacked_bases));
      if (check_bases_fields(&unpacked_bases, &bases))
         return 1;
   }
   return 0;
}

static int
check_every_record_bit(void)
{
   uint8_t depth[AGX_PPP_HAL300_DEPTH_BIAS_RECORD_LENGTH] = {0};
   uint8_t scissor[AGX_PPP_HAL300_SCISSOR_RECORD_LENGTH] = {0};

   for (unsigned bit = 0; bit < sizeof(depth) * 8u; ++bit) {
      struct agx_ppp_hal300_depth_bias_record_fields fields;
      struct agx_ppp_hal300_depth_bias_record repacked;
      memset(depth, 0, sizeof(depth));
      depth[bit / 8] = (uint8_t)(1u << (bit % 8));
      CHECK(agx_ppp_hal300_depth_bias_record_unpack(AGX_CHIP_G17G, depth,
                                                    sizeof(depth), &fields));
      CHECK(agx_ppp_hal300_depth_bias_record_pack(AGX_CHIP_G17G, &fields,
                                                  &repacked));
      CHECK(memcmp(depth, repacked.bytes, sizeof(depth)) == 0);
   }

   for (unsigned bit = 0; bit < sizeof(scissor) * 8u; ++bit) {
      struct agx_ppp_hal300_scissor_record_fields fields;
      struct agx_ppp_hal300_scissor_record repacked;
      memset(scissor, 0, sizeof(scissor));
      scissor[bit / 8] = (uint8_t)(1u << (bit % 8));
      CHECK(agx_ppp_hal300_scissor_record_unpack(AGX_CHIP_G17G, scissor,
                                                 sizeof(scissor), &fields));
      CHECK(
         agx_ppp_hal300_scissor_record_pack(AGX_CHIP_G17G, &fields, &repacked));
      CHECK(memcmp(scissor, repacked.bytes, sizeof(scissor)) == 0);
   }
   return 0;
}

/* Representative live draw values.  The G17 scissor record is max/min in
 * each axis (not the API-facing min/max order), followed by raw IEEE-754 Z.
 */
static int
check_live_draw_record_bytes(void)
{
   const struct agx_ppp_hal300_depth_bias_record_fields depth = {
      .depth_bias_raw = UINT32_C(0xbe800000), /* -0.25 */
      .slope_scale_raw = UINT32_C(0x3fc00000), /* 1.5 */
      .clamp_raw = UINT32_C(0x3e000000), /* 0.125 */
   };
   const struct agx_ppp_hal300_scissor_record_fields scissor = {
      .max_x = UINT16_C(1920),
      .min_x = UINT16_C(16),
      .max_y = UINT16_C(1080),
      .min_y = UINT16_C(8),
      .min_z_raw = UINT32_C(0x00000000), /* 0.0 */
      .max_z_raw = UINT32_C(0x3f800000), /* 1.0 */
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
      struct agx_ppp_hal300_depth_bias_record packed_depth;
      struct agx_ppp_hal300_scissor_record packed_scissor;
      CHECK(agx_ppp_hal300_depth_bias_record_pack(
         supported_chips[i], &depth, &packed_depth));
      CHECK(packed_depth.length == sizeof(expected_depth));
      CHECK(memcmp(packed_depth.bytes, expected_depth,
                   sizeof(expected_depth)) == 0);
      CHECK(agx_ppp_hal300_scissor_record_pack(
         supported_chips[i], &scissor, &packed_scissor));
      CHECK(packed_scissor.length == sizeof(expected_scissor));
      CHECK(memcmp(packed_scissor.bytes, expected_scissor,
                   sizeof(expected_scissor)) == 0);
   }
   return 0;
}

static bool
is_reserved_bases_bit(unsigned bit)
{
   if (bit < 32)
      return true;

   const unsigned base_bit = (bit - 32u) % 64u;
   return base_bit < 16u || (base_bit >= 32u && base_bit < 34u);
}

static int
check_every_bases_bit(void)
{
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields zero = {0};
   struct agx_ppp_hal300_depth_bias_scissor_bases baseline;
   CHECK(agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17G, &zero,
                                                      &baseline));

   for (unsigned bit = 0; bit < baseline.length * 8u; ++bit) {
      uint8_t changed[AGX_PPP_HAL300_DEPTH_BIAS_SCISSOR_BASES_LENGTH];
      struct agx_ppp_hal300_depth_bias_scissor_bases_fields fields;
      memcpy(changed, baseline.bytes, sizeof(changed));
      changed[bit / 8] ^= (uint8_t)(1u << (bit % 8));
      memset(&fields, 0xa5, sizeof(fields));

      if (is_reserved_bases_bit(bit)) {
         const struct agx_ppp_hal300_depth_bias_scissor_bases_fields
            zero_fields = {0};
         CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_unpack(
            AGX_CHIP_G17G, changed, sizeof(changed), &fields));
         if (check_bases_fields(&fields, &zero_fields))
            return 1;
      } else {
         struct agx_ppp_hal300_depth_bias_scissor_bases repacked;
         CHECK(agx_ppp_hal300_depth_bias_scissor_bases_unpack(
            AGX_CHIP_G17G, changed, sizeof(changed), &fields));
         CHECK(agx_ppp_hal300_depth_bias_scissor_bases_pack(
            AGX_CHIP_G17G, &fields, &repacked));
         CHECK(memcmp(changed, repacked.bytes, sizeof(changed)) == 0);
      }
   }
   return 0;
}

static int
check_base_alignment_and_range(void)
{
   struct agx_ppp_hal300_depth_bias_scissor_bases_fields fields = {0};
   struct agx_ppp_hal300_depth_bias_scissor_bases packed;
   const uint64_t valid[] = {
      UINT64_C(0),
      UINT64_C(4),
      UINT64_C(0x0000800000000000),
      UINT64_C(0x0000fffffffffffc),
   };

   for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
      struct agx_ppp_hal300_depth_bias_scissor_bases_fields unpacked;
      fields.depth_bias_base = valid[i];
      fields.scissor_base = valid[(i + 1) % (sizeof(valid) / sizeof(valid[0]))];
      CHECK(agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17C, &fields,
                                                         &packed));
      CHECK(agx_ppp_hal300_depth_bias_scissor_bases_unpack(
         AGX_CHIP_G17C, packed.bytes, packed.length, &unpacked));
      if (check_bases_fields(&unpacked, &fields))
         return 1;
   }

   for (uint64_t low = 1; low < 4; ++low) {
      fields.depth_bias_base = UINT64_C(0x1000) | low;
      fields.scissor_base = UINT64_C(0x2000);
      memset(&packed, 0xa5, sizeof(packed));
      CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17C,
                                                          &fields, &packed));
      if (check_zero_bytes(packed.bytes, sizeof(packed.bytes), packed.length))
         return 1;

      fields.depth_bias_base = UINT64_C(0x1000);
      fields.scissor_base = UINT64_C(0x2000) | low;
      memset(&packed, 0xa5, sizeof(packed));
      CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17C,
                                                          &fields, &packed));
      if (check_zero_bytes(packed.bytes, sizeof(packed.bytes), packed.length))
         return 1;
   }

   const uint64_t out_of_range[] = {
      UINT64_C(0x0001000000000000),
      UINT64_C(0x8000000000000000),
      UINT64_MAX,
   };
   for (unsigned i = 0; i < sizeof(out_of_range) / sizeof(out_of_range[0]);
        ++i) {
      fields.depth_bias_base = out_of_range[i];
      fields.scissor_base = 0;
      memset(&packed, 0xa5, sizeof(packed));
      CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17G,
                                                          &fields, &packed));
      if (check_zero_bytes(packed.bytes, sizeof(packed.bytes), packed.length))
         return 1;

      fields.depth_bias_base = 0;
      fields.scissor_base = out_of_range[i];
      memset(&packed, 0xa5, sizeof(packed));
      CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17G,
                                                          &fields, &packed));
      if (check_zero_bytes(packed.bytes, sizeof(packed.bytes), packed.length))
         return 1;
   }
   return 0;
}

static int
check_transactional_rejections(void)
{
   const struct agx_ppp_hal300_depth_bias_record_fields depth = {1, 2, 3};
   const struct agx_ppp_hal300_scissor_record_fields scissor = {
      1, 2, 3, 4, 5, 6,
   };
   const struct agx_ppp_hal300_depth_bias_scissor_bases_fields bases = {
      UINT64_C(0x1000),
      UINT64_C(0x2000),
   };
   struct agx_ppp_hal300_depth_bias_record depth_payload;
   struct agx_ppp_hal300_scissor_record scissor_payload;
   struct agx_ppp_hal300_depth_bias_scissor_bases bases_payload;
   CHECK(agx_ppp_hal300_depth_bias_record_pack(AGX_CHIP_G17G, &depth,
                                               &depth_payload));
   CHECK(agx_ppp_hal300_scissor_record_pack(AGX_CHIP_G17G, &scissor,
                                            &scissor_payload));
   CHECK(agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17G, &bases,
                                                      &bases_payload));

   for (unsigned i = 0;
        i < sizeof(unsupported_chips) / sizeof(unsupported_chips[0]); ++i) {
      struct agx_ppp_hal300_depth_bias_record packed_depth;
      struct agx_ppp_hal300_scissor_record packed_scissor;
      struct agx_ppp_hal300_depth_bias_scissor_bases packed_bases;
      struct agx_ppp_hal300_depth_bias_record_fields decoded_depth;
      struct agx_ppp_hal300_scissor_record_fields decoded_scissor;
      struct agx_ppp_hal300_depth_bias_scissor_bases_fields decoded_bases;
      memset(&packed_depth, 0xa5, sizeof(packed_depth));
      memset(&packed_scissor, 0xa5, sizeof(packed_scissor));
      memset(&packed_bases, 0xa5, sizeof(packed_bases));
      CHECK(!agx_ppp_hal300_depth_bias_record_pack(unsupported_chips[i], &depth,
                                                   &packed_depth));
      CHECK(!agx_ppp_hal300_scissor_record_pack(unsupported_chips[i], &scissor,
                                                &packed_scissor));
      CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_pack(
         unsupported_chips[i], &bases, &packed_bases));
      if (check_zero_bytes(packed_depth.bytes, sizeof(packed_depth.bytes),
                           packed_depth.length) ||
          check_zero_bytes(packed_scissor.bytes, sizeof(packed_scissor.bytes),
                           packed_scissor.length) ||
          check_zero_bytes(packed_bases.bytes, sizeof(packed_bases.bytes),
                           packed_bases.length))
         return 1;

      memset(&decoded_depth, 0xa5, sizeof(decoded_depth));
      memset(&decoded_scissor, 0xa5, sizeof(decoded_scissor));
      memset(&decoded_bases, 0xa5, sizeof(decoded_bases));
      CHECK(!agx_ppp_hal300_depth_bias_record_unpack(
         unsupported_chips[i], depth_payload.bytes, depth_payload.length,
         &decoded_depth));
      CHECK(!agx_ppp_hal300_scissor_record_unpack(
         unsupported_chips[i], scissor_payload.bytes, scissor_payload.length,
         &decoded_scissor));
      CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_unpack(
         unsupported_chips[i], bases_payload.bytes, bases_payload.length,
         &decoded_bases));
      const struct agx_ppp_hal300_depth_bias_record_fields zero_depth = {0};
      const struct agx_ppp_hal300_scissor_record_fields zero_scissor = {0};
      const struct agx_ppp_hal300_depth_bias_scissor_bases_fields zero_bases = {
         0};
      if (check_depth_fields(&decoded_depth, &zero_depth) ||
          check_scissor_fields(&decoded_scissor, &zero_scissor) ||
          check_bases_fields(&decoded_bases, &zero_bases))
         return 1;
   }

   for (size_t size = 0; size <= bases_payload.length + 1u; ++size) {
      struct agx_ppp_hal300_depth_bias_scissor_bases_fields decoded;
      if (size == bases_payload.length)
         continue;
      memset(&decoded, 0xa5, sizeof(decoded));
      CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_unpack(
         AGX_CHIP_G17G, bases_payload.bytes, size, &decoded));
      const struct agx_ppp_hal300_depth_bias_scissor_bases_fields zero = {0};
      if (check_bases_fields(&decoded, &zero))
         return 1;
   }

   struct agx_ppp_hal300_depth_bias_record_fields decoded_depth;
   struct agx_ppp_hal300_scissor_record_fields decoded_scissor;
   struct agx_ppp_hal300_depth_bias_scissor_bases_fields decoded_bases;
   memset(&decoded_depth, 0xa5, sizeof(decoded_depth));
   memset(&decoded_scissor, 0xa5, sizeof(decoded_scissor));
   memset(&decoded_bases, 0xa5, sizeof(decoded_bases));
   CHECK(!agx_ppp_hal300_depth_bias_record_unpack(
      AGX_CHIP_G17G, NULL, depth_payload.length, &decoded_depth));
   CHECK(!agx_ppp_hal300_scissor_record_unpack(
      AGX_CHIP_G17G, NULL, scissor_payload.length, &decoded_scissor));
   CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_unpack(
      AGX_CHIP_G17G, NULL, bases_payload.length, &decoded_bases));
   CHECK(!agx_ppp_hal300_depth_bias_record_pack(AGX_CHIP_G17G, NULL,
                                                &depth_payload));
   CHECK(!agx_ppp_hal300_scissor_record_pack(AGX_CHIP_G17G, NULL,
                                             &scissor_payload));
   CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17G, NULL,
                                                       &bases_payload));
   CHECK(!agx_ppp_hal300_depth_bias_record_pack(AGX_CHIP_G17G, &depth, NULL));
   CHECK(!agx_ppp_hal300_scissor_record_pack(AGX_CHIP_G17G, &scissor, NULL));
   CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_pack(AGX_CHIP_G17G, &bases,
                                                       NULL));
   CHECK(!agx_ppp_hal300_depth_bias_record_unpack(
      AGX_CHIP_G17G, depth_payload.bytes, sizeof(depth_payload.bytes), NULL));
   CHECK(!agx_ppp_hal300_scissor_record_unpack(
      AGX_CHIP_G17G, scissor_payload.bytes, sizeof(scissor_payload.bytes),
      NULL));
   CHECK(!agx_ppp_hal300_depth_bias_scissor_bases_unpack(
      AGX_CHIP_G17G, bases_payload.bytes, sizeof(bases_payload.bytes), NULL));
   return 0;
}

int
main(void)
{
   if (check_exact_records_and_initial_update_order() ||
       check_live_draw_record_bytes() || check_every_record_bit() ||
       check_every_bases_bit() ||
       check_base_alignment_and_range() || check_transactional_rejections())
      return 1;

   printf("agx_ppp_hal300_depth_bias_scissor_core: %u checks passed\n", checks);
   return 0;
}
