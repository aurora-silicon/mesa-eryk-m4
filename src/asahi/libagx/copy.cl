/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"
#include "present_blt.h"

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
                        uint sample_count, uint dest_tiled, uint src_tiled,
                        uint resolve_mode)
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
      uint extrema = resolve_mode == 1 ? 255 : 0;
      for (uint s = 0; s < sample_count; ++s) {
         uint src_offset_B = libagx_copy_image_offset(
            src_x_px + x, src_y_px + y, 0, src_stride_el,
            src_tile_width_el, src_tile_height_el,
            4 * sample_count, src_tiled);
         uint value = src[src_offset_B + s * 4 + b];
         sum += value;
         extrema = resolve_mode == 1 ? min(extrema, value)
                                      : max(extrema, value);
      }

      dest[dest_offset_B + b] = resolve_mode
                                   ? extrema
                                   : (sum + sample_count / 2) / sample_count;
   }
}

static float
libagx_srgb_to_linear(float value)
{
   return value <= 0.04045f
             ? value / 12.92f
             : native_powr((value + 0.055f) / 1.055f, 2.4f);
}

static float
libagx_linear_to_srgb(float value)
{
   return value <= 0.0031308f
             ? value * 12.92f
             : 1.055f * native_powr(value, 1.0f / 2.4f) - 0.055f;
}

static uint
libagx_present_load_u16(global uint8_t *address)
{
   return (uint)address[0] | ((uint)address[1] << 8);
}

static uint
libagx_present_load_u32(global uint8_t *address)
{
   return (uint)address[0] | ((uint)address[1] << 8) |
          ((uint)address[2] << 16) | ((uint)address[3] << 24);
}

static void
libagx_present_store_u16(global uint8_t *address, uint value)
{
   address[0] = (uint8_t)value;
   address[1] = (uint8_t)(value >> 8);
}

static void
libagx_present_store_u32(global uint8_t *address, uint value)
{
   address[0] = (uint8_t)value;
   address[1] = (uint8_t)(value >> 8);
   address[2] = (uint8_t)(value >> 16);
   address[3] = (uint8_t)(value >> 24);
}

/* Explicit IEEE-754 binary16 conversion keeps the raw-memory kernel
 * independent of optional OpenCL half-storage extensions. */
static float
libagx_present_half_to_float(uint value)
{
   uint sign = (value & 0x8000u) << 16;
   uint exponent = (value >> 10) & 0x1fu;
   uint mantissa = value & 0x3ffu;
   uint bits;

   if (exponent == 0) {
      if (mantissa == 0) {
         bits = sign;
      } else {
         int unbiased = -14;
         while ((mantissa & 0x400u) == 0) {
            mantissa <<= 1;
            --unbiased;
         }
         mantissa &= 0x3ffu;
         bits = sign | (uint)(unbiased + 127) << 23 | mantissa << 13;
      }
   } else if (exponent == 0x1fu) {
      bits = sign | 0x7f800000u | mantissa << 13;
      if (mantissa)
         bits |= 1u;
   } else {
      bits = sign | (exponent + 112u) << 23 | mantissa << 13;
   }
   return as_float(bits);
}

static uint
libagx_present_float_to_half(float value)
{
   uint bits = as_uint(value);
   uint sign = (bits >> 16) & 0x8000u;
   uint exponent = (bits >> 23) & 0xffu;
   uint mantissa = bits & 0x7fffffu;

   if (exponent == 0xffu) {
      if (!mantissa)
         return sign | 0x7c00u;
      return sign | 0x7c00u | max(1u, mantissa >> 13);
   }
   if (exponent > 142u)
      return sign | 0x7c00u;
   if (exponent < 113u) {
      if (exponent < 103u)
         return sign;
      mantissa |= 0x800000u;
      uint shift = 125u - exponent;
      uint rounded =
         (mantissa + ((1u << (shift - 1u)) - 1u) +
          ((mantissa >> shift) & 1u)) >> shift;
      return sign | rounded;
   }

   uint half_exponent = exponent - 112u;
   mantissa += 0xfffu + ((mantissa >> 13) & 1u);
   if (mantissa & 0x800000u) {
      mantissa = 0;
      ++half_exponent;
      if (half_exponent >= 0x1fu)
         return sign | 0x7c00u;
   }
   return sign | half_exponent << 10 | mantissa >> 13;
}

