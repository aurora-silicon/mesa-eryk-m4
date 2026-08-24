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

/* Independently-versioned AGX hardware axes used by the portable validation
 * policy. The values intentionally match agx_chip.h so an integrated driver
 * can pass them directly; this header remains free of Mesa and DRM types. */
#define D12_AGX_CS_GENERATION_AGX2   0u
#define D12_AGX_CS_GENERATION_G15    1u
#define D12_AGX_CS_GENERATION_HAL200 2u
#define D12_AGX_CS_GENERATION_HAL300 3u

#define D12_AGX_DESCRIPTOR_GENERATION_7  7u
#define D12_AGX_DESCRIPTOR_GENERATION_8  8u
#define D12_AGX_DESCRIPTOR_GENERATION_9  9u
#define D12_AGX_DESCRIPTOR_GENERATION_10 10u

/*
 * Sample counts this driver can rasterize and lay out.
 *
 * The limit is the raster command path, not descriptor storage or allocation.
 * DescriptorGen10 spells 8x and AIL now models the live-measured M5 2x4
 * footprint, including the 128/256-byte blocks needed by wide formats.
 * HAL300's Gen7 PPP utile_config and the measured raster capability remain
 * capped at 4x, however, and the currently runnable AGX2 path likewise
 * advertises only 1/2/4 through Honeykrisp.
 *
 * So the D3D12 answer stays a zero from CheckFeatureSupport, and refusing
 * here is what keeps an 8x request an E_NOTIMPL instead of an UNREACHABLE
 * inside the caller's process. An M5 8x allocation may be representable
 * without being a legal 8x render target; keep those capabilities separate.
 */
static inline bool
d12_sample_count_supported(uint32_t sample_count)
{
   return sample_count == 1 || sample_count == 2 || sample_count == 4;
}

/* A resource being representable is not the same as it being rasterizable.
 *
 * M5/DescriptorGen10 has a proven 8x footprint and two-bit sample-count
 * field, and Apple's allocation oracle accepted every row in the dedicated
 * 8x sweep. M4 and A18 Pro are DescriptorGen9: the field could spell 8, but
 * those plugins do not instantiate the 8x lossless compressor, so accepting
 * it there would infer capability from field width alone. */
static inline bool
d12_resource_sample_count_representable(uint32_t descriptor_generation,
                                        uint32_t sample_count)
{
   if (sample_count == 1 || sample_count == 2 || sample_count == 4)
      return descriptor_generation >= D12_AGX_DESCRIPTOR_GENERATION_7 &&
             descriptor_generation <= D12_AGX_DESCRIPTOR_GENERATION_10;

   return descriptor_generation == D12_AGX_DESCRIPTOR_GENERATION_10 &&
          sample_count == 8;
}

/* D3D12_RESOURCE_DESC permits a multisampled texture only when it is created
 * for render-target or depth/stencil use. This is an API contract independent
 * of the hardware sample-count ceiling; callers must still check whether the
 * requested count itself is supported. */
static inline bool
d12_resource_multisample_flags_valid(uint32_t sample_count,
                                     bool allow_render_target,
                                     bool allow_depth_stencil)
{
   return sample_count <= 1 || allow_render_target || allow_depth_stencil;
}

/* The HAL300 render-state encoder still builds utile_config with only the
 * 2x and 4x selectors, and the A-grade Gen7 PPP sweep reports max samples=4.
 * Therefore an M5 8x resource may be represented/sampled, but must not be
 * advertised as an 8x render target or used for ForcedSampleCount. */
static inline bool
d12_raster_sample_count_supported(uint32_t command_stream_generation,
                                  uint32_t sample_count)
{
   const bool known_generation =
      command_stream_generation <= D12_AGX_CS_GENERATION_HAL300;
   return known_generation && d12_sample_count_supported(sample_count);
}

/* Native depth bounds first appears in HAL300's PPPEncoderGen7. The feature
 * may be advertised only after the command writer can emit its three-word
 * PPP block and the associated two-float array. */
static inline bool
d12_native_depth_bounds_present(uint32_t command_stream_generation)
{
   return command_stream_generation == D12_AGX_CS_GENERATION_HAL300;
}

static inline bool
d12_depth_bounds_driver_supported(uint32_t command_stream_generation,
                                  bool gen7_ppp_encoder_complete)
{
   return d12_native_depth_bounds_present(command_stream_generation) &&
          gen7_ppp_encoder_complete;
}

struct d12_depth_bounds {
   float minimum;
   float maximum;
};

/* Match Apple's HAL300 setter: clamp min upward at zero and max downward at
 * one, then clamp min down to max.
 * Keeping this portable lets the COM and WDDM frontends share the exact
 * dynamic-state semantics once the Gen7 PPP writer is connected. */
static inline struct d12_depth_bounds
d12_depth_bounds_canonicalize(float minimum, float maximum)
{
   if (minimum < 0.0f)
      minimum = 0.0f;
   if (maximum > 1.0f)
      maximum = 1.0f;
   if (minimum > maximum)
      minimum = maximum;

   return (struct d12_depth_bounds){minimum, maximum};
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
