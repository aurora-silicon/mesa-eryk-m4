/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AGX_G17P_BIND0_BYTES 0x400u

/* The existing native compact-direct Bind0 image. The first VDM pair names
 * +0x40; surrounding records participate in its implicit hardware walk.
 * Initialize this fixed device-owned image once, before publishing the
 * render-context BO, instead of rewriting it while another CB may execute.
 */
static inline bool
agx_g17p_bind0_initialize(void *map, size_t size)
{
   if (!map || size < AGX_G17P_BIND0_BYTES)
      return false;
   uint8_t *bytes = map;
   memset(bytes, 0, AGX_G17P_BIND0_BYTES);
   for (unsigned i = 0; i < 7; ++i) {
      const size_t base = i * 0x80;
      bytes[base] = 0x80;
      bytes[base + 0x42] = 0x04;
      bytes[base + 0x43] = 0x10;
   }
   bytes[0x300] = 0xc0;
   bytes[0x301] = 0xfc;
   memset(bytes + 0x340, 0, 4);
   return true;
}
