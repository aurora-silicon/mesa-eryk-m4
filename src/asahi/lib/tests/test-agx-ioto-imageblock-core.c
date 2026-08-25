/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_ioto_imageblock_core.h"

#include <stdio.h>
#include <string.h>

static uint64_t checks;

#define CHECK(x)                                                               \
   do {                                                                        \
      ++checks;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                \
         return 1;                                                             \
      }                                                                        \
   } while (0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const enum agx_chip gpu0_targets[] = {
   AGX_CHIP_G15G, AGX_CHIP_G15S, AGX_CHIP_G15C, AGX_CHIP_G15D,
   AGX_CHIP_G16G, AGX_CHIP_G16S, AGX_CHIP_G16C, AGX_CHIP_G17P,
};

static const enum agx_chip gpu1_targets[] = {
   AGX_CHIP_G17G,
   AGX_CHIP_G17S,
   AGX_CHIP_G17C,
};

static const enum agx_chip unsupported_targets[] = {
   AGX_CHIP_G13G,    AGX_CHIP_G13X,    AGX_CHIP_G14G,
   AGX_CHIP_G14X,    AGX_CHIP_G15P,    AGX_CHIP_G16P,
   AGX_CHIP_G17A,    AGX_CHIP_UNKNOWN, (enum agx_chip)-1,
   (enum agx_chip)999,
};

static bool
bytes_are_zero(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   for (size_t i = 0; i < size; ++i) {
      if (bytes[i] != 0)
         return false;
   }
   return true;
}

/* An intentionally different ceil implementation for the test oracle. */
static uint32_t
reference_ceil_pow2(uint32_t value)
{
   if (value <= 1 || value > (UINT32_C(1) << 31))
      return 1;

   uint32_t power = 1;
   while (power < value)
      power <<= 1;
   return power;
}

static uint32_t
reference_log_code(uint32_t value)
{
   uint32_t power = 2;
   for (uint32_t code = 1; code <= 10; ++code, power <<= 1) {
      if (value == power)
         return code;
   }
   return 0;
}

static uint32_t
reference_sample_code(enum agx_ioto_imageblock_encoding encoding,
                      uint32_t samples)
{
   if (samples == 2)
      return 1;
   if (samples == 4)
      return 2;
   return encoding == AGX_IOTO_IMAGEBLOCK_ENCODING_GPU1 && samples == 8 ? 3
                                                                        : 0;
}

static uint32_t
reference_mul32(uint32_t a, uint32_t b)
{
   return (uint32_t)((uint64_t)a * b);
}

static enum agx_ioto_imageblock_encoding
reference_encoding(enum agx_chip chip)
{
   for (size_t i = 0; i < ARRAY_SIZE(gpu0_targets); ++i) {
      if (chip == gpu0_targets[i])
         return AGX_IOTO_IMAGEBLOCK_ENCODING_GPU0;
   }
   for (size_t i = 0; i < ARRAY_SIZE(gpu1_targets); ++i) {
      if (chip == gpu1_targets[i])
         return AGX_IOTO_IMAGEBLOCK_ENCODING_GPU1;
   }
   return AGX_IOTO_IMAGEBLOCK_ENCODING_UNSUPPORTED;
}

static void
reference_transform(enum agx_chip chip,
                    const struct agx_ioto_imageblock_params *params,
                    struct agx_ioto_imageblock_result *result)
{
   memset(result, 0, sizeof(*result));
   result->chip = chip;
   result->encoding = reference_encoding(chip);
   result->sample_count = params->p1 == 0 ? 1 : params->p1;
   result->p0_aligned = params->p0 & ~UINT32_C(3);

   result->first_product =
      reference_mul32(result->p0_aligned, result->sample_count);
   result->first_product =
      reference_mul32(result->first_product, params->p2);
   result->first_product =
      reference_mul32(result->first_product, params->p3);

   result->a = ((params->p0 >> 2) + UINT32_C(1)) & ~UINT32_C(1);
   result->x = reference_ceil_pow2(params->p2);
   result->y = reference_ceil_pow2(params->p3);
   result->four_a = reference_mul32(result->a, 4);
   result->second_product = reference_mul32(result->four_a, result->x);
   result->second_product =
      reference_mul32(result->second_product, result->y);
   result->second_product =
      reference_mul32(result->second_product, result->sample_count);
   result->active =
      result->first_product != 0 && result->second_product != 0;

   result->x_log_code = reference_log_code(result->x);
   result->y_log_code = reference_log_code(result->y);
   result->sample_code =
      reference_sample_code(result->encoding, result->sample_count);

   result->rounded_four_a =
      (result->four_a + UINT32_C(7)) & UINT32_C(0xfffffff8);
   result->q = result->rounded_four_a / 8;
   result->q_field = result->q % 32;
   if (result->active) {
      result->lo = result->x_log_code |
                   ((uint64_t)result->y_log_code << 32);
      result->hi = result->sample_code |
                   ((uint64_t)result->q_field << 35);
   }

   const uint32_t shifted_p4 = (uint32_t)((uint64_t)params->p4 << 16);
   result->h_payload =
      (shifted_p4 + UINT32_C(0x003f0000)) & UINT32_C(0xffc00000);
   result->h = (result->active ? UINT64_C(0x100000000) : 0) |
               result->h_payload | (params->p4 == 0 ? 0 : 1);
}

static bool
results_equal(const struct agx_ioto_imageblock_result *a,
              const struct agx_ioto_imageblock_result *b)
{
   return a->chip == b->chip && a->encoding == b->encoding &&
          a->sample_count == b->sample_count &&
          a->p0_aligned == b->p0_aligned &&
          a->first_product == b->first_product && a->a == b->a &&
          a->x == b->x && a->y == b->y && a->four_a == b->four_a &&
          a->second_product == b->second_product &&
          a->x_log_code == b->x_log_code &&
          a->y_log_code == b->y_log_code &&
          a->sample_code == b->sample_code &&
          a->rounded_four_a == b->rounded_four_a && a->q == b->q &&
          a->q_field == b->q_field && a->active == b->active &&
          a->lo == b->lo && a->hi == b->hi &&
          a->h_payload == b->h_payload && a->h == b->h;
}

static int
check_against_reference(enum agx_chip chip,
                        const struct agx_ioto_imageblock_params *params)
{
   struct agx_ioto_imageblock_result actual;
   struct agx_ioto_imageblock_result expected;
   CHECK(agx_ioto_imageblock_transform(chip, params, &actual));
   reference_transform(chip, params, &expected);
   CHECK(results_equal(&actual, &expected));
   return 0;
}

static int
test_target_and_api_contract(void)
{
   const struct agx_ioto_imageblock_params params = {8, 2, 3, 5, 1};

   for (size_t i = 0; i < ARRAY_SIZE(gpu0_targets); ++i) {
      CHECK(agx_ioto_imageblock_target_supported(gpu0_targets[i]));
      CHECK(agx_ioto_imageblock_target_encoding(gpu0_targets[i]) ==
            AGX_IOTO_IMAGEBLOCK_ENCODING_GPU0);
      CHECK(check_against_reference(gpu0_targets[i], &params) == 0);
   }
   for (size_t i = 0; i < ARRAY_SIZE(gpu1_targets); ++i) {
      CHECK(agx_ioto_imageblock_target_supported(gpu1_targets[i]));
      CHECK(agx_ioto_imageblock_target_encoding(gpu1_targets[i]) ==
            AGX_IOTO_IMAGEBLOCK_ENCODING_GPU1);
      CHECK(check_against_reference(gpu1_targets[i], &params) == 0);
   }

   for (size_t i = 0; i < ARRAY_SIZE(unsupported_targets); ++i) {
      const enum agx_chip chip = unsupported_targets[i];
      CHECK(!agx_ioto_imageblock_target_supported(chip));
      CHECK(agx_ioto_imageblock_target_encoding(chip) ==
            AGX_IOTO_IMAGEBLOCK_ENCODING_UNSUPPORTED);

      struct agx_ioto_imageblock_result result;
      memset(&result, 0xa5, sizeof(result));
      CHECK(!agx_ioto_imageblock_transform(chip, &params, &result));
      CHECK(bytes_are_zero(&result, sizeof(result)));
   }

   struct agx_ioto_imageblock_result result;
   memset(&result, 0xa5, sizeof(result));
   CHECK(!agx_ioto_imageblock_transform(AGX_CHIP_G15G, NULL, &result));
   CHECK(bytes_are_zero(&result, sizeof(result)));
   CHECK(!agx_ioto_imageblock_transform(AGX_CHIP_G15G, &params, NULL));
   return 0;
}

static int
test_exact_witnesses(void)
{
   const struct agx_ioto_imageblock_params params = {8, 2, 3, 5, 1};
   struct agx_ioto_imageblock_result result;
   CHECK(agx_ioto_imageblock_transform(AGX_CHIP_G15G, &params, &result));
   CHECK(result.sample_count == 2);
   CHECK(result.p0_aligned == 8);
   CHECK(result.first_product == 240);
   CHECK(result.a == 2);
   CHECK(result.x == 4);
   CHECK(result.y == 8);
   CHECK(result.four_a == 8);
   CHECK(result.second_product == 512);
   CHECK(result.active);
   CHECK(result.x_log_code == 2);
   CHECK(result.y_log_code == 3);
   CHECK(result.sample_code == 1);
   CHECK(result.rounded_four_a == 8);
   CHECK(result.q == 1);
   CHECK(result.q_field == 1);
   CHECK(result.lo == UINT64_C(0x0000000300000002));
   CHECK(result.hi == UINT64_C(0x0000000800000001));
   CHECK(result.h_payload == UINT32_C(0x00400000));
   CHECK(result.h == UINT64_C(0x0000000100400001));

   const struct agx_ioto_imageblock_params gpu1_params = {8, 8, 3, 5, 1};
   CHECK(agx_ioto_imageblock_transform(AGX_CHIP_G17G, &gpu1_params,
                                       &result));
   CHECK(result.sample_code == 3);
   CHECK((uint32_t)result.hi == 3);

   CHECK(agx_ioto_imageblock_transform(AGX_CHIP_G15G, &gpu1_params,
                                       &result));
   CHECK(result.sample_code == 0);
   CHECK((uint32_t)result.hi == 0);
   return 0;
}

static int
test_ceil_and_log_boundaries(void)
{
   static const struct {
      uint32_t input;
      uint32_t rounded;
      uint32_t log_code;
   } fixed[] = {
      {0, 1, 0},
      {1, 1, 0},
      {2, 2, 1},
      {3, 4, 2},
      {4, 4, 2},
      {5, 8, 3},
      {1023, 1024, 10},
      {1024, 1024, 10},
      {1025, 2048, 0},
      {UINT32_C(0x7fffffff), UINT32_C(0x80000000), 0},
      {UINT32_C(0x80000000), UINT32_C(0x80000000), 0},
      {UINT32_C(0x80000001), 1, 0},
      {UINT32_MAX, 1, 0},
   };

   for (size_t i = 0; i < ARRAY_SIZE(fixed); ++i) {
      const struct agx_ioto_imageblock_params p2 = {
         4, 1, fixed[i].input, 1, 0,
      };
      struct agx_ioto_imageblock_result result;
      CHECK(agx_ioto_imageblock_transform(AGX_CHIP_G15G, &p2, &result));
      CHECK(result.x == fixed[i].rounded);
      CHECK(result.x_log_code == fixed[i].log_code);

      const struct agx_ioto_imageblock_params p3 = {
         4, 1, 1, fixed[i].input, 0,
      };
      CHECK(agx_ioto_imageblock_transform(AGX_CHIP_G17G, &p3, &result));
      CHECK(result.y == fixed[i].rounded);
      CHECK(result.y_log_code == fixed[i].log_code);
   }

   for (uint32_t exponent = 1; exponent < 32; ++exponent) {
      const uint32_t power = UINT32_C(1) << exponent;
      const uint32_t inputs[] = {power - 1, power};
      for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
         const struct agx_ioto_imageblock_params params = {
            12, 4, inputs[i], inputs[1 - i], exponent,
         };
         CHECK(check_against_reference(AGX_CHIP_G17S, &params) == 0);
      }
      if (power != (UINT32_C(1) << 31)) {
         const struct agx_ioto_imageblock_params params = {
            12, 4, power + 1, power + 1, exponent,
         };
         CHECK(check_against_reference(AGX_CHIP_G15S, &params) == 0);
      }
   }
   return 0;
}

