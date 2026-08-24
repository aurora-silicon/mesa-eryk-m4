/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define D12_CORE_FILL_MODE_WIREFRAME UINT32_C(2)
#define D12_CORE_FILL_MODE_SOLID UINT32_C(3)
#define D12_CORE_POLYGON_MODE_LINE UINT32_C(1)
#define D12_CORE_POLYGON_MODE_FILL UINT32_C(0)

static inline bool
d12_command_has_graphics_pipeline(bool has_pipeline, bool pipeline_is_compute)
{
   return has_pipeline && !pipeline_is_compute;
}

static inline bool
d12_command_has_compute_pipeline(bool has_pipeline, bool pipeline_is_compute)
{
   return has_pipeline && pipeline_is_compute;
}

static inline bool
d12_raster_polygon_mode(uint32_t fill_mode, uint32_t *polygon_mode)
{
   if (!polygon_mode)
      return false;

   switch (fill_mode) {
   case D12_CORE_FILL_MODE_WIREFRAME:
      *polygon_mode = D12_CORE_POLYGON_MODE_LINE;
      return true;
   case D12_CORE_FILL_MODE_SOLID:
      *polygon_mode = D12_CORE_POLYGON_MODE_FILL;
      return true;
   default:
      return false;
   }
}

/*
 * D3D12_RASTERIZER_DESC::ForcedSampleCount. Zero means the attachments decide.
 * The API's own legal set is {0, 1, 4, 8} plus an optional 16; AGX rasterizes
 * at 1, 2 or 4 samples (ail_effective_width_sa/ail_effective_height_sa model
 * no wider footprint, and Honeykrisp advertises the same set), so 8 and 16 are
 * refused rather than silently rasterized at a narrower count -- a shader
 * reading SV_Coverage would otherwise get a mask of the wrong width.
 *
 * 2 is accepted because it is expressible; the API never produces it, so this
 * is a superset that costs nothing.
 */
#define D12_CORE_MAX_FORCED_SAMPLE_COUNT UINT32_C(4)

/*
 * Sample counts this driver can rasterize and lay out.
 *
 * The limit is the driver's, not the descriptor's. The hardware sample-count
 * field is two bits holding log2(count)-1, so it encodes 8x perfectly well;
 * an M5 reports supportsTextureSampleCount(8) == 1. What stops 8x here is
 * everything downstream: AGX2 -- the only instruction set and descriptor
 * generation this driver targets -- advertises 1/2/4 through Honeykrisp,
 * agx_translate_sample_count encodes only 2 and 4, AIL models no footprint
 * wider than 4x (ail_effective_width_sa doubles only at 4), and
 * ail_get_max_tile_size aborts outright on the 128-byte block an 8x
 * 16-byte-per-pixel image would need.
 *
 * So the D3D12 answer stays a zero from CheckFeatureSupport, and refusing
 * here is what keeps an 8x request an E_NOTIMPL instead of an UNREACHABLE
 * inside the caller's process. Raising it is a real possibility on a part
 * this driver does not yet support, not a hardware impossibility.
 */
static inline bool
d12_sample_count_supported(uint32_t sample_count)
{
   return sample_count == 1 || sample_count == 2 || sample_count == 4;
}

static inline bool
d12_raster_forced_sample_count_supported(uint32_t forced_sample_count)
{
   switch (forced_sample_count) {
   case 0:
   case 1:
   case 2:
   case 4:
      return true;
   default:
      return false;
   }
}

/*
 * Target-independent rasterization is only expressible on AGX when nothing is
 * attached: the render command's sample count sizes the tilebuffer and drives
 * the rasterizer at once, so a forced count cannot differ from an attachment's
 * own count. D3D12 already requires no depth-stencil and single-sample render
 * targets whenever the count is forced, which leaves exactly the attachmentless
 * case -- the one TIR is for -- fully honourable.
 */
static inline uint32_t
d12_raster_rasterization_samples(uint32_t forced_sample_count,
                                 uint32_t attachment_samples,
                                 bool has_attachments)
{
   if (!forced_sample_count)
      return attachment_samples ? attachment_samples : 1u;
   if (has_attachments)
      return attachment_samples ? attachment_samples : 1u;
   return forced_sample_count;
}

static inline bool
d12_stream_output_view_valid(uint64_t buffer_location, uint64_t size_B,
                             uint64_t filled_size_location)
{
   /* D3D12 defines a zero-size view as unbound. The address fields are
    * ignored in that case and may contain arbitrary values. */
   if (!size_B)
      return true;

   return buffer_location && filled_size_location &&
          !(filled_size_location & (sizeof(uint32_t) - 1)) &&
          size_B <= UINT32_MAX;
}
