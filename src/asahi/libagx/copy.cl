/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"

KERNEL(32)
libagx_fill(global uint32_t *address, uint32_t value)
{
   assert((((uintptr_t)address) & 0x3) == 0);

   address[cl_global_id.x] = value;
}

KERNEL(32)
libagx_copy_uint4(global uint4 *dest, global uint4 *src)
{
   assert((((uintptr_t)dest) & 0xf) == 0);
   assert((((uintptr_t)src) & 0xf) == 0);

   dest[cl_global_id.x] = src[cl_global_id.x];
}

KERNEL(16)
libagx_copy_uchar(global uint8_t *dest, global uint8_t *src)
{
   dest[cl_global_id.x] = src[cl_global_id.x];
}

/*
 * Copy a rectangle from an AIL GPU-tiled image into a linear buffer. This is
 * the device-side form of layout/tiling.cc::memcpy_small: tiles are in raster
 * order, while elements inside each power-of-two tile use Morton order.
 *
 * Keeping this raw (rather than formatted texture loads) preserves exact bits
 * for depth/stencil and typeless copies, as D3D12 CopyTextureRegion requires.
 */
static uint
libagx_space_bits(uint x)
{
   uint accum = 0;
   for (uint i = 0; i < 14; ++i)
      accum |= (x & (1u << i)) << i;
   return accum;
}

KERNEL(32)
libagx_detile(global uint8_t *dest, global uint8_t *src,
              uint dest_pitch_B, uint width_el, uint height_el,
              uint src_x_el, uint src_y_el, uint stride_el,
              uint tile_width_el, uint tile_height_el, uint blocksize_B)
{
   uint id = cl_global_id.x;
   if (id >= width_el * height_el)
      return;

   uint x = src_x_el + (id % width_el);
   uint y = src_y_el + (id / width_el);
   uint tiles_per_row = (stride_el + tile_width_el - 1) / tile_width_el;
   uint tile_idx = (y / tile_height_el) * tiles_per_row +
                   (x / tile_width_el);
   uint tile_area_el = tile_width_el * tile_height_el;
   uint morton_el = libagx_space_bits(x & (tile_width_el - 1)) |
                    (libagx_space_bits(y & (tile_height_el - 1)) << 1);
   uint src_offset_B = (tile_idx * tile_area_el + morton_el) * blocksize_B;
   uint dst_offset_B = (id / width_el) * dest_pitch_B +
                       (id % width_el) * blocksize_B;

   for (uint b = 0; b < blocksize_B; ++b)
      dest[dst_offset_B + b] = src[src_offset_B + b];
}

/*
 * Raw D3D12 texture copies operate on format elements, not interpreted
 * colours.  An element is a pixel for ordinary formats and a whole block for
 * BC formats.  Keeping both sides in element coordinates also implements the
 * specified BC<->RGBA32 copy compatibility without format conversion: one
 * 16-byte BC block is copied to or from one 16-byte RGBA32 pixel.
 *
 * CPU command recording validates every rectangle against the logical image
 * or placed footprint before this kernel is dispatched.  The id guard is a
 * second line of defence against an over-wide launch.
 */
static uint
libagx_copy_image_offset(uint x, uint y, uint pitch_B, uint stride_el,
                         uint tile_width_el, uint tile_height_el,
                         uint blocksize_B, uint tiled)
{
   if (!tiled)
      return y * pitch_B + x * blocksize_B;

   uint tiles_per_row = (stride_el + tile_width_el - 1) / tile_width_el;
   uint tile_idx = (y / tile_height_el) * tiles_per_row +
                   (x / tile_width_el);
   uint tile_area_el = tile_width_el * tile_height_el;
   uint morton_el = libagx_space_bits(x & (tile_width_el - 1)) |
                    (libagx_space_bits(y & (tile_height_el - 1)) << 1);

   return (tile_idx * tile_area_el + morton_el) * blocksize_B;
}

