/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * D3D12 programmable sample positions packed into AGX's PPP_MULTISAMPLECTL.
 *
 * The register holds one byte per sample: the low nibble is X and the high
 * nibble is Y, both in sixteenths of a pixel measured from the pixel's top
 * left corner, with sample zero in the least significant byte.  D3D12 states
 * positions the same way but relative to the pixel CENTRE and signed, so the
 * translation is a bias of eight.
 *
 * That relationship is not assumed: packing D3D12's own standard positions
 * through this reproduces the three default register values Honeykrisp
 * programs, which the host tests assert.
 *
 * Keep this free of Windows and Mesa headers.
 */

#ifndef D12_SAMPLE_POSITION_CORE_H
#define D12_SAMPLE_POSITION_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One nibble per axis leaves four sample bytes in the 32-bit register, which
 * is also the most AGX rasterizes. */
#define D12_SAMPLE_POSITION_MAX_SAMPLES 4u

/* D3D12_SAMPLE_POSITION carries signed sixteenths from the pixel centre. */
#define D12_SAMPLE_POSITION_MIN (-8)
#define D12_SAMPLE_POSITION_MAX (7)

struct d12_sample_position {
   int8_t x;
   int8_t y;
};

/*
 * Pack `count` positions into a PPP_MULTISAMPLECTL value.
 *
 * False when the request cannot be expressed: a count AGX does not
 * rasterize, or a coordinate outside the range the register's nibbles can
 * hold. Refusing is the only safe answer -- a silently clamped position
 * changes where the hardware samples, which no application could detect
 * except as wrong pixels.
 */
static inline bool
d12_sample_positions_pack(uint32_t count,
                          const struct d12_sample_position *positions,
                          uint32_t *out)
{
   uint32_t packed = 0;

   if (!positions || !out)
      return false;
   if (count != 1 && count != 2 && count != 4)
      return false;

   for (uint32_t i = 0; i < count; i++) {
      const int32_t x = positions[i].x;
      const int32_t y = positions[i].y;

      if (x < D12_SAMPLE_POSITION_MIN || x > D12_SAMPLE_POSITION_MAX ||
          y < D12_SAMPLE_POSITION_MIN || y > D12_SAMPLE_POSITION_MAX)
         return false;

      const uint32_t nx = (uint32_t)(x + 8);
      const uint32_t ny = (uint32_t)(y + 8);
      packed |= ((ny << 4) | nx) << (8u * i);
   }

   *out = packed;
   return true;
}

/*
 * The standard D3D12 positions for a sample count, which are what the
 * hardware uses when an application programs none. Kept here so the default
 * register values and the programmed ones come from one encoder.
 */
static inline bool
d12_sample_positions_standard(uint32_t count,
                              struct d12_sample_position *positions)
{
   static const struct d12_sample_position one[] = {{0, 0}};
   static const struct d12_sample_position two[] = {{4, 4}, {-4, -4}};
   static const struct d12_sample_position four[] = {
      {-2, -6}, {6, -2}, {-6, 2}, {2, 6},
   };
   const struct d12_sample_position *source;

   if (!positions)
      return false;

   switch (count) {
   case 1: source = one; break;
   case 2: source = two; break;
   case 4: source = four; break;
   default: return false;
   }

   for (uint32_t i = 0; i < count; i++)
      positions[i] = source[i];
   return true;
}

/*
 * Programmable sample positions, tier 1: every pixel in the render target
 * uses the same set. AGX has one PPP_MULTISAMPLECTL for the whole render, so
 * a per-pixel pattern (tier 2) has nowhere to live.
 *
 * A zero pixel count is D3D12's spelling for "return to the defaults".
 */
static inline bool
d12_sample_positions_tier1_request_valid(uint32_t samples_per_pixel,
                                         uint32_t pixel_count,
                                         uint32_t render_target_samples)
{
   if (!samples_per_pixel && !pixel_count)
      return true;

   if (pixel_count != 1)
      return false;
   if (samples_per_pixel != 1 && samples_per_pixel != 2 &&
       samples_per_pixel != 4)
      return false;

   /* The pattern describes the render target's samples, so it has to have
    * as many entries as the target has. Zero means no target is bound yet
    * and the count cannot be checked here. */
   return !render_target_samples || samples_per_pixel == render_target_samples;
}

#ifdef __cplusplus
}
#endif

#endif
