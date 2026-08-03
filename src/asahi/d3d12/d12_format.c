/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * DXGI format support.
 *
 * The driver previously hardcoded RGBA8 UNORM in eight places, so any other
 * format was silently impossible. This maps DXGI formats onto the pipe
 * formats the AGX layout and packing code already speaks, and — importantly
 * — answers honestly about what the hardware can actually do rather than
 * accepting a format and producing wrong pixels.
 *
 * Renderability is not assumed: it is read from Mesa's own AGX format table
 * (ail_pixel_format), which is the hardware truth for this GPU.
 */

#include "d12_private.h"

#include "layout.h"
#include "util/format/u_format.h"

struct d12_format_entry {
   DXGI_FORMAT dxgi;
   enum pipe_format pipe;
};

/*
 * Formats we map. Kept deliberately explicit rather than generated: each
 * line is a claim that we handle that format end to end, and a shorter
 * honest table beats a long one full of formats that fail downstream.
 */
static const struct d12_format_entry d12_formats[] = {
   /* 8-bit */
   {DXGI_FORMAT_R8_UNORM, PIPE_FORMAT_R8_UNORM},
   {DXGI_FORMAT_R8_SNORM, PIPE_FORMAT_R8_SNORM},
   {DXGI_FORMAT_R8_UINT, PIPE_FORMAT_R8_UINT},
   {DXGI_FORMAT_R8_SINT, PIPE_FORMAT_R8_SINT},

   {DXGI_FORMAT_R8G8_UNORM, PIPE_FORMAT_R8G8_UNORM},
   {DXGI_FORMAT_R8G8_SNORM, PIPE_FORMAT_R8G8_SNORM},
   {DXGI_FORMAT_R8G8_UINT, PIPE_FORMAT_R8G8_UINT},
   {DXGI_FORMAT_R8G8_SINT, PIPE_FORMAT_R8G8_SINT},

   {DXGI_FORMAT_R8G8B8A8_UNORM, PIPE_FORMAT_R8G8B8A8_UNORM},
   {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, PIPE_FORMAT_R8G8B8A8_SRGB},
   {DXGI_FORMAT_R8G8B8A8_SNORM, PIPE_FORMAT_R8G8B8A8_SNORM},
   {DXGI_FORMAT_R8G8B8A8_UINT, PIPE_FORMAT_R8G8B8A8_UINT},
   {DXGI_FORMAT_R8G8B8A8_SINT, PIPE_FORMAT_R8G8B8A8_SINT},

   {DXGI_FORMAT_B8G8R8A8_UNORM, PIPE_FORMAT_B8G8R8A8_UNORM},
   {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, PIPE_FORMAT_B8G8R8A8_SRGB},
   {DXGI_FORMAT_B8G8R8X8_UNORM, PIPE_FORMAT_B8G8R8X8_UNORM},

   /* 16-bit */
   {DXGI_FORMAT_R16_UNORM, PIPE_FORMAT_R16_UNORM},
   {DXGI_FORMAT_R16_SNORM, PIPE_FORMAT_R16_SNORM},
   {DXGI_FORMAT_R16_UINT, PIPE_FORMAT_R16_UINT},
   {DXGI_FORMAT_R16_SINT, PIPE_FORMAT_R16_SINT},
   {DXGI_FORMAT_R16_FLOAT, PIPE_FORMAT_R16_FLOAT},

   {DXGI_FORMAT_R16G16_UNORM, PIPE_FORMAT_R16G16_UNORM},
   {DXGI_FORMAT_R16G16_SNORM, PIPE_FORMAT_R16G16_SNORM},
   {DXGI_FORMAT_R16G16_UINT, PIPE_FORMAT_R16G16_UINT},
   {DXGI_FORMAT_R16G16_SINT, PIPE_FORMAT_R16G16_SINT},
   {DXGI_FORMAT_R16G16_FLOAT, PIPE_FORMAT_R16G16_FLOAT},

   {DXGI_FORMAT_R16G16B16A16_UNORM, PIPE_FORMAT_R16G16B16A16_UNORM},
   {DXGI_FORMAT_R16G16B16A16_SNORM, PIPE_FORMAT_R16G16B16A16_SNORM},
   {DXGI_FORMAT_R16G16B16A16_UINT, PIPE_FORMAT_R16G16B16A16_UINT},
   {DXGI_FORMAT_R16G16B16A16_SINT, PIPE_FORMAT_R16G16B16A16_SINT},
   {DXGI_FORMAT_R16G16B16A16_FLOAT, PIPE_FORMAT_R16G16B16A16_FLOAT},

   /* 32-bit */
   {DXGI_FORMAT_R32_UINT, PIPE_FORMAT_R32_UINT},
   {DXGI_FORMAT_R32_SINT, PIPE_FORMAT_R32_SINT},
   {DXGI_FORMAT_R32_FLOAT, PIPE_FORMAT_R32_FLOAT},

   {DXGI_FORMAT_R32G32_UINT, PIPE_FORMAT_R32G32_UINT},
   {DXGI_FORMAT_R32G32_SINT, PIPE_FORMAT_R32G32_SINT},
   {DXGI_FORMAT_R32G32_FLOAT, PIPE_FORMAT_R32G32_FLOAT},

   {DXGI_FORMAT_R32G32B32A32_UINT, PIPE_FORMAT_R32G32B32A32_UINT},
   {DXGI_FORMAT_R32G32B32A32_SINT, PIPE_FORMAT_R32G32B32A32_SINT},
   {DXGI_FORMAT_R32G32B32A32_FLOAT, PIPE_FORMAT_R32G32B32A32_FLOAT},

   /* Packed */
   {DXGI_FORMAT_R10G10B10A2_UNORM, PIPE_FORMAT_R10G10B10A2_UNORM},
   {DXGI_FORMAT_R10G10B10A2_UINT, PIPE_FORMAT_R10G10B10A2_UINT},
   {DXGI_FORMAT_R11G11B10_FLOAT, PIPE_FORMAT_R11G11B10_FLOAT},
};

enum pipe_format
d12_format_to_pipe(DXGI_FORMAT format)
{
   for (unsigned i = 0; i < ARRAY_SIZE(d12_formats); i++) {
      if (d12_formats[i].dxgi == format)
         return d12_formats[i].pipe;
   }
   return PIPE_FORMAT_NONE;
}

bool
d12_format_is_renderable(DXGI_FORMAT format)
{
   enum pipe_format pipe = d12_format_to_pipe(format);
   if (pipe == PIPE_FORMAT_NONE)
      return false;

   /* Hardware truth, not our opinion: the AGX table records which formats
    * can be rendered to. A sample-only format has renderable == NONE.
    */
   return ail_is_valid_pixel_format(pipe) &&
          ail_pixel_format[pipe].renderable != PIPE_FORMAT_NONE;
}

unsigned
d12_format_bytes_per_pixel(DXGI_FORMAT format)
{
   enum pipe_format pipe = d12_format_to_pipe(format);
   if (pipe == PIPE_FORMAT_NONE)
      return 0;

   return util_format_get_blocksize(pipe);
}