static float4
libagx_present_load_pixel(global uint8_t *address, uint format)
{
   if (format == LIBAGX_PRESENT_FORMAT_RGBA8 ||
       format == LIBAGX_PRESENT_FORMAT_RGBA8_SRGB) {
      return (float4)((float)address[0], (float)address[1],
                      (float)address[2], (float)address[3]) /
             255.0f;
   }
   if (format == LIBAGX_PRESENT_FORMAT_BGRA8 ||
       format == LIBAGX_PRESENT_FORMAT_BGRX8) {
      return (float4)((float)address[2], (float)address[1],
                      (float)address[0],
                      format == LIBAGX_PRESENT_FORMAT_BGRX8
                         ? 255.0f
                         : (float)address[3]) /
             255.0f;
   }

   uint packed = libagx_present_load_u16(address);
   if (format == LIBAGX_PRESENT_FORMAT_BGR565) {
      return (float4)((float)((packed >> 11) & 0x1fu) / 31.0f,
                      (float)((packed >> 5) & 0x3fu) / 63.0f,
                      (float)(packed & 0x1fu) / 31.0f, 1.0f);
   }
   if (format == LIBAGX_PRESENT_FORMAT_BGRA5551) {
      return (float4)((float)((packed >> 10) & 0x1fu) / 31.0f,
                      (float)((packed >> 5) & 0x1fu) / 31.0f,
                      (float)(packed & 0x1fu) / 31.0f,
                      (float)((packed >> 15) & 1u));
   }
   if (format == LIBAGX_PRESENT_FORMAT_RGB10A2) {
      packed = libagx_present_load_u32(address);
      return (float4)((float)(packed & 0x3ffu) / 1023.0f,
                      (float)((packed >> 10) & 0x3ffu) / 1023.0f,
                      (float)((packed >> 20) & 0x3ffu) / 1023.0f,
                      (float)((packed >> 30) & 3u) / 3.0f);
   }

   return (float4)(
      libagx_present_half_to_float(libagx_present_load_u16(address + 0)),
      libagx_present_half_to_float(libagx_present_load_u16(address + 2)),
      libagx_present_half_to_float(libagx_present_load_u16(address + 4)),
      libagx_present_half_to_float(libagx_present_load_u16(address + 6)));
}

static uint
libagx_present_quantize(float value, uint maximum)
{
   return convert_uint_sat_rte(clamp(value, 0.0f, 1.0f) *
                               (float)maximum);
}

static void
libagx_present_store_pixel(global uint8_t *address, uint format,
                           float4 value)
{
   if (format == LIBAGX_PRESENT_FORMAT_RGBA8 ||
       format == LIBAGX_PRESENT_FORMAT_RGBA8_SRGB) {
      address[0] = (uint8_t)libagx_present_quantize(value.x, 255u);
      address[1] = (uint8_t)libagx_present_quantize(value.y, 255u);
      address[2] = (uint8_t)libagx_present_quantize(value.z, 255u);
      address[3] = (uint8_t)libagx_present_quantize(value.w, 255u);
      return;
   }
   if (format == LIBAGX_PRESENT_FORMAT_BGRA8 ||
       format == LIBAGX_PRESENT_FORMAT_BGRX8) {
      address[0] = (uint8_t)libagx_present_quantize(value.z, 255u);
      address[1] = (uint8_t)libagx_present_quantize(value.y, 255u);
      address[2] = (uint8_t)libagx_present_quantize(value.x, 255u);
      address[3] = format == LIBAGX_PRESENT_FORMAT_BGRX8
                      ? 255
                      : (uint8_t)libagx_present_quantize(value.w, 255u);
      return;
   }
   if (format == LIBAGX_PRESENT_FORMAT_BGR565) {
      uint packed = libagx_present_quantize(value.z, 31u) |
                    libagx_present_quantize(value.y, 63u) << 5 |
                    libagx_present_quantize(value.x, 31u) << 11;
      libagx_present_store_u16(address, packed);
      return;
   }
   if (format == LIBAGX_PRESENT_FORMAT_BGRA5551) {
      uint packed = libagx_present_quantize(value.z, 31u) |
                    libagx_present_quantize(value.y, 31u) << 5 |
                    libagx_present_quantize(value.x, 31u) << 10 |
                    libagx_present_quantize(value.w, 1u) << 15;
      libagx_present_store_u16(address, packed);
      return;
   }
   if (format == LIBAGX_PRESENT_FORMAT_RGB10A2) {
      uint packed = libagx_present_quantize(value.x, 1023u) |
                    libagx_present_quantize(value.y, 1023u) << 10 |
                    libagx_present_quantize(value.z, 1023u) << 20 |
                    libagx_present_quantize(value.w, 3u) << 30;
      libagx_present_store_u32(address, packed);
      return;
   }

   libagx_present_store_u16(address + 0,
                            libagx_present_float_to_half(value.x));
   libagx_present_store_u16(address + 2,
                            libagx_present_float_to_half(value.y));
   libagx_present_store_u16(address + 4,
                            libagx_present_float_to_half(value.z));
   libagx_present_store_u16(address + 6,
                            libagx_present_float_to_half(value.w));
}

