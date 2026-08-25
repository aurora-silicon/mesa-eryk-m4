/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_ioto_imageblock_core.h"

#include <string.h>

enum agx_ioto_imageblock_encoding
agx_ioto_imageblock_target_encoding(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
      return AGX_IOTO_IMAGEBLOCK_ENCODING_GPU0;
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return AGX_IOTO_IMAGEBLOCK_ENCODING_GPU1;
   default:
      return AGX_IOTO_IMAGEBLOCK_ENCODING_UNSUPPORTED;
   }
}

bool
agx_ioto_imageblock_target_supported(enum agx_chip chip)
{
   return agx_ioto_imageblock_target_encoding(chip) !=
          AGX_IOTO_IMAGEBLOCK_ENCODING_UNSUPPORTED;
}

static uint32_t
agx_ioto_mul32(uint32_t a, uint32_t b)
{
   return a * b;
}

/* ARM CLZ defines the zero input as 32.  Keep that behavior portable instead
 * of relying on compiler builtins whose zero case is undefined. */
static uint32_t
agx_ioto_clz32(uint32_t value)
{
   if (value == 0)
      return 32;

   uint32_t count = 0;
   for (uint32_t bit = UINT32_C(1) << 31; (value & bit) == 0; bit >>= 1)
      ++count;

   return count;
}

static uint32_t
agx_ioto_ceil_pow2(uint32_t value)
{
   const uint32_t exponent =
      (UINT32_C(0) - agx_ioto_clz32(value - UINT32_C(1))) & UINT32_C(31);
   return UINT32_C(1) << exponent;
}

static uint32_t
agx_ioto_log_code(uint32_t value)
{
   switch (value) {
   case UINT32_C(2):
      return 1;
   case UINT32_C(4):
      return 2;
   case UINT32_C(8):
      return 3;
   case UINT32_C(16):
      return 4;
   case UINT32_C(32):
      return 5;
   case UINT32_C(64):
      return 6;
   case UINT32_C(128):
      return 7;
   case UINT32_C(256):
      return 8;
   case UINT32_C(512):
      return 9;
   case UINT32_C(1024):
      return 10;
   default:
      return 0;
   }
}

static uint32_t
agx_ioto_sample_code(enum agx_ioto_imageblock_encoding encoding,
                     uint32_t sample_count)
{
   if (sample_count == 2)
      return 1;
   if (sample_count == 4)
      return 2;
   if (encoding == AGX_IOTO_IMAGEBLOCK_ENCODING_GPU1 && sample_count == 8)
      return 3;
   return 0;
}

bool
agx_ioto_imageblock_transform(
   enum agx_chip chip, const struct agx_ioto_imageblock_params *params,
   struct agx_ioto_imageblock_result *result)
{
   if (result)
      memset(result, 0, sizeof(*result));

   const enum agx_ioto_imageblock_encoding encoding =
      agx_ioto_imageblock_target_encoding(chip);
   if (!params || !result ||
       encoding == AGX_IOTO_IMAGEBLOCK_ENCODING_UNSUPPORTED)
      return false;

   result->chip = chip;
   result->encoding = encoding;
   result->sample_count = params->p1 > 0 ? params->p1 : UINT32_C(1);
   result->p0_aligned = params->p0 & UINT32_C(0xfffffffc);

   result->first_product =
      agx_ioto_mul32(result->p0_aligned, result->sample_count);
   result->first_product =
      agx_ioto_mul32(result->first_product, params->p2);
   result->first_product =
      agx_ioto_mul32(result->first_product, params->p3);

   result->a = ((params->p0 >> 2) + UINT32_C(1)) & UINT32_C(0xfffffffe);
   result->x = agx_ioto_ceil_pow2(params->p2);
   result->y = agx_ioto_ceil_pow2(params->p3);
   result->four_a = agx_ioto_mul32(result->a, UINT32_C(4));

   result->second_product = agx_ioto_mul32(result->four_a, result->x);
   result->second_product =
      agx_ioto_mul32(result->second_product, result->y);
   result->second_product =
      agx_ioto_mul32(result->second_product, result->sample_count);
   result->active =
      result->first_product != 0 && result->second_product != 0;

   result->x_log_code = agx_ioto_log_code(result->x);
   result->y_log_code = agx_ioto_log_code(result->y);
   result->sample_code =
      agx_ioto_sample_code(encoding, result->sample_count);

   result->rounded_four_a =
      (result->four_a + UINT32_C(7)) & UINT32_C(0xfffffff8);
   result->q = result->rounded_four_a >> 3;
   result->q_field = result->q & UINT32_C(0x1f);

   if (result->active) {
      result->lo = (uint64_t)result->x_log_code |
                   ((uint64_t)result->y_log_code << 32);
      result->hi = (uint64_t)result->sample_code |
                   ((uint64_t)result->q_field << 35);
   }

   /* The shift, add and mask are each 32-bit operations in the constructor. */
   result->h_payload =
      ((params->p4 << 16) + UINT32_C(0x003f0000)) &
      UINT32_C(0xffc00000);
   result->h = (result->active ? UINT64_C(1) << 32 : UINT64_C(0)) |
               (uint64_t)result->h_payload |
               (params->p4 != 0 ? UINT64_C(1) : UINT64_C(0));
   return true;
}
