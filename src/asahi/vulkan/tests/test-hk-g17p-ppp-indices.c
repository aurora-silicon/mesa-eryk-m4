/* SPDX-License-Identifier: MIT */
#include "../hk_g17p_ppp_indices.h"

#include <assert.h>
#include <stdio.h>

static uint32_t
word(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
expect(size_t scissor, size_t bias, uint32_t payload)
{
   uint8_t bytes[8];
   memset(bytes, 0xa5, sizeof(bytes));
   assert(hk_g17p_ppp_indices_pack(scissor, bias, bytes));
   assert(word(bytes) == 0x100);
   assert(word(bytes + 4) == payload);
}

static void
reject(size_t scissor, size_t bias)
{
   uint8_t bytes[8], saved[8];
   memset(bytes, 0xa5, sizeof(bytes));
   memcpy(saved, bytes, sizeof(saved));
   assert(!hk_g17p_ppp_indices_pack(scissor, bias, bytes));
   assert(!memcmp(bytes, saved, sizeof(bytes)));
}

int
main(void)
{
   /* The first real records are index0 even when scissor testing is enabled.
    * Valid cold194053 PPP at1000048000 is this exact eight-byte form.
    */
   expect(16, 12, 0);
   expect(32, 24, 0x00010001);
   expect(16 * 4, 12 * 7, 0x00060003);
   expect(16 * 0x102, 12 * 0x304, 0x03030101);

   const size_t max_count = (size_t)UINT16_MAX + 1;
   expect(16 * max_count, 12 * max_count, UINT32_MAX);
   reject(16 * (max_count + 1), 12);
   reject(16, 12 * (max_count + 1));
   reject(0, 0);
   reject(0, 12);
   reject(16, 0);
   reject(15, 12);
   reject(17, 12);
   reject(16, 11);
   reject(16, 13);
   reject(SIZE_MAX, 12);
   reject(16, SIZE_MAX);
   assert(!hk_g17p_ppp_indices_pack(16, 12, NULL));

   puts("G17P PPP indices: exact first/retained/multi-record indices and bounds pass");
   return 0;
}