static float4
libagx_present_sample(global uint8_t *src, uint x, uint y,
                      uint src_pitch_B, uint src_stride_el,
                      uint src_tile_width_el, uint src_tile_height_el,
                      uint src_pixel_size_B, uint sample_count,
                      uint src_tiled, uint src_format, uint decode_gamma)
{
   uint offset = libagx_copy_image_offset(
      x, y, src_pitch_B, src_stride_el, src_tile_width_el,
      src_tile_height_el, src_pixel_size_B * sample_count, src_tiled);
   float4 sum = (float4)(0.0f);
   for (uint sample = 0; sample < sample_count; ++sample) {
      float4 value = libagx_present_load_pixel(
         src + offset + sample * src_pixel_size_B, src_format);
      if (decode_gamma) {
         value.x = libagx_srgb_to_linear(clamp(value.x, 0.0f, 1.0f));
         value.y = libagx_srgb_to_linear(clamp(value.y, 0.0f, 1.0f));
         value.z = libagx_srgb_to_linear(clamp(value.z, 0.0f, 1.0f));
      }
      sum += value;
   }
   return sum / (float)sample_count;
}

/*
 * DXGI presentation blit. Resolve, bilinear scale, counter-clockwise
 * rotation and display-format conversion happen in one dispatch. This is
 * intentionally raw-memory based so it works for both linear and AIL tiled
 * allocations without building transient texture/sampler descriptors.
 */
