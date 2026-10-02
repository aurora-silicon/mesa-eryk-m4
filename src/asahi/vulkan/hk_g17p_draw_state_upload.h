/* SPDX-License-Identifier: MIT */
#pragma once

#include "asahi/lib/agx_va_runtime_hole.h"
#include "asahi/lib/agx_vdm_hal200_bind_tail_core.h"
#include "hk_g17p_ppp_indices.h"
#include "asahi/lib/agx_ppp_viewport_payload_core.h"

#define HK_G17P_BIND_GROUP_BYTES 0x80u
#define HK_G17P_VIEWPORT_RECORD_BYTES 40u
#define HK_G17P_VARYING_RECORD_BYTES 20u
#define HK_G17P_VARYING_RECORD_OFFSET \
   (HK_G17P_BIND_GROUP_BYTES + HK_G17P_VIEWPORT_RECORD_BYTES)
#define HK_G17P_BIND_STATE_BYTES \
   (HK_G17P_VARYING_RECORD_OFFSET + HK_G17P_VARYING_RECORD_BYTES)

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
                          size_t viewport_size, uint32_t counts_32, uint32_t counts_16,
                          struct hk_g17p_bind_state_upload *out)
{
   const size_t varying_offset = HK_G17P_BIND_GROUP_BYTES + viewport_size;
   const size_t state_size = varying_offset + HK_G17P_VARYING_RECORD_BYTES;
   if (!cpu || !bind_group || !viewport || !out ||
       viewport_size < AGX_PPP_VIEWPORT_PAYLOAD_MIN_LENGTH ||
       viewport_size > AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH ||
       (viewport_size - AGX_PPP_VIEWPORT_PAYLOAD_MIN_LENGTH) % 32 ||
       capacity < state_size ||
       ((counts_32 | counts_16) & UINT32_C(0xff000000)) != 0 ||
       !hk_g17p_draw_state_range_valid(compact, state_size))
      return false;

   const uint32_t group = (uint32_t)compact;
   struct hk_g17p_bind_state_upload encoded = {
      .pairs = {
         {group + (uint32_t)varying_offset, 0x700},
         {group + 0x00, 0x500},
         {group + 0x1c, 0x700},
         {group + 0x30, 0x500},
         {group + 0x4c, (uint32_t)(viewport_size / 4) << 8},
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
   /* Native VertexProgramVariant+0x1008 (35b628..35b694): separate
    * f32/f16 smooth, flat, linear counts followed by two auxiliary words.
    * The admitted user-only layout has no auxiliary outputs. This record
    * belongs to the draw, just like FS/OUTPUT, never the shared Bind0 page. */
   const uint32_t words[5] = {UINT32_C(0x10040000), counts_32, counts_16, 0, 0};
   uint8_t *varyings = (uint8_t *)cpu + varying_offset;
   for (unsigned i = 0; i < 5; ++i) {
      for (unsigned byte = 0; byte < 4; ++byte)
         varyings[4 * i + byte] = words[i] >> (8 * byte);
   }
   *out = encoded;
   return true;
}

/* Match every uploaded byte, including viewport count/transform and varying
 * counts. Only immutable command-pool allocations may be reused; this helper
 * never writes an allocation already referenced by a recorded draw. */
static inline bool
hk_g17p_bind_state_matches(const void *cpu, size_t size,
                          const uint8_t *bind_group, const uint8_t *viewport,
                          size_t viewport_size, uint32_t counts_32,
                          uint32_t counts_16)
{
   if (!cpu || !bind_group || !viewport ||
       viewport_size < AGX_PPP_VIEWPORT_PAYLOAD_MIN_LENGTH ||
       viewport_size > AGX_PPP_VIEWPORT_PAYLOAD_MAX_LENGTH ||
       (viewport_size - AGX_PPP_VIEWPORT_PAYLOAD_MIN_LENGTH) % 32 ||
       size != HK_G17P_BIND_GROUP_BYTES + viewport_size +
                  HK_G17P_VARYING_RECORD_BYTES ||
       ((counts_32 | counts_16) & UINT32_C(0xff000000)))
      return false;

   const uint32_t words[5] = {UINT32_C(0x10040000), counts_32, counts_16, 0, 0};
   uint8_t varying[HK_G17P_VARYING_RECORD_BYTES];
   for (unsigned i = 0; i < 5; ++i)
      for (unsigned b = 0; b < 4; ++b)
         varying[4 * i + b] = words[i] >> (8 * b);

   const uint8_t *stored = cpu;
   return !memcmp(stored, bind_group, HK_G17P_BIND_GROUP_BYTES) &&
          !memcmp(stored + HK_G17P_BIND_GROUP_BYTES, viewport, viewport_size) &&
          !memcmp(stored + HK_G17P_BIND_GROUP_BYTES + viewport_size,
                  varying, sizeof(varying));
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