static int
test_activity_wrap_and_q_field(void)
{
   static const struct agx_ioto_imageblock_params inactive[] = {
      {0, 1, 1, 1, 0},
      {3, 1, 1, 1, 0},
      {4, 1, 0, 1, 0},
      {4, 1, 1, 0, 0},
      /* First product wraps to zero. */
      {4, UINT32_C(0x40000000), 1, 1, 0},
      /* First remains nonzero while the rounded-dimension product wraps. */
      {4, 1, UINT32_C(0x20000000), 1, 0},
      /* a == 0x40000000, so four_a wraps to zero. */
      {UINT32_MAX, 1, 1, 1, 0},
   };

   for (size_t i = 0; i < ARRAY_SIZE(inactive); ++i) {
      struct agx_ioto_imageblock_result result;
      CHECK(agx_ioto_imageblock_transform(AGX_CHIP_G15C, &inactive[i],
                                          &result));
      CHECK(!result.active);
      CHECK(result.lo == 0);
      CHECK(result.hi == 0);
      CHECK((result.h & (UINT64_C(1) << 32)) == 0);
      CHECK(check_against_reference(AGX_CHIP_G15C, &inactive[i]) == 0);
   }

   /* p0 covers two complete cycles of the five-bit q field, plus every
    * transition caused by the low two discarded input bits. */
   for (uint32_t p0 = 0; p0 < 1024; ++p0) {
      const struct agx_ioto_imageblock_params params = {p0, 2, 3, 5, p0};
      CHECK(check_against_reference(AGX_CHIP_G17C, &params) == 0);
   }

   const uint32_t p0_boundaries[] = {
      UINT32_C(0x7ffffffb), UINT32_C(0x7ffffffc),
      UINT32_C(0x7fffffff), UINT32_C(0x80000000),
      UINT32_C(0xfffffffb), UINT32_C(0xfffffffc),
      UINT32_MAX,
   };
   for (size_t i = 0; i < ARRAY_SIZE(p0_boundaries); ++i) {
      const struct agx_ioto_imageblock_params params = {
         p0_boundaries[i], UINT32_MAX, 3, 5, p0_boundaries[i],
      };
      CHECK(check_against_reference(AGX_CHIP_G15D, &params) == 0);
   }
   return 0;
}

