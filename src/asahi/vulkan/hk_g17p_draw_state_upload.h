/* SPDX-License-Identifier: MIT */
#pragma once

#include "asahi/lib/agx_va_runtime_hole.h"
#include "asahi/lib/agx_vdm_hal200_bind_tail_core.h"
#include "hk_g17p_ppp_indices.h"

#define HK_G17P_BIND_GROUP_BYTES 0x80u
#define HK_G17P_VIEWPORT_RECORD_BYTES 40u
#define HK_G17P_BIND_STATE_BYTES \
   (HK_G17P_BIND_GROUP_BYTES + HK_G17P_VIEWPORT_RECORD_BYTES)

struct hk_g17p_bind_state_upload {
   struct agx_vdm_hal200_bind_pair pairs[AGX_VDM_HAL200_BIND_COUNT];
   struct agx_vdm_hal200_bind_tail tail;
};

/* hk_pool_vdm_alloc returns an offset from AGX_G17P_VDM_USER_BASE
 * (0x1000000000), never a USC address or ordinary main-heap VA. Keep that
 * established command-BO aperture and reject overflow before narrowing any
 * address. These helpers do not allocate/rebind/recycle memory: each caller
 * supplies fresh command-buffer-owned storage retained until CB reset.
 */
static inline bool
hk_g17p_draw_state_range_valid(uint64_t compact, size_t size)
{
   return size != 0 && compact >= AGX_G17P_VDM_ALIAS_OFFSET_START &&
          compact < AGX_G17P_VDM_ALIAS_OFFSET_END && (compact & 3) == 0 &&
          size <= AGX_G17P_VDM_ALIAS_OFFSET_END - compact;
}

static inline bool
hk_g17p_upload_bind_state(void *cpu, size_t capacity, uint64_t compact,
                          const uint8_t *bind_group, const uint8_t *viewport,
                          size_t viewport_size,
                          struct hk_g17p_bind_state_upload *out)
{
   if (!cpu || !bind_group || !viewport || !out ||
       capacity < HK_G17P_BIND_STATE_BYTES ||
       viewport_size != HK_G17P_VIEWPORT_RECORD_BYTES ||
       !hk_g17p_draw_state_range_valid(compact, HK_G17P_BIND_STATE_BYTES))
      return false;

   const uint32_t group = (uint32_t)compact;
   struct hk_g17p_bind_state_upload encoded = {
      .pairs = {
         {0x40, 0x700}, /* Fixed immutable Bind0: implicit walk unchanged. */
         {group + 0x00, 0x500},
         {group + 0x1c, 0x700},
         {group + 0x30, 0x500},
         {group + 0x4c, 0xa00},
         {group + HK_G17P_BIND_GROUP_BYTES, 0x300},
         {group + 0x60, 0x200},
         {group + 0x6c, 0x200},
      },
   };
   if (!agx_vdm_hal200_bind_tail_pack(AGX_CHIP_G17P, encoded.pairs,
                                       &encoded.tail))
      return false;
   memcpy(cpu, bind_group, HK_G17P_BIND_GROUP_BYTES);
   memcpy((uint8_t *)cpu + HK_G17P_BIND_GROUP_BYTES, viewport, viewport_size);
   *out = encoded;
   return true;
}

static inline bool
hk_g17p_upload_draw_indices(void *cpu, size_t capacity, uint64_t compact,
                            size_t scissor_bytes, size_t depth_bias_bytes,
                            uint32_t *offset)
{
   uint8_t indices[8];
   if (!cpu || !offset || capacity < sizeof(indices) ||
       !hk_g17p_draw_state_range_valid(compact, sizeof(indices)) ||
       !hk_g17p_ppp_indices_pack(scissor_bytes, depth_bias_bytes, indices))
      return false;
   memcpy(cpu, indices, sizeof(indices));
   *offset = (uint32_t)compact;
   return true;
}