KERNEL(32)
libagx_copy_image(global uint8_t *dest, global uint8_t *src,
                  uint width_el, uint height_el,
                  uint dest_x_el, uint dest_y_el,
                  uint src_x_el, uint src_y_el,
                  uint dest_pitch_B, uint src_pitch_B,
                  uint dest_stride_el, uint src_stride_el,
                  uint dest_tile_width_el, uint dest_tile_height_el,
                  uint src_tile_width_el, uint src_tile_height_el,
                  uint blocksize_B, uint dest_tiled, uint src_tiled)
{
   uint id = cl_global_id.x;
   if (id >= width_el * height_el)
      return;

   uint x = id % width_el;
   uint y = id / width_el;
   uint dest_offset_B = libagx_copy_image_offset(
      dest_x_el + x, dest_y_el + y, dest_pitch_B, dest_stride_el,
      dest_tile_width_el, dest_tile_height_el, blocksize_B, dest_tiled);
   uint src_offset_B = libagx_copy_image_offset(
      src_x_el + x, src_y_el + y, src_pitch_B, src_stride_el,
      src_tile_width_el, src_tile_height_el, blocksize_B, src_tiled);

   for (uint b = 0; b < blocksize_B; ++b)
      dest[dest_offset_B + b] = src[src_offset_B + b];
}

/*
 * Resolve an unorm8x4 multisample surface into a single-sample surface.
 * AIL tiles one logical pixel as a block containing every sample. Averaging
 * bytes is format-order agnostic for the four 8-bit UNORM channel formats
 * this entry point accepts on the CPU side.
 */
KERNEL(32)
libagx_resolve_unorm8x4(global uint8_t *dest, global uint8_t *src,
                        uint width_px, uint height_px,
                        uint dest_x_px, uint dest_y_px,
                        uint src_x_px, uint src_y_px,
                        uint dest_pitch_B, uint dest_stride_el,
                        uint src_stride_el,
                        uint dest_tile_width_el, uint dest_tile_height_el,
                        uint src_tile_width_el, uint src_tile_height_el,
                        uint sample_count, uint dest_tiled, uint src_tiled)
{
   uint id = cl_global_id.x;
   if (id >= width_px * height_px)
      return;

   uint x = id % width_px;
   uint y = id / width_px;
   uint dest_offset_B = libagx_copy_image_offset(
      dest_x_px + x, dest_y_px + y, dest_pitch_B, dest_stride_el,
      dest_tile_width_el, dest_tile_height_el, 4, dest_tiled);

   for (uint b = 0; b < 4; ++b) {
      uint sum = 0;
      for (uint s = 0; s < sample_count; ++s) {
         uint src_offset_B = libagx_copy_image_offset(
            src_x_px + x, src_y_px + y, 0, src_stride_el,
            src_tile_width_el, src_tile_height_el,
            4 * sample_count, src_tiled);
         sum += src[src_offset_B + s * 4 + b];
      }

      dest[dest_offset_B + b] = (sum + sample_count / 2) / sample_count;
   }
}

/* Resolve the R32G32B32A32_FLOAT shape used by the render-target sample-count
 * probes. Each AIL pixel block contains sample_count consecutive float4s. */
KERNEL(32)
libagx_resolve_float32x4(global float *dest, global float *src,
                         uint width_px, uint height_px,
                         uint dest_x_px, uint dest_y_px,
                         uint src_x_px, uint src_y_px,
                         uint dest_pitch_B, uint dest_stride_el,
                         uint src_stride_el,
                         uint dest_tile_width_el, uint dest_tile_height_el,
                         uint src_tile_width_el, uint src_tile_height_el,
                         uint sample_count, uint dest_tiled, uint src_tiled)
{
   uint id = cl_global_id.x;
   if (id >= width_px * height_px)
      return;

   uint x = id % width_px;
   uint y = id / width_px;
   uint dest_offset_B = libagx_copy_image_offset(
      dest_x_px + x, dest_y_px + y, dest_pitch_B, dest_stride_el,
      dest_tile_width_el, dest_tile_height_el, 16, dest_tiled);
   uint src_offset_B = libagx_copy_image_offset(
      src_x_px + x, src_y_px + y, 0, src_stride_el,
      src_tile_width_el, src_tile_height_el,
      16 * sample_count, src_tiled);

   for (uint c = 0; c < 4; ++c) {
      float sum = 0.0f;
      for (uint s = 0; s < sample_count; ++s)
         sum += src[(src_offset_B + s * 16) / 4 + c];

      dest[dest_offset_B / 4 + c] = sum / (float)sample_count;
   }
}

