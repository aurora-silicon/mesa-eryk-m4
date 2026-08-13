/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_resource_private.h"

#include <string.h>

/* D3DDDIFORMAT values used by Aurora's D3D11 standard allocations.  Keep
 * these wire values local to the shared blob translator so its host tests do
 * not need to include the Windows-only D3D headers. */
#define D12_UMD_D3DDDIFMT_A8R8G8B8 21u
#define D12_UMD_D3DDDIFMT_X8R8G8B8 22u
#define D12_UMD_D3DDDIFMT_A8B8G8R8 32u

/* DXGI_FORMAT values corresponding to the formats above. */
#define D12_UMD_DXGI_FORMAT_R8G8B8A8_UNORM 28u
#define D12_UMD_DXGI_FORMAT_R8G8B8A8_UNORM_SRGB 29u
#define D12_UMD_DXGI_FORMAT_B8G8R8A8_UNORM 87u
#define D12_UMD_DXGI_FORMAT_B8G8R8X8_UNORM 88u
#define D12_UMD_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB 91u
#define D12_UMD_DXGI_FORMAT_B8G8R8X8_UNORM_SRGB 93u

_Static_assert(offsetof(struct d12_umd_resource_private, kmd) == 0,
               "KMD resource-private prefix must remain first");
_Static_assert(sizeof(struct agxw_resource_private) == 16,
               "pinned AppleAgxWddm resource-private width");
_Static_assert(sizeof(struct d12_umd_resource_private) == 176,
               "D3D12 opened-resource private-data width");

static bool
d12_umd_resource_private_heap_valid(
   const struct d12_frontend_heap_desc *heap)
{
   const uint32_t supported =
      D12_FRONTEND_HEAP_ALLOW_NON_RT_DS_TEXTURES |
      D12_FRONTEND_HEAP_ALLOW_BUFFERS |
      D12_FRONTEND_HEAP_COHERENT_SYSTEMWIDE |
      D12_FRONTEND_HEAP_PRIMARY |
      D12_FRONTEND_HEAP_ALLOW_RT_DS_TEXTURES;
   const uint32_t category =
      D12_FRONTEND_HEAP_ALLOW_NON_RT_DS_TEXTURES |
      D12_FRONTEND_HEAP_ALLOW_BUFFERS |
      D12_FRONTEND_HEAP_ALLOW_RT_DS_TEXTURES;
   const uint64_t alignment = heap ? heap->alignment : 0;

   return heap && heap->size && (heap->flags & category) &&
          !(heap->flags & ~supported) &&
          (!alignment ||
           (alignment >= AGXW_PAGE_SIZE &&
            !(alignment & (alignment - UINT64_C(1))))) &&
          heap->memory_pool <= D12_FRONTEND_MEMORY_POOL_L1 &&
          heap->cpu_page_property <= D12_FRONTEND_CPU_PAGE_WRITE_BACK &&
          heap->creation_node_mask <= 1 && heap->visible_node_mask <= 1;
}

static bool
d12_umd_resource_private_resource_valid(
   const struct d12_frontend_resource_desc *resource)
{
   const uint32_t supported =
      D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
      D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL |
      D12_FRONTEND_RESOURCE_ALLOW_CROSS_ADAPTER |
      D12_FRONTEND_RESOURCE_ALLOW_SIMULTANEOUS_ACCESS |
      D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE |
      D12_FRONTEND_RESOURCE_ALLOW_UNORDERED_ACCESS;
   const uint32_t rt_ds = D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
                          D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL;

   if (!resource || resource->dimension < 1 || resource->dimension > 4 ||
       !resource->width || !resource->height ||
       !resource->depth_or_array_size || !resource->mip_levels ||
       (resource->sample_count != 1 && resource->sample_count != 2 &&
        resource->sample_count != 4) ||
       (resource->sample_count == 1 && resource->sample_quality) ||
       resource->layout > 3 || (resource->flags & ~supported) ||
       (resource->flags & rt_ds) == rt_ds ||
       (resource->alignment &&
        (resource->alignment < AGXW_PAGE_SIZE ||
         (resource->alignment & (resource->alignment - UINT64_C(1))))) ||
       (resource->layout == 1 && resource->sample_count != 1))
      return false;

   if (resource->dimension == 1)
      return resource->height == 1 && resource->depth_or_array_size == 1 &&
             resource->mip_levels == 1 && resource->format == 0 &&
             resource->sample_count == 1 && resource->layout == 1;

   if (!resource->format)
      return false;
   if (resource->dimension == 2 &&
       (resource->height != 1 || resource->sample_count != 1))
      return false;
   if (resource->dimension == 4 && resource->sample_count != 1)
      return false;
   return true;
}

