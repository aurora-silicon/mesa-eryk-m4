/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

enum d12_discard_dimension {
   D12_DISCARD_DIMENSION_BUFFER,
   D12_DISCARD_DIMENSION_TEXTURE1D,
   D12_DISCARD_DIMENSION_TEXTURE2D,
   D12_DISCARD_DIMENSION_TEXTURE3D,
};

enum d12_discard_validation_result {
   D12_DISCARD_VALID,
   D12_DISCARD_INVALID_ARGUMENT,
   D12_DISCARD_UNSUPPORTED_COMMAND_LIST,
};

struct d12_discard_resource_info {
   enum d12_discard_dimension dimension;
   uint64_t width;
   uint32_t height;
   uint32_t mip_levels;
   uint32_t array_size;
   uint32_t plane_count;
   bool allow_unordered_access;
};

struct d12_discard_region_info {
   uint32_t rectangle_count;
   bool rectangles_present;
   uint32_t first_subresource;
   uint32_t subresource_count;
};

static inline uint64_t
d12_discard_total_subresources(const struct d12_discard_resource_info *resource)
{
   if (!resource)
      return 0;

   if (resource->dimension == D12_DISCARD_DIMENSION_BUFFER)
      return 1;

   if (!resource->mip_levels || !resource->array_size ||
       !resource->plane_count)
      return 0;

   const uint64_t array_size =
      resource->dimension == D12_DISCARD_DIMENSION_TEXTURE3D
         ? 1
         : resource->array_size;

   return (uint64_t)resource->mip_levels * array_size *
          resource->plane_count;
}

static inline uint64_t
d12_discard_minify(uint64_t extent, uint32_t level)
{
   if (level >= 64)
      return 1;

   extent >>= level;
   return extent ? extent : 1;
}

static inline bool
d12_discard_rect_subresources_match(
   const struct d12_discard_resource_info *resource,
   const struct d12_discard_region_info *region)
{
   if (resource->dimension != D12_DISCARD_DIMENSION_TEXTURE2D ||
       !region->subresource_count)
      return false;

   const uint32_t first_mip =
      region->first_subresource % resource->mip_levels;
   const uint64_t first_width =
      d12_discard_minify(resource->width, first_mip);
   const uint64_t first_height =
      d12_discard_minify(resource->height, first_mip);

   for (uint32_t i = 1; i < region->subresource_count; ++i) {
      const uint32_t mip =
         (region->first_subresource + i) % resource->mip_levels;
      if (d12_discard_minify(resource->width, mip) != first_width ||
          d12_discard_minify(resource->height, mip) != first_height)
         return false;
   }

   return true;
}

/*
 * DiscardResource is available on direct lists and on compute lists for UAV
 * resources. Copy and bundle lists do not support it. A NULL region means the
 * whole resource. When rectangles are present, Direct3D additionally requires
 * 2D subresources of equal dimensions.
 */
static inline enum d12_discard_validation_result
d12_discard_validate(bool direct_list, bool compute_list,
                     const struct d12_discard_resource_info *resource,
                     const struct d12_discard_region_info *region)
{
   if (!resource)
      return D12_DISCARD_INVALID_ARGUMENT;

   if (!direct_list && !compute_list)
      return D12_DISCARD_UNSUPPORTED_COMMAND_LIST;

   if (compute_list && !resource->allow_unordered_access)
      return D12_DISCARD_INVALID_ARGUMENT;

   /* The public D3D12 contract defines pRects == NULL as a whole-resource
    * discard. NumRects and the subresource fields are consequently ignored
    * in that case, even when the caller left nonzero values in them. */
   if (!region || !region->rectangles_present)
      return D12_DISCARD_VALID;

   const uint64_t total = d12_discard_total_subresources(resource);
   if (!total || region->first_subresource > total ||
       region->subresource_count > total - region->first_subresource)
      return D12_DISCARD_INVALID_ARGUMENT;

   if (region->rectangle_count &&
       !d12_discard_rect_subresources_match(resource, region))
      return D12_DISCARD_INVALID_ARGUMENT;

   return D12_DISCARD_VALID;
}
