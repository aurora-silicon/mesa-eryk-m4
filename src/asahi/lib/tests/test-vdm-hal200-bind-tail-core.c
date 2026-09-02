/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_vdm_hal200_bind_tail_core.h"

#include <assert.h>
#include <string.h>

int
main(void)
{
   static const struct agx_vdm_hal200_bind_pair pairs[] = {
      {0x00000040, 0x700}, {0x00058000, 0x500},
      {0x0005801c, 0x700}, {0x00058030, 0x500},
      {0x0005804c, 0xa00}, {0x00068900, 0x300},
      {0x00058060, 0x200}, {0x0005806c, 0x200},
   };
   static const uint32_t expected_words[] = {
      0x00000500,
      0x00000040, 0x00000700,
      0x00058000, 0x00000500,
      0x0005801c, 0x00000700,
      0x00058030, 0x00000500,
      0x0005804c, 0x00000a00,
      0x00068900, 0x00000300,
      0x00058060, 0x00000200,
      0x0005806c, 0x00000200,
   };

   struct agx_vdm_hal200_bind_tail packed;
   assert(agx_vdm_hal200_bind_tail_pack(AGX_CHIP_G17P, pairs, &packed));
   assert(packed.length == sizeof(expected_words));
   assert(memcmp(packed.bytes, expected_words, sizeof(expected_words)) == 0);

   struct agx_vdm_hal200_bind_pair unpacked[AGX_VDM_HAL200_BIND_COUNT];
   assert(agx_vdm_hal200_bind_tail_unpack(
      AGX_CHIP_G17P, packed.bytes, packed.length, unpacked));
   assert(memcmp(unpacked, pairs, sizeof(pairs)) == 0);

   assert(!agx_vdm_hal200_bind_tail_pack(AGX_CHIP_G17G, pairs, &packed));

   struct agx_vdm_hal200_bind_pair bad[AGX_VDM_HAL200_BIND_COUNT];
   memcpy(bad, pairs, sizeof(bad));
   bad[3].control = 0x700;
   assert(!agx_vdm_hal200_bind_tail_pack(AGX_CHIP_G17P, bad, &packed));
   memcpy(bad, pairs, sizeof(bad));
   bad[0].context_offset = 2;
   assert(!agx_vdm_hal200_bind_tail_pack(AGX_CHIP_G17P, bad, &packed));
   return 0;
}