static int
test_h_exhaustive_low16(void)
{
   /* The masked payload depends on all sixteen low p4 bits; exercise that
    * complete domain, then verify that high bits only affect the nonzero flag. */
   for (uint32_t p4 = 0; p4 <= UINT16_MAX; ++p4) {
      const struct agx_ioto_imageblock_params params = {8, 2, 3, 5, p4};
      CHECK(check_against_reference(AGX_CHIP_G15G, &params) == 0);
   }

   const uint32_t high_values[] = {
      UINT32_C(0x00010000), UINT32_C(0x80000000),
      UINT32_C(0xffff0000), UINT32_MAX,
   };
   for (size_t i = 0; i < ARRAY_SIZE(high_values); ++i) {
      const struct agx_ioto_imageblock_params params = {
         8, 2, 3, 5, high_values[i],
      };
      CHECK(check_against_reference(AGX_CHIP_G17G, &params) == 0);
   }
   return 0;
}

static int
test_exhaustive_small_domain(void)
{
   const enum agx_chip representatives[] = {AGX_CHIP_G15G, AGX_CHIP_G17G};

   for (size_t target = 0; target < ARRAY_SIZE(representatives); ++target) {
      for (uint32_t p0 = 0; p0 < 64; ++p0) {
         for (uint32_t p1 = 0; p1 < 9; ++p1) {
            for (uint32_t p2 = 0; p2 < 25; ++p2) {
               for (uint32_t p3 = 0; p3 < 25; ++p3) {
                  const struct agx_ioto_imageblock_params params = {
                     p0, p1, p2, p3,
                     (p0 * 257u) ^ (p1 << 12) ^ (p2 << 6) ^ p3,
                  };
                  CHECK(check_against_reference(representatives[target],
                                                &params) == 0);
               }
            }
         }
      }
   }
   return 0;
}

int
main(void)
{
   CHECK(test_target_and_api_contract() == 0);
   CHECK(test_exact_witnesses() == 0);
   CHECK(test_ceil_and_log_boundaries() == 0);
   CHECK(test_activity_wrap_and_q_field() == 0);
   CHECK(test_h_exhaustive_low16() == 0);
   CHECK(test_exhaustive_small_domain() == 0);

   printf("agx_ioto_imageblock_core: %llu checks passed\n",
          (unsigned long long)checks);
   return 0;
}