/* Resolve a multisampled 32-bit float depth plane with D3D12's MIN mode. */
KERNEL(32)
libagx_resolve_float32_min(global float *dest, global float *src,
                           uint width_px, uint height_px,
                           uint dest_x_px, uint dest_y_px,
                           uint src_x_px, uint src_y_px,
                           uint dest_pitch_B, uint dest_stride_el,
                           uint src_stride_el,
                           uint dest_tile_width_el, uint dest_tile_height_el,
                           uint src_tile_width_el, uint src_tile_height_el,
                           uint sample_count, uint dest_tiled, uint src_tiled)
{
   uint id = cl_global_id.x;
   if (id >= width_px * height_px)
      return;

   uint x = id % width_px;
   uint y = id / width_px;
   uint dest_offset_B = libagx_copy_image_offset(
      dest_x_px + x, dest_y_px + y, dest_pitch_B, dest_stride_el,
      dest_tile_width_el, dest_tile_height_el, 4, dest_tiled);
   uint src_offset_B = libagx_copy_image_offset(
      src_x_px + x, src_y_px + y, 0, src_stride_el,
      src_tile_width_el, src_tile_height_el,
      4 * sample_count, src_tiled);

   float value = src[src_offset_B / 4];
   for (uint s = 1; s < sample_count; ++s)
      value = fmin(value, src[src_offset_B / 4 + s]);

   dest[dest_offset_B / 4] = value;
}

/* Resolve one 32-bit float channel by averaging all samples. This is the
 * scalar counterpart of libagx_resolve_float32x4 and also covers a D32_FLOAT
 * source resolved through the compatible R32_FLOAT view required by D3D12. */
KERNEL(32)
libagx_resolve_float32_average(global float *dest, global float *src,
                               uint width_px, uint height_px,
                               uint dest_x_px, uint dest_y_px,
                               uint src_x_px, uint src_y_px,
                               uint dest_pitch_B, uint dest_stride_el,
                               uint src_stride_el,
                               uint dest_tile_width_el,
                               uint dest_tile_height_el,
                               uint src_tile_width_el,
                               uint src_tile_height_el,
                               uint sample_count, uint dest_tiled,
                               uint src_tiled)
{
   uint id = cl_global_id.x;
   if (id >= width_px * height_px)
      return;

   uint x = id % width_px;
   uint y = id / width_px;
   uint dest_offset_B = libagx_copy_image_offset(
      dest_x_px + x, dest_y_px + y, dest_pitch_B, dest_stride_el,
      dest_tile_width_el, dest_tile_height_el, 4, dest_tiled);
   uint src_offset_B = libagx_copy_image_offset(
      src_x_px + x, src_y_px + y, 0, src_stride_el,
      src_tile_width_el, src_tile_height_el,
      4 * sample_count, src_tiled);

   float sum = 0.0f;
   for (uint s = 0; s < sample_count; ++s)
      sum += src[src_offset_B / 4 + s];

   dest[dest_offset_B / 4] = sum / (float)sample_count;
}

/* Resolve the scalar MIN/MAX modes used by 32-bit float/integer colour and
 * depth planes, plus the separate 8-bit stencil plane. Operation values are
 * private to d3d12agx: 0=float, 1=uint, 2=sint; choose_max selects the
 * extremum. Keeping the scalar size explicit makes every address bounded by
 * the same checked surface geometry used by the copy kernels. */