bool
d12_umd_resource_private_pack(
   const struct d12_frontend_heap_desc *heap,
   const struct d12_frontend_resource_desc *resource,
   const struct d12_frontend_clear_value *clear_value, uint32_t row_pitch,
   uint32_t slice_pitch, struct d12_umd_resource_private *data)
{
   if (!data || !d12_umd_resource_private_heap_valid(heap) ||
       (resource && !d12_umd_resource_private_resource_valid(resource)) ||
       (clear_value &&
        (!resource ||
         !(resource->flags &
           (D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
            D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL)))) ||
       ((row_pitch || slice_pitch) &&
        (!resource || resource->layout != 1 || !row_pitch)))
      return false;

   memset(data, 0, sizeof(*data));
   data->kmd = (struct agxw_resource_private){
      .version = AGXW_RESOURCE_PRIVATE_VERSION,
      .struct_size = sizeof(struct agxw_resource_private),
      .allocation_count = AGXW_MAX_ALLOCATIONS_PER_RESOURCE,
   };
   data->magic = D12_UMD_RESOURCE_PRIVATE_MAGIC;
   data->version = D12_UMD_RESOURCE_PRIVATE_VERSION;
   data->struct_size = sizeof(*data);
   data->heap = *heap;
   if (resource) {
      data->flags |= D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE;
      data->resource = *resource;
   }
   if (clear_value) {
      data->flags |= D12_UMD_RESOURCE_PRIVATE_HAS_CLEAR_VALUE;
      data->clear_value = *clear_value;
   }
   if (row_pitch || slice_pitch) {
      data->flags |= D12_UMD_RESOURCE_PRIVATE_HAS_ROW_MAJOR_LAYOUT;
      data->row_pitch = row_pitch;
      data->slice_pitch = slice_pitch;
   }
   return true;
}

bool
d12_umd_resource_private_parse(const void *blob, uint32_t blob_size,
                               struct d12_umd_resource_private *data)
{
   struct d12_umd_resource_private parsed;
   const uint32_t supported =
      D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE |
      D12_UMD_RESOURCE_PRIVATE_HAS_CLEAR_VALUE |
      D12_UMD_RESOURCE_PRIVATE_HAS_ROW_MAJOR_LAYOUT;

   if (!blob || !data || blob_size != sizeof(parsed))
      return false;
   memcpy(&parsed, blob, sizeof(parsed));
   if (parsed.kmd.version != AGXW_RESOURCE_PRIVATE_VERSION ||
       parsed.kmd.struct_size != sizeof(struct agxw_resource_private) ||
       parsed.kmd.allocation_count != AGXW_MAX_ALLOCATIONS_PER_RESOURCE ||
       parsed.kmd.flags || parsed.magic != D12_UMD_RESOURCE_PRIVATE_MAGIC ||
       parsed.version != D12_UMD_RESOURCE_PRIVATE_VERSION ||
       parsed.struct_size != sizeof(parsed) || (parsed.flags & ~supported) ||
       parsed.reserved[0] || parsed.reserved[1] ||
       !d12_umd_resource_private_heap_valid(&parsed.heap))
      return false;
   if (!!(parsed.flags & D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE) !=
       d12_umd_resource_private_resource_valid(&parsed.resource))
      return false;
   if ((parsed.flags & (D12_UMD_RESOURCE_PRIVATE_HAS_CLEAR_VALUE |
                        D12_UMD_RESOURCE_PRIVATE_HAS_ROW_MAJOR_LAYOUT)) &&
       !(parsed.flags & D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE))
      return false;
   if ((parsed.flags & D12_UMD_RESOURCE_PRIVATE_HAS_CLEAR_VALUE) &&
       !(parsed.resource.flags &
         (D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
          D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL)))
      return false;
   if ((parsed.flags & D12_UMD_RESOURCE_PRIVATE_HAS_ROW_MAJOR_LAYOUT) &&
       (parsed.resource.layout != 1 || !parsed.row_pitch))
      return false;
   if (!(parsed.flags & D12_UMD_RESOURCE_PRIVATE_HAS_ROW_MAJOR_LAYOUT) &&
       (parsed.row_pitch || parsed.slice_pitch))
      return false;
   *data = parsed;
   return true;
}

static bool
d12_umd_resource_private_dxgi_to_d3d(uint32_t dxgi_format,
                                     uint32_t *d3d_format)
{
   if (!d3d_format)
      return false;
   switch (dxgi_format) {
   case D12_UMD_DXGI_FORMAT_B8G8R8A8_UNORM:
   case D12_UMD_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      *d3d_format = D12_UMD_D3DDDIFMT_A8R8G8B8;
      return true;
   case D12_UMD_DXGI_FORMAT_B8G8R8X8_UNORM:
   case D12_UMD_DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      *d3d_format = D12_UMD_D3DDDIFMT_X8R8G8B8;
      return true;
   case D12_UMD_DXGI_FORMAT_R8G8B8A8_UNORM:
   case D12_UMD_DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      *d3d_format = D12_UMD_D3DDDIFMT_A8B8G8R8;
      return true;
   default:
      return false;
   }
}

