/* SPDX-License-Identifier: MIT */
#pragma once

#include "asahi/lib/agx_chip.h"

#include <stdbool.h>
#include <stddef.h>

#define HK_CS_ROOT_SIZE       65536u
/* Each G17P draw is currently retained as a separate render. Keep its root
 * compact so those renders share command BOs instead of exhausting aliases,
 * but large enough for hk_flush_gfx_state's 0x2000 reservation after the
 * 12-byte barrier/initial-PPP opening and the control-stream safety padding.
 */
#define HK_G17P_VDM_ROOT_SIZE    12288u
#define HK_G17P_VDM_INITIAL_SIZE 12u
#define HK_GFX_DRAW_RESERVATION  0x2000u
#define HK_CS_SPACE_PADDING      0x800u

static inline size_t
hk_cs_root_size(enum agx_chip chip, bool compute)
{
   return chip == AGX_CHIP_G17P && !compute ? HK_G17P_VDM_ROOT_SIZE
                                            : HK_CS_ROOT_SIZE;
}

static inline bool
hk_cs_space_fits(size_t remaining, size_t requested, size_t link_length)
{
   return requested <= remaining && link_length <= remaining - requested &&
          HK_CS_SPACE_PADDING <= remaining - requested - link_length;
}