KERNEL(32)
libagx_resolve_scalar_extrema(global uint8_t *dest, global uint8_t *src,
                              uint width_px, uint height_px,
                              uint dest_x_px, uint dest_y_px,
                              uint src_x_px, uint src_y_px,
                              uint dest_pitch_B, uint dest_stride_el,
                              uint src_stride_el,
                              uint dest_tile_width_el,
                              uint dest_tile_height_el,
                              uint src_tile_width_el,
                              uint src_tile_height_el,
                              uint sample_count, uint dest_tiled,
                              uint src_tiled, uint scalar_size_B,
                              uint operation, uint choose_max)
{
   uint id = cl_global_id.x;
   if (id >= width_px * height_px)
      return;

   uint x = id % width_px;
   uint y = id / width_px;
   uint dest_offset_B = libagx_copy_image_offset(
      dest_x_px + x, dest_y_px + y, dest_pitch_B, dest_stride_el,
      dest_tile_width_el, dest_tile_height_el, scalar_size_B, dest_tiled);
   uint src_offset_B = libagx_copy_image_offset(
      src_x_px + x, src_y_px + y, 0, src_stride_el,
      src_tile_width_el, src_tile_height_el,
      scalar_size_B * sample_count, src_tiled);

   if (scalar_size_B == 1) {
      uint8_t value = src[src_offset_B];
      for (uint s = 1; s < sample_count; ++s) {
         uint8_t next = src[src_offset_B + s];
         value = choose_max ? max(value, next) : min(value, next);
      }
      dest[dest_offset_B] = value;
   } else if (operation == 0) {
      global float *src_f = (global float *)(src + src_offset_B);
      global float *dest_f = (global float *)(dest + dest_offset_B);
      float value = src_f[0];
      for (uint s = 1; s < sample_count; ++s)
         value = choose_max ? fmax(value, src_f[s]) : fmin(value, src_f[s]);
      dest_f[0] = value;
   } else if (operation == 1) {
      global uint *src_u = (global uint *)(src + src_offset_B);
      global uint *dest_u = (global uint *)(dest + dest_offset_B);
      uint value = src_u[0];
      for (uint s = 1; s < sample_count; ++s)
         value = choose_max ? max(value, src_u[s]) : min(value, src_u[s]);
      dest_u[0] = value;
   } else {
      global int *src_i = (global int *)(src + src_offset_B);
      global int *dest_i = (global int *)(dest + dest_offset_B);
      int value = src_i[0];
      for (uint s = 1; s < sample_count; ++s)
         value = choose_max ? max(value, src_i[s]) : min(value, src_i[s]);
      dest_i[0] = value;
   }
}

KERNEL(32)
libagx_fill_image(global uint8_t *dest, global uint8_t *value,
                  uint width_el, uint height_el,
                  uint dest_x_el, uint dest_y_el, uint dest_pitch_B,
                  uint dest_stride_el, uint dest_tile_width_el,
                  uint dest_tile_height_el, uint blocksize_B, uint dest_tiled)
{
   uint id = cl_global_id.x;
   if (id >= width_el * height_el)
      return;

   uint x = id % width_el;
   uint y = id / width_el;
   uint dest_offset_B = libagx_copy_image_offset(
      dest_x_el + x, dest_y_el + y, dest_pitch_B, dest_stride_el,
      dest_tile_width_el, dest_tile_height_el, blocksize_B, dest_tiled);

   for (uint b = 0; b < blocksize_B; ++b)
      dest[dest_offset_B + b] = value[b];
}

KERNEL(32)
libagx_fill_uint4(global uint4 *address, uint layer_stride_uint4, uint a,
                  uint b, uint c, uint d)
{
   uint offs = cl_global_id.x + cl_global_id.y * layer_stride_uint4;
   address[offs] = (uint4)(a, b, c, d);
}
