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

   puts("d12_command_validation_core_tests: PASS");
   return 0;
}
