/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_command_validation_core.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
   assert(!d12_command_has_graphics_pipeline(false, false));
   assert(!d12_command_has_graphics_pipeline(false, true));
   assert(d12_command_has_graphics_pipeline(true, false));
   assert(!d12_command_has_graphics_pipeline(true, true));

   assert(!d12_command_has_compute_pipeline(false, false));
   assert(!d12_command_has_compute_pipeline(false, true));
   assert(!d12_command_has_compute_pipeline(true, false));
   assert(d12_command_has_compute_pipeline(true, true));

   uint32_t polygon_mode = UINT32_MAX;
   assert(d12_raster_polygon_mode(D12_CORE_FILL_MODE_SOLID, &polygon_mode));
   assert(polygon_mode == D12_CORE_POLYGON_MODE_FILL);
   assert(d12_raster_polygon_mode(D12_CORE_FILL_MODE_WIREFRAME,
                                  &polygon_mode));
   assert(polygon_mode == D12_CORE_POLYGON_MODE_LINE);
   assert(!d12_raster_polygon_mode(0, &polygon_mode));
   assert(!d12_raster_polygon_mode(D12_CORE_FILL_MODE_SOLID, NULL));

   assert(d12_stream_output_view_valid(0, 0, 0));
   assert(d12_stream_output_view_valid(0x10000, 4096, 0x20000));
   assert(!d12_stream_output_view_valid(0, 4096, 0x20000));
   assert(d12_stream_output_view_valid(0x10000, 0, 0x20003));
   assert(!d12_stream_output_view_valid(0x10000, 4096, 0));
   assert(!d12_stream_output_view_valid(0x10000, 4096, 0x20002));
   assert(!d12_stream_output_view_valid(0x10000,
                                        (uint64_t)UINT32_MAX + 1,
                                        0x20000));

   /* ForcedSampleCount: the API's legal set is {0, 1, 4, 8} plus optional 16.
    * AGX rasterizes at most 4 samples, so 8 and 16 must be refused rather
    * than silently narrowed -- SV_Coverage would be the wrong width. */
   assert(d12_raster_forced_sample_count_supported(0));
   assert(d12_raster_forced_sample_count_supported(1));
   assert(d12_raster_forced_sample_count_supported(2));
   assert(d12_raster_forced_sample_count_supported(4));
   assert(!d12_raster_forced_sample_count_supported(3));
   assert(!d12_raster_forced_sample_count_supported(8));
   assert(!d12_raster_forced_sample_count_supported(16));
   assert(!d12_raster_forced_sample_count_supported(UINT32_MAX));

   /* Not forced: the attachments decide, and an attachmentless pass still
    * rasterizes at one sample. */
   assert(d12_raster_rasterization_samples(0, 4, true) == 4);
   assert(d12_raster_rasterization_samples(0, 1, true) == 1);
   assert(d12_raster_rasterization_samples(0, 0, false) == 1);

   /* Forced with nothing attached is exactly expressible. */
   assert(d12_raster_rasterization_samples(4, 0, false) == 4);
   assert(d12_raster_rasterization_samples(2, 0, false) == 2);
   assert(d12_raster_rasterization_samples(1, 0, false) == 1);

   /* Forced with something attached cannot differ from the attachment: the
    * render command's sample count sizes the tilebuffer and drives the
    * rasterizer at once. */
   assert(d12_raster_rasterization_samples(4, 1, true) == 1);
   assert(d12_raster_rasterization_samples(1, 4, true) == 4);

   /* AGX rasterizes 1, 2 and 4 samples. Anything else must be refused, not
    * encoded: agx_translate_sample_count aborts on an unknown count. */
   assert(d12_sample_count_supported(1));
   assert(d12_sample_count_supported(2));
   assert(d12_sample_count_supported(4));
   assert(!d12_sample_count_supported(0));
   assert(!d12_sample_count_supported(3));
   assert(!d12_sample_count_supported(8));
   assert(!d12_sample_count_supported(16));
   assert(!d12_sample_count_supported(UINT32_MAX));

   /* Descriptor capability and render capability are different on M5.
    * DescriptorGen10 has a live-measured 8x resource layout, while HAL300's
    * render utile_config remains limited to 4x. A18 Pro is Gen9/HAL200. */
   assert(d12_resource_sample_count_representable(
      D12_AGX_DESCRIPTOR_GENERATION_9, 4));
   assert(!d12_resource_sample_count_representable(
      D12_AGX_DESCRIPTOR_GENERATION_9, 8));
   assert(d12_resource_sample_count_representable(
      D12_AGX_DESCRIPTOR_GENERATION_10, 8));
   assert(!d12_resource_sample_count_representable(
      D12_AGX_DESCRIPTOR_GENERATION_10, 16));
   assert(!d12_resource_sample_count_representable(0, 4));
   assert(d12_raster_sample_count_supported(D12_AGX_CS_GENERATION_HAL200, 4));
   assert(d12_raster_sample_count_supported(D12_AGX_CS_GENERATION_HAL300, 4));
   assert(!d12_raster_sample_count_supported(D12_AGX_CS_GENERATION_HAL300, 8));
   assert(!d12_raster_sample_count_supported(
      D12_AGX_CS_GENERATION_HAL300 + 1, 4));

   assert(!d12_native_depth_bounds_present(D12_AGX_CS_GENERATION_AGX2));
   assert(!d12_native_depth_bounds_present(D12_AGX_CS_GENERATION_G15));
   assert(!d12_native_depth_bounds_present(D12_AGX_CS_GENERATION_HAL200));
   assert(d12_native_depth_bounds_present(D12_AGX_CS_GENERATION_HAL300));
   assert(!d12_depth_bounds_driver_supported(
      D12_AGX_CS_GENERATION_HAL300, false));
   assert(d12_depth_bounds_driver_supported(
      D12_AGX_CS_GENERATION_HAL300, true));

   struct d12_depth_bounds bounds = d12_depth_bounds_canonicalize(-0.5f, 2.0f);
   assert(bounds.minimum == 0.0f && bounds.maximum == 1.0f);
   bounds = d12_depth_bounds_canonicalize(0.75f, 0.25f);
   assert(bounds.minimum == 0.25f && bounds.maximum == 0.25f);
   bounds = d12_depth_bounds_canonicalize(0.2f, 0.8f);
   assert(bounds.minimum == 0.2f && bounds.maximum == 0.8f);

   puts("d12_command_validation_core_tests: PASS");
   return 0;
}