KERNEL(32)
libagx_present_blt(global uint8_t *dest, global uint8_t *src,
                   uint dest_width_px, uint dest_height_px,
                   uint dest_x_px, uint dest_y_px,
                   uint src_left_px, uint src_top_px,
                   uint src_right_px, uint src_bottom_px,
                   uint dest_pitch_B, uint src_pitch_B,
                   uint dest_stride_el, uint src_stride_el,
                   uint dest_tile_width_el, uint dest_tile_height_el,
                   uint src_tile_width_el, uint src_tile_height_el,
                   uint dest_pixel_size_B, uint src_pixel_size_B,
                   uint source_sample_count, uint dest_tiled,
                   uint src_tiled, uint dest_format, uint src_format,
                   uint rotation)
{
   uint id = cl_global_id.x;
   if (id >= dest_width_px * dest_height_px)
      return;

   uint x = id % dest_width_px;
   uint y = id / dest_width_px;
   float u = ((float)x + 0.5f) / (float)dest_width_px;
   float v = ((float)y + 0.5f) / (float)dest_height_px;
   float source_u = u;
   float source_v = v;

   /* Rotate source contents counter-clockwise, per BltDXGI. These are the
    * inverse maps from destination space back into the source rectangle. */
   if (rotation == LIBAGX_PRESENT_ROTATION_CCW_90) {
      source_u = 1.0f - v;
      source_v = u;
   } else if (rotation == LIBAGX_PRESENT_ROTATION_CCW_180) {
      source_u = 1.0f - u;
      source_v = 1.0f - v;
   } else if (rotation == LIBAGX_PRESENT_ROTATION_CCW_270) {
      source_u = v;
      source_v = 1.0f - u;
   }

   float source_x = (float)src_left_px +
                    source_u * (float)(src_right_px - src_left_px) - 0.5f;
   float source_y = (float)src_top_px +
                    source_v * (float)(src_bottom_px - src_top_px) - 0.5f;
   source_x = clamp(source_x, (float)src_left_px,
                    (float)(src_right_px - 1u));
   source_y = clamp(source_y, (float)src_top_px,
                    (float)(src_bottom_px - 1u));
   uint x0 = (uint)floor(source_x);
   uint y0 = (uint)floor(source_y);
   uint x1 = min(x0 + 1u, src_right_px - 1u);
   uint y1 = min(y0 + 1u, src_bottom_px - 1u);
   float x_weight = source_x - (float)x0;
   float y_weight = source_y - (float)y0;

   uint src_float = src_format == LIBAGX_PRESENT_FORMAT_RGBA16_FLOAT;
   uint dest_float = dest_format == LIBAGX_PRESENT_FORMAT_RGBA16_FLOAT;
   uint srgb_roundtrip =
      src_format == LIBAGX_PRESENT_FORMAT_RGBA8_SRGB &&
      dest_format == LIBAGX_PRESENT_FORMAT_RGBA8_SRGB;
   uint decode_gamma = (!src_float && dest_float) || srgb_roundtrip;
   uint encode_gamma = (src_float && !dest_float) || srgb_roundtrip;

   float4 c00 = libagx_present_sample(
      src, x0, y0, src_pitch_B, src_stride_el, src_tile_width_el,
      src_tile_height_el, src_pixel_size_B, source_sample_count, src_tiled,
      src_format, decode_gamma);
   float4 c10 = libagx_present_sample(
      src, x1, y0, src_pitch_B, src_stride_el, src_tile_width_el,
      src_tile_height_el, src_pixel_size_B, source_sample_count, src_tiled,
      src_format, decode_gamma);
   float4 c01 = libagx_present_sample(
      src, x0, y1, src_pitch_B, src_stride_el, src_tile_width_el,
      src_tile_height_el, src_pixel_size_B, source_sample_count, src_tiled,
      src_format, decode_gamma);
   float4 c11 = libagx_present_sample(
      src, x1, y1, src_pitch_B, src_stride_el, src_tile_width_el,
      src_tile_height_el, src_pixel_size_B, source_sample_count, src_tiled,
      src_format, decode_gamma);
   float4 top = c00 + (c10 - c00) * x_weight;
   float4 bottom = c01 + (c11 - c01) * x_weight;
   float4 value = top + (bottom - top) * y_weight;

   if (encode_gamma) {
      value.x = libagx_linear_to_srgb(clamp(value.x, 0.0f, 1.0f));
      value.y = libagx_linear_to_srgb(clamp(value.y, 0.0f, 1.0f));
      value.z = libagx_linear_to_srgb(clamp(value.z, 0.0f, 1.0f));
   }

   uint dest_offset = libagx_copy_image_offset(
      dest_x_px + x, dest_y_px + y, dest_pitch_B, dest_stride_el,
      dest_tile_width_el, dest_tile_height_el, dest_pixel_size_B,
      dest_tiled);
   libagx_present_store_pixel(dest + dest_offset, dest_format, value);
}

/* Strongly-typed RGBA8 resolves convert the source storage encoding to
 * linear before averaging and encode according to the resolve format. */
KERNEL(32)
libagx_resolve_unorm8x4_typed(global uint8_t *dest, global uint8_t *src,
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
                              uint src_tiled, uint src_srgb, uint dest_srgb)
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

   for (uint c = 0; c < 4; ++c) {
      float sum = 0.0f;
      for (uint s = 0; s < sample_count; ++s) {
         float value = (float)src[src_offset_B + s * 4 + c] / 255.0f;
         if (src_srgb && c < 3)
            value = libagx_srgb_to_linear(value);
         sum += value;
      }

      float value = sum / (float)sample_count;
      if (dest_srgb && c < 3)
         value = libagx_linear_to_srgb(value);
      dest[dest_offset_B + c] = convert_uchar_sat_rte(value * 255.0f);
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