static bool
d12_umd_resource_private_d3d_to_dxgi(uint32_t d3d_format,
                                     uint32_t *dxgi_format)
{
   if (!dxgi_format)
      return false;
   switch (d3d_format) {
   case D12_UMD_D3DDDIFMT_A8R8G8B8:
      *dxgi_format = D12_UMD_DXGI_FORMAT_B8G8R8A8_UNORM;
      return true;
   case D12_UMD_D3DDDIFMT_X8R8G8B8:
      *dxgi_format = D12_UMD_DXGI_FORMAT_B8G8R8X8_UNORM;
      return true;
   case D12_UMD_D3DDDIFMT_A8B8G8R8:
      *dxgi_format = D12_UMD_DXGI_FORMAT_R8G8B8A8_UNORM;
      return true;
   default:
      return false;
   }
}

bool
d12_umd_resource_private_standard_desc_pack(
   const struct d12_frontend_resource_desc *resource,
   uint64_t *out_descriptor)
{
   uint32_t d3d_format;

   if (!resource || !out_descriptor || resource->dimension != 3 ||
       !resource->width || resource->width > UINT32_MAX ||
       !resource->height || resource->depth_or_array_size != 1 ||
       resource->mip_levels != 1 || resource->sample_count != 1 ||
       resource->sample_quality != 0 ||
       (resource->flags & D12_FRONTEND_RESOURCE_ALLOW_DEPTH_STENCIL) ||
       !d12_umd_resource_private_dxgi_to_d3d(resource->format, &d3d_format))
      return false;

   return agxw_standard_desc_pack((uint32_t)resource->width,
                                  resource->height, d3d_format,
                                  out_descriptor) == 0;
}

bool
d12_umd_resource_private_parse_foreign(
   const void *blob, uint32_t blob_size,
   const struct agxw_alloc_private *allocation,
   struct d12_umd_resource_private *data)
{
   struct agxw_resource_private resource_private;
   uint32_t width, height, d3d_format, dxgi_format;
   uint64_t minimum_size;

   if (!blob || !allocation || !data ||
       blob_size != sizeof(resource_private) ||
       agxw_alloc_private_validate(allocation, sizeof(*allocation)) != 0 ||
       !(allocation->flags & AGXW_ALLOC_SHAREABLE) ||
       (allocation->flags & (AGXW_ALLOC_VM_PRIVATE | AGXW_ALLOC_NO_CPU_MAP)) ||
       (allocation->align_log2 &&
        allocation->align_log2 > AGXW_WDDM_LARGE_PAGE_BITS) ||
       !allocation->reserved ||
       agxw_standard_desc_unpack(allocation->reserved, &width, &height,
                                 &d3d_format) != 0 ||
       !d12_umd_resource_private_d3d_to_dxgi(d3d_format, &dxgi_format))
      return false;

   memcpy(&resource_private, blob, sizeof(resource_private));
   if (agxw_resource_private_validate(&resource_private, blob_size) != 0 ||
       resource_private.allocation_count !=
          AGXW_MAX_ALLOCATIONS_PER_RESOURCE)
      return false;

   /* All formats represented by the cross-UMD standard descriptor are four
    * bytes per texel.  Prove that the KMD allocation can contain the visible
    * image before constructing renderer objects around foreign handles. */
   minimum_size = (uint64_t)width * (uint64_t)height;
   if (minimum_size > UINT64_MAX / UINT64_C(4) ||
       minimum_size * UINT64_C(4) > allocation->size)
      return false;

   memset(data, 0, sizeof(*data));
   data->kmd = resource_private;
   data->magic = D12_UMD_RESOURCE_PRIVATE_MAGIC;
   data->version = D12_UMD_RESOURCE_PRIVATE_VERSION;
   data->struct_size = sizeof(*data);
   data->flags = D12_UMD_RESOURCE_PRIVATE_HAS_RESOURCE;
   data->heap = (struct d12_frontend_heap_desc){
      .size = allocation->size,
      .alignment = allocation->align_log2
                      ? UINT64_C(1) << allocation->align_log2
                      : AGXW_PAGE_SIZE,
      .memory_pool = D12_FRONTEND_MEMORY_POOL_L0,
      .cpu_page_property =
         allocation->flags & AGXW_ALLOC_WRITEBACK
            ? D12_FRONTEND_CPU_PAGE_WRITE_BACK
            : D12_FRONTEND_CPU_PAGE_WRITE_COMBINE,
      .flags = D12_FRONTEND_HEAP_ALLOW_NON_RT_DS_TEXTURES |
               D12_FRONTEND_HEAP_ALLOW_RT_DS_TEXTURES |
               (allocation->flags & AGXW_ALLOC_PRIMARY
                   ? D12_FRONTEND_HEAP_PRIMARY
                   : 0),
      .creation_node_mask = 1,
      .visible_node_mask = 1,
   };
   data->resource = (struct d12_frontend_resource_desc){
      .dimension = 3,
      .width = width,
      .height = height,
      .depth_or_array_size = 1,
      .mip_levels = 1,
      .format = dxgi_format,
      .sample_count = 1,
      .layout = 0,
      .flags = D12_FRONTEND_RESOURCE_ALLOW_RENDER_TARGET |
               D12_FRONTEND_RESOURCE_ALLOW_SHADER_RESOURCE,
   };
   return d12_umd_resource_private_heap_valid(&data->heap) &&
          d12_umd_resource_private_resource_valid(&data->resource);
}
