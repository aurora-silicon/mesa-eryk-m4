/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_resource_private.h"

#include <string.h>

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
