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

KERNEL(32)
libagx_fill_uint4(global uint4 *address, uint layer_stride_uint4, uint a,
                  uint b, uint c, uint d)
{
   uint offs = cl_global_id.x + cl_global_id.y * layer_stride_uint4;
   address[offs] = (uint4)(a, b, c, d);
}
