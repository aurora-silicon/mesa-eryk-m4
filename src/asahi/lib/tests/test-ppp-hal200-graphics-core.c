/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_ppp_hal200_graphics_core.h"

#include <assert.h>
#include <string.h>

static void
check(enum agx_ppp_hal200_graphics_bundle bundle, const uint32_t *words,
      unsigned word_count, const uint8_t *expected, size_t expected_size)
{
   struct agx_ppp_hal200_graphics_fields fields = {0};
   memcpy(fields.words, words, word_count * sizeof(words[0]));

   struct agx_ppp_hal200_graphics_core packed;
   assert(agx_ppp_hal200_graphics_core_pack(AGX_CHIP_G17P, bundle, &fields,
                                             &packed));
   assert(packed.length == expected_size);
   assert(memcmp(packed.bytes, expected, expected_size) == 0);

   struct agx_ppp_hal200_graphics_fields unpacked;
   assert(agx_ppp_hal200_graphics_core_unpack(
      AGX_CHIP_G17P, bundle, packed.bytes, packed.length, &unpacked));
   assert(memcmp(unpacked.words, fields.words, sizeof(fields.words)) == 0);
}

int
main(void)
{
   static const uint32_t fs_words[] = {0x00020100, 0x000004c0, 0, 0x01000000};
   static const uint8_t fs_expected[] = {
      0x00, 0x00, 0x80, 0x00, 0x00, 0x01, 0x02, 0x00, 0xc0, 0x04,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
   };
   check(AGX_PPP_HAL200_GRAPHICS_FS, fs_words, 4, fs_expected,
         sizeof(fs_expected));

   static const uint32_t output_words[] = {
      0, 0, 0, 4, AGX_PPP_HAL200_PRIMARY_FACE_HEADER, 0x00010200,
   };
   static const uint8_t output_expected[] = {
      0x00, 0x00, 0x0a, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
      0xb4, 0x00, 0x00, 0x01, 0x00, 0x02, 0x01, 0x00,
   };
   check(AGX_PPP_HAL200_GRAPHICS_OUTPUT, output_words, 6, output_expected,
         sizeof(output_expected));

   static const uint32_t primary_words[] = {
      0x00010200, 0x07200f00, 0x0e00ffff,
      0x07200f00, 0x0e00ffff, 0,
   };
   static const uint8_t primary_expected[] = {
      0xb4, 0x00, 0x00, 0x01, 0x00, 0x02, 0x01, 0x00,
      0x00, 0x0f, 0x20, 0x07, 0xff, 0xff, 0x00, 0x0e,
      0x00, 0x0f, 0x20, 0x07, 0xff, 0xff, 0x00, 0x0e,
      0x00, 0x00, 0x00, 0x00,
   };
   check(AGX_PPP_HAL200_GRAPHICS_PRIMARY_FACE, primary_words, 6,
         primary_expected, sizeof(primary_expected));

   static const uint32_t secondary_words[] = {
      0x00000200, 0x07e00000, 0x07e00000, 0x0001ffff,
   };
   static const uint8_t secondary_expected[] = {
      0x48, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
      0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0xff, 0xff, 0x01, 0x00,
   };
   check(AGX_PPP_HAL200_GRAPHICS_SECONDARY_FACE, secondary_words, 4,
         secondary_expected, sizeof(secondary_expected));

   static const uint32_t cull_words[] = {0x00010480};
   static const uint8_t cull_expected[] = {
      0x00, 0x00, 0x20, 0x00, 0x80, 0x04, 0x01, 0x00,
   };
   check(AGX_PPP_HAL200_GRAPHICS_CULL, cull_words, 1, cull_expected,
         sizeof(cull_expected));

   struct agx_ppp_hal200_graphics_fields fields = {0};
   struct agx_ppp_hal200_graphics_core packed;
   assert(!agx_ppp_hal200_graphics_core_pack(
      AGX_CHIP_G17G, AGX_PPP_HAL200_GRAPHICS_FS, &fields, &packed));
   return 0;
}
