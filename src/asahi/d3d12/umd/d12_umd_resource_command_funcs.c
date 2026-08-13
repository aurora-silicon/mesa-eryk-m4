/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Resource and output-merger command translation for the DDI 0003 table.
 * Runtime handles are validated once, then the shared renderer records the
 * same native command-list operations used by the public COM frontend.
 */

#include "d12_umd_resource_command_funcs.h"

#include "d12_umd_internal.h"
#include "../d12_present_blt_math.h"

#include <stdbool.h>
#include <stdint.h>

#define D12_UMD_CLEAR_DEPTH 0x1u
#define D12_UMD_CLEAR_STENCIL 0x2u
#define D12_UMD_BLT_RESOLVE 0x1u
#define D12_UMD_BLT_CONVERT 0x2u
#define D12_UMD_BLT_STRETCH 0x4u
#define D12_UMD_BLT_PRESENT 0x8u

_Static_assert((UINT)DXGI_DDI_MODE_ROTATION_UNSPECIFIED ==
                  D12_PRESENT_ROTATION_UNSPECIFIED &&
                  (UINT)DXGI_DDI_MODE_ROTATION_IDENTITY ==
                  D12_PRESENT_ROTATION_IDENTITY &&
                  (UINT)DXGI_DDI_MODE_ROTATION_ROTATE90 ==
                  D12_PRESENT_ROTATION_CCW_90 &&
                  (UINT)DXGI_DDI_MODE_ROTATION_ROTATE180 ==
                  D12_PRESENT_ROTATION_CCW_180 &&
                  (UINT)DXGI_DDI_MODE_ROTATION_ROTATE270 ==
                  D12_PRESENT_ROTATION_CCW_270,
               "DXGI and frontend rotation encodings diverged");

static bool
d12_umd_blt_display_format(DXGI_FORMAT format)
{
   switch (format) {
   case DXGI_FORMAT_B5G6R5_UNORM:
   case DXGI_FORMAT_B5G5R5A1_UNORM:
   case DXGI_FORMAT_B8G8R8A8_UNORM:
   case DXGI_FORMAT_B8G8R8X8_UNORM:
   case DXGI_FORMAT_R16G16B16A16_FLOAT:
   case DXGI_FORMAT_R10G10B10A2_UNORM:
   case DXGI_FORMAT_R8G8B8A8_UNORM:
   case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      return true;
   default:
      return false;
   }
}

static void
d12_umd_resource_command_error(struct d12_umd_command_list *list,
                               HRESULT result)
{
   d12_umd_report_command_list_error(list, result);
}

static struct d12_umd_resource *
d12_umd_command_resource(struct d12_umd_device *device,
                         D3D12DDI_HRESOURCE handle, bool allow_null)
{
   struct d12_umd_resource *resource = handle.pDrvPrivate;

   if (!resource)
      return allow_null ? NULL : (struct d12_umd_resource *)(uintptr_t)1;
   return resource->signature == D12_UMD_RESOURCE_SIGNATURE &&
                 resource->device == device && resource->backend
             ? resource
             : (struct d12_umd_resource *)(uintptr_t)1;
}

static bool
d12_umd_command_list_ready(struct d12_umd_command_list *list)
{
   return list && list->device && list->backend && list->device->renderer_ops;
}

static void APIENTRY
d12_umd_set_vertex_buffers(D3D12DDI_HCOMMANDLIST list_handle, UINT start_slot,
                           UINT count,
                           const D3D12DDI_VERTEX_BUFFER_VIEW *views)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_frontend_vertex_buffer_view
      translated[D12_FRONTEND_MAX_VERTEX_BUFFERS];

   if (!d12_umd_command_list_ready(list) ||
       !list->device->renderer_ops->set_vertex_buffers ||
       start_slot > D12_FRONTEND_MAX_VERTEX_BUFFERS ||
       count > D12_FRONTEND_MAX_VERTEX_BUFFERS - start_slot ||
       (count && !views)) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   for (UINT index = 0; index < count; ++index) {
      translated[index] = (struct d12_frontend_vertex_buffer_view){
         .address = views[index].BufferLocation,
         .size = views[index].SizeInBytes,
         .stride = views[index].StrideInBytes,
      };
   }
   list->device->renderer_ops->set_vertex_buffers(
      list->backend, start_slot, count, translated);
}

static void APIENTRY
d12_umd_set_index_buffer(D3D12DDI_HCOMMANDLIST list_handle,
                         const D3D12DDI_INDEX_BUFFER_VIEW *view)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_frontend_index_buffer_view translated;

   if (!d12_umd_command_list_ready(list) ||
       !list->device->renderer_ops->set_index_buffer) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   if (!view) {
      list->device->renderer_ops->set_index_buffer(list->backend, NULL);
      return;
   }
   if (view->Format != DXGI_FORMAT_UNKNOWN &&
       view->Format != DXGI_FORMAT_R16_UINT &&
       view->Format != DXGI_FORMAT_R32_UINT) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   translated = (struct d12_frontend_index_buffer_view){
      .address = view->BufferLocation,
      .size = view->SizeInBytes,
      .format = (uint32_t)view->Format,
   };
   list->device->renderer_ops->set_index_buffer(list->backend, &translated);
}

static void APIENTRY
d12_umd_set_stream_output_targets(
   D3D12DDI_HCOMMANDLIST list_handle, UINT start_slot, UINT count,
   const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW *views)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_frontend_stream_output_buffer_view
      translated[D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS] = {0};

   if (!d12_umd_command_list_ready(list) ||
       !list->device->renderer_ops->set_stream_output_targets ||
       start_slot > D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS ||
       count > D12_FRONTEND_MAX_STREAM_OUTPUT_BUFFERS - start_slot) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   for (UINT index = 0; views && index < count; ++index) {
      const bool empty = !views[index].BufferLocation &&
                         !views[index].SizeInBytes &&
                         !views[index].BufferFilledSizeLocation;
      if (!empty &&
          (!views[index].BufferLocation || !views[index].SizeInBytes ||
           !views[index].BufferFilledSizeLocation ||
           views[index].SizeInBytes > UINT32_MAX)) {
         d12_umd_resource_command_error(list, E_INVALIDARG);
         return;
      }
      translated[index] = (struct d12_frontend_stream_output_buffer_view){
         .address = views[index].BufferLocation,
         .size = views[index].SizeInBytes,
         .filled_size_address = views[index].BufferFilledSizeLocation,
      };
   }
   list->device->renderer_ops->set_stream_output_targets(
      list->backend, start_slot, count, translated);
}

static void APIENTRY
d12_umd_discard_resource(
   D3D12DDI_HCOMMANDLIST list_handle, D3D12DDI_HRESOURCE resource_handle,
   const D3D12DDIARG_DISCARD_RESOURCE_0003 *arguments)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_resource *resource;

   if (!d12_umd_command_list_ready(list) ||
       (arguments && arguments->NumRects && !arguments->pRects)) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   resource = d12_umd_command_resource(list->device, resource_handle, false);
   if (resource == (void *)(uintptr_t)1) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }

   /* Discard is a lifetime/performance hint. AGX has no transition needed to
    * make old contents inaccessible, so retaining the allocation is the
    * correct conservative implementation until tile-pass load elimination is
    * taught to consume discard regions. */
}

static void APIENTRY
d12_umd_set_render_targets(
   D3D12DDI_HCOMMANDLIST list_handle, UINT count,
   const D3D12DDI_CPU_DESCRIPTOR_HANDLE *render_targets, BOOL contiguous,
   const D3D12DDI_CPU_DESCRIPTOR_HANDLE *depth_stencil)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   uint64_t translated[D12_FRONTEND_MAX_RENDER_TARGETS];
   uint64_t translated_depth;

   if (!d12_umd_command_list_ready(list) ||
       !list->device->renderer_ops->set_render_targets ||
       count > D12_FRONTEND_MAX_RENDER_TARGETS ||
       (count && !render_targets)) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   for (UINT index = 0; index < count; ++index) {
      if (!render_targets[index].ptr) {
         d12_umd_resource_command_error(list, E_INVALIDARG);
         return;
      }
      translated[index] = render_targets[index].ptr;
   }
   if (depth_stencil) {
      if (!depth_stencil->ptr) {
         d12_umd_resource_command_error(list, E_INVALIDARG);
         return;
      }
      translated_depth = depth_stencil->ptr;
   }
   list->device->renderer_ops->set_render_targets(
      list->backend, count, translated, contiguous != FALSE,
      depth_stencil ? &translated_depth : NULL);
}

static void APIENTRY
d12_umd_copy_buffer_region(D3D12DDI_HCOMMANDLIST list_handle,
                           D3D12DDIARG_BUFFER_PLACEMENT destination,
                           D3D12DDIARG_BUFFER_PLACEMENT source, UINT64 bytes)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_resource *destination_resource;
   struct d12_umd_resource *source_resource;

   if (!d12_umd_command_list_ready(list) || !bytes ||
       !list->device->renderer_ops->copy_buffer_region) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   destination_resource = d12_umd_command_resource(
      list->device, destination.BaseAddress.UMD.hResource, false);
   source_resource = d12_umd_command_resource(
      list->device, source.BaseAddress.UMD.hResource, false);
   if (destination_resource == (void *)(uintptr_t)1 ||
       source_resource == (void *)(uintptr_t)1) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->copy_buffer_region(
      list->backend, destination_resource->backend,
      destination.BaseAddress.UMD.Offset, source_resource->backend,
      source.BaseAddress.UMD.Offset, bytes);
}

static void APIENTRY
d12_umd_copy_resource(D3D12DDI_HCOMMANDLIST list_handle,
                      D3D12DDI_HRESOURCE destination_handle,
                      D3D12DDI_HRESOURCE source_handle)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_resource *destination;
   struct d12_umd_resource *source;

   if (!d12_umd_command_list_ready(list) ||
       !list->device->renderer_ops->copy_resource) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   destination = d12_umd_command_resource(list->device, destination_handle,
                                           false);
   source = d12_umd_command_resource(list->device, source_handle, false);
   if (destination == (void *)(uintptr_t)1 ||
       source == (void *)(uintptr_t)1) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->copy_resource(
      list->backend, destination->backend, source->backend);
}

static void APIENTRY
d12_umd_copy_tiles(
   D3D12DDI_HCOMMANDLIST list_handle, D3D12DDI_HRESOURCE resource_handle,
   const D3D12DDI_TILED_RESOURCE_COORDINATE *region_start,
   const D3D12DDI_TILE_REGION_SIZE *region_size,
   D3D12DDI_HRESOURCE buffer_handle, UINT64 buffer_offset,
   D3D12DDI_TILE_COPY_FLAGS flags)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_resource *resource;
   struct d12_umd_resource *buffer;
   const UINT allowed = D3D12DDI_TILE_COPY_FLAG_NO_HAZARD |
                        D3D12DDI_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE |
                        D3D12DDI_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER;

   if (!d12_umd_command_list_ready(list) || !region_start || !region_size ||
       ((UINT)flags & ~allowed) ||
       ((flags &
         D3D12DDI_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE) &&
        (flags &
         D3D12DDI_TILE_COPY_FLAG_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER)) ||
       !list->device->renderer_ops->copy_tiles) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   resource = d12_umd_command_resource(list->device, resource_handle, false);
   buffer = d12_umd_command_resource(list->device, buffer_handle, false);
   if (resource == (void *)(uintptr_t)1 ||
       buffer == (void *)(uintptr_t)1 || !resource->reserved ||
       buffer->reserved || buffer->desc.ResourceType != D3D12DDI_RT_BUFFER) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }

   const struct d12_frontend_tiled_resource_coordinate start = {
      region_start->X, region_start->Y, region_start->Z,
      region_start->Subresource,
   };
   const struct d12_frontend_tile_region_size size = {
      region_size->NumTiles, region_size->UseBox ? 1u : 0u,
      region_size->Width,
      region_size->Height, region_size->Depth,
   };
   HRESULT result = list->device->renderer_ops->copy_tiles(
      list->backend, resource->backend, &start, &size, buffer->backend,
      buffer_offset, (uint32_t)flags);
   if (FAILED(result))
      d12_umd_resource_command_error(list, result);
}

static bool
d12_umd_texture_copy_location(
   struct d12_umd_device *device,
   const D3D12DDIARG_BUFFER_PLACEMENT *placement,
   D3D12DDIARG_PLACED_RESOURCE placed,
   struct d12_frontend_texture_copy_location *translated)
{
   struct d12_umd_resource *resource;

   if (!placement || !translated)
      return false;
   resource = d12_umd_command_resource(
      device, placement->BaseAddress.UMD.hResource, false);
   if (resource == (void *)(uintptr_t)1)
      return false;
   translated->resource = resource->backend;
   switch (placed.Layout) {
   case D3D12DDI_RL_SELECT_SUBRESOURCE:
      if (placement->BaseAddress.UMD.Offset > UINT32_MAX)
         return false;
      translated->type = D12_FRONTEND_TEXTURE_COPY_SUBRESOURCE;
      translated->subresource = (uint32_t)placement->BaseAddress.UMD.Offset;
      return true;
   case D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED: {
      const D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT *layout =
         placed.pLayout;
      if (!layout)
         return false;
      translated->type = D12_FRONTEND_TEXTURE_COPY_FOOTPRINT;
      translated->footprint.offset = placement->BaseAddress.UMD.Offset;
      translated->footprint.format = (uint32_t)layout->Format;
      translated->footprint.width = layout->PhysicalWidth;
      translated->footprint.height = layout->PhysicalHeight;
      translated->footprint.depth = layout->PhysicalDepth;
      translated->footprint.row_pitch = layout->Pitch;
      return true;
   }
   case D3D12DDI_RL_PLACED_VIRTUAL_SUBRESOURCE_PITCHED: {
      const D3D12DDIARG_VIRTUAL_SUBRESOURCE_PITCHED_LAYOUT *layout =
         placed.pLayout;
      if (!layout)
         return false;
      translated->type = D12_FRONTEND_TEXTURE_COPY_FOOTPRINT;
      translated->footprint.offset = placement->BaseAddress.UMD.Offset;
      translated->footprint.format = (uint32_t)layout->Format;
      translated->footprint.width = layout->VirtualWidth;
      translated->footprint.height = layout->VirtualHeight;
      translated->footprint.depth = layout->VirtualDepth;
      translated->footprint.row_pitch = layout->Pitch;
      return true;
   }
   default:
      return false;
   }
}

static void APIENTRY
d12_umd_copy_texture_region(
   D3D12DDI_HCOMMANDLIST list_handle,
   const D3D12DDIARG_BUFFER_PLACEMENT *destination_placement,
   D3D12DDIARG_PLACED_RESOURCE destination, UINT destination_x,
   UINT destination_y, UINT destination_z,
   const D3D12DDIARG_BUFFER_PLACEMENT *source_placement,
   D3D12DDIARG_PLACED_RESOURCE source, const D3D12DDI_BOX *source_box)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_frontend_texture_copy_location translated_destination = {0};
   struct d12_frontend_texture_copy_location translated_source = {0};
   struct d12_frontend_box translated_box;

   if (!d12_umd_command_list_ready(list) ||
       !list->device->renderer_ops->copy_texture_region ||
       !d12_umd_texture_copy_location(list->device, destination_placement,
                                      destination, &translated_destination) ||
       !d12_umd_texture_copy_location(list->device, source_placement, source,
                                      &translated_source) ||
       (source_box &&
        (source_box->Left < 0 || source_box->Top < 0 ||
         source_box->Front < 0 || source_box->Right < source_box->Left ||
         source_box->Bottom < source_box->Top ||
         source_box->Back < source_box->Front))) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   if (source_box) {
      translated_box = (struct d12_frontend_box){
         .left = source_box->Left,
         .top = source_box->Top,
         .front = source_box->Front,
         .right = source_box->Right,
         .bottom = source_box->Bottom,
         .back = source_box->Back,
      };
   }
   list->device->renderer_ops->copy_texture_region(
      list->backend, &translated_destination, destination_x, destination_y,
      destination_z, &translated_source,
      source_box ? &translated_box : NULL);
}

static void APIENTRY
d12_umd_resolve_subresource(D3D12DDI_HCOMMANDLIST list_handle,
                            D3D12DDI_HRESOURCE destination_handle,
                            UINT destination_subresource,
                            D3D12DDI_HRESOURCE source_handle,
                            UINT source_subresource, DXGI_FORMAT format)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_resource *destination;
   struct d12_umd_resource *source;

   if (!d12_umd_command_list_ready(list) ||
       !list->device->renderer_ops->resolve_subresource) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   destination = d12_umd_command_resource(list->device, destination_handle,
                                           false);
   source = d12_umd_command_resource(list->device, source_handle, false);
   if (destination == (void *)(uintptr_t)1 ||
       source == (void *)(uintptr_t)1) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->resolve_subresource(
      list->backend, destination->backend, destination_subresource,
      source->backend, source_subresource, (uint32_t)format);
}

bool
d12_umd_resource_subresource_extent(const struct d12_umd_resource *resource,
                                    UINT subresource, UINT *out_width,
                                    UINT *out_height)
{
   uint64_t width;
   uint64_t height;
   UINT mip;

   if (!resource || !out_width || !out_height ||
       !resource->desc.MipLevels || !resource->desc.DepthOrArraySize ||
       resource->desc.ResourceType == D3D12DDI_RT_BUFFER)
      return false;
   if ((uint64_t)subresource >=
       (uint64_t)resource->desc.MipLevels *
          resource->desc.DepthOrArraySize)
      return false;

   mip = subresource % resource->desc.MipLevels;
   width = resource->desc.Width;
   height = resource->desc.Height;
   for (UINT level = 0; level < mip; ++level) {
      if (width > 1)
         width >>= 1;
      if (height > 1)
         height >>= 1;
   }

   if (width > UINT32_MAX || height > UINT32_MAX)
      return false;
   *out_width = (UINT)width;
   *out_height = (UINT)height;
   return true;
}

static bool
d12_umd_classify_blt(
   const struct d12_umd_resource *destination,
   const struct d12_umd_resource *source,
   const struct d12_umd_blt_desc *desc,
   struct d12_present_blt_classification *classification)
{
   UINT destination_width;
   UINT destination_height;
   UINT source_width;
   UINT source_height;

   if (!destination || !source || !desc || !classification ||
       !d12_umd_resource_subresource_extent(
          destination, desc->destination_subresource,
          &destination_width, &destination_height) ||
       !d12_umd_resource_subresource_extent(
          source, desc->source_subresource, &source_width, &source_height))
      return false;

   const struct d12_present_blt_geometry geometry = {
      .destination_width = destination_width,
      .destination_height = destination_height,
      .destination_left = desc->destination_left,
      .destination_top = desc->destination_top,
      .destination_right = desc->destination_right,
      .destination_bottom = desc->destination_bottom,
      .source_width = source_width,
      .source_height = source_height,
      .source_left = desc->source_left,
      .source_top = desc->source_top,
      .source_right = desc->source_right,
      .source_bottom = desc->source_bottom,
      .destination_format = (uint32_t)destination->desc.Format,
      .source_format = (uint32_t)source->desc.Format,
      .rotation = (uint32_t)desc->rotation,
      .resolve = !!(desc->flags & D12_UMD_BLT_RESOLVE),
   };
   return d12_present_blt_classify(&geometry, classification);
}

HRESULT
d12_umd_validate_blt(struct d12_umd_device *device,
                     struct d12_umd_resource *destination,
                     struct d12_umd_resource *source,
                     const struct d12_umd_blt_desc *desc)
{
   const uint32_t supported_flags =
      D12_UMD_BLT_RESOLVE | D12_UMD_BLT_CONVERT |
      D12_UMD_BLT_STRETCH | D12_UMD_BLT_PRESENT;
   struct d12_present_blt_classification classification;

   if (!device || !destination || !source || !desc ||
       destination->device != device || source->device != device ||
       destination->desc.ResourceType == D3D12DDI_RT_BUFFER ||
       source->desc.ResourceType == D3D12DDI_RT_BUFFER ||
       !d12_umd_classify_blt(destination, source, desc, &classification) ||
       desc->source_left > INT32_MAX || desc->source_top > INT32_MAX ||
       desc->source_right > INT32_MAX || desc->source_bottom > INT32_MAX)
      return E_INVALIDARG;
   if (desc->flags & ~supported_flags)
      return E_INVALIDARG;

   if ((classification.format_conversion &&
        !(desc->flags & D12_UMD_BLT_CONVERT)) ||
       (classification.stretch && !(desc->flags & D12_UMD_BLT_STRETCH)))
      return E_INVALIDARG;

   if (desc->flags & D12_UMD_BLT_RESOLVE) {
      if (source->desc.SampleDesc.Count <= 1 ||
          destination->desc.SampleDesc.Count != 1)
         return E_INVALIDARG;
   } else if (source->desc.SampleDesc.Count != 1 ||
              destination->desc.SampleDesc.Count != 1) {
      return E_INVALIDARG;
   }

   /* Exact copies and exact same-format full resolves retain their small raw
    * kernels. Every compound operation is one formatted compute dispatch,
    * including Resolve+Convert+Stretch, as required for Present quality. */
   if (classification.shader_blt &&
       (destination->desc.ResourceType != D3D12DDI_RT_TEXTURE2D ||
        source->desc.ResourceType != D3D12DDI_RT_TEXTURE2D ||
        destination == source ||
        !d12_umd_blt_display_format(destination->desc.Format) ||
        !d12_umd_blt_display_format(source->desc.Format)))
      return E_NOTIMPL;
   return S_OK;
}

HRESULT
d12_umd_record_blt(struct d12_umd_device *device, void *command_list,
                   struct d12_umd_resource *destination,
                   struct d12_umd_resource *source,
                   const struct d12_umd_blt_desc *desc)
{
   HRESULT result = d12_umd_validate_blt(device, destination, source, desc);
   if (FAILED(result))
      return result;
   if (!command_list || !destination->backend || !source->backend ||
       !device->renderer_ops)
      return E_INVALIDARG;

   struct d12_present_blt_classification classification;
   if (!d12_umd_classify_blt(destination, source, desc, &classification))
      return E_FAIL;

   if (classification.shader_blt) {
      if (!device->renderer_ops->present_blt)
         return E_NOTIMPL;
      const struct d12_frontend_present_blt_desc translated = {
         .destination_subresource = desc->destination_subresource,
         .destination_left = desc->destination_left,
         .destination_top = desc->destination_top,
         .destination_right = desc->destination_right,
         .destination_bottom = desc->destination_bottom,
         .source_subresource = desc->source_subresource,
         .source_left = desc->source_left,
         .source_top = desc->source_top,
         .source_right = desc->source_right,
         .source_bottom = desc->source_bottom,
         .rotation = (uint32_t)desc->rotation,
         .resolve = !!(desc->flags & D12_UMD_BLT_RESOLVE),
      };
      return device->renderer_ops->present_blt(
         command_list, destination->backend, source->backend, &translated);
   }

   if (desc->flags & D12_UMD_BLT_RESOLVE) {
      if (!device->renderer_ops->resolve_subresource)
         return E_NOTIMPL;
      device->renderer_ops->resolve_subresource(
         command_list, destination->backend, desc->destination_subresource,
         source->backend, desc->source_subresource,
         (uint32_t)destination->desc.Format);
      return S_OK;
   }
   if (!device->renderer_ops->copy_texture_region)
      return E_NOTIMPL;

   const struct d12_frontend_texture_copy_location translated_destination = {
      .resource = destination->backend,
      .type = D12_FRONTEND_TEXTURE_COPY_SUBRESOURCE,
      .subresource = desc->destination_subresource,
   };
   const struct d12_frontend_texture_copy_location translated_source = {
      .resource = source->backend,
      .type = D12_FRONTEND_TEXTURE_COPY_SUBRESOURCE,
      .subresource = desc->source_subresource,
   };
   const struct d12_frontend_box source_box = {
      .left = (int32_t)desc->source_left,
      .top = (int32_t)desc->source_top,
      .front = 0,
      .right = (int32_t)desc->source_right,
      .bottom = (int32_t)desc->source_bottom,
      .back = 1,
   };
   device->renderer_ops->copy_texture_region(
      command_list, &translated_destination, desc->destination_left,
      desc->destination_top, 0, &translated_source, &source_box);
   return S_OK;
}

static void APIENTRY
d12_umd_blt(D3D12DDI_HCOMMANDLIST list_handle,
            const D3D12DDIARG_BLT *arguments)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_resource *destination;
   struct d12_umd_resource *source;

   if (!d12_umd_command_list_ready(list) || !arguments) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   destination = d12_umd_command_resource(list->device,
                                           arguments->hDstResource, false);
   source = d12_umd_command_resource(list->device, arguments->hSrcResource,
                                     false);
   if (destination == (void *)(uintptr_t)1 ||
       source == (void *)(uintptr_t)1) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   const struct d12_umd_blt_desc desc = {
      .destination_subresource = arguments->DstSubresource,
      .destination_left = arguments->DstLeft,
      .destination_top = arguments->DstTop,
      .destination_right = arguments->DstRight,
      .destination_bottom = arguments->DstBottom,
      .source_subresource = arguments->SrcSubresource,
      .source_left = arguments->SrcLeft,
      .source_top = arguments->SrcTop,
      .source_right = arguments->SrcRight,
      .source_bottom = arguments->SrcBottom,
      .flags = arguments->Flags.Value,
      .rotation = arguments->Rotate,
   };
   const HRESULT result = d12_umd_record_blt(
      list->device, list->backend, destination, source, &desc);
   if (FAILED(result))
      d12_umd_resource_command_error(list, result);
}

static void APIENTRY
d12_umd_resource_barriers(
   D3D12DDI_HCOMMANDLIST list_handle, UINT count,
   const D3D12DDIARG_RESOURCE_BARRIER_0003 *barriers)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_frontend_resource_barrier local[16];
   struct d12_frontend_resource_barrier *translated = local;

   if (!d12_umd_command_list_ready(list) || (count && !barriers) ||
       !list->device->renderer_ops->resource_barriers) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   if (count > ARRAYSIZE(local)) {
      if ((SIZE_T)count > SIZE_MAX / sizeof(*translated)) {
         d12_umd_resource_command_error(list, E_OUTOFMEMORY);
         return;
      }
      translated = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                             (SIZE_T)count * sizeof(*translated));
      if (!translated) {
         d12_umd_resource_command_error(list, E_OUTOFMEMORY);
         return;
      }
   }
   for (UINT index = 0; index < count; ++index) {
      struct d12_umd_resource *first;
      struct d12_umd_resource *second;
      translated[index].type = (uint32_t)barriers[index].Type;
      translated[index].flags = (uint32_t)barriers[index].Flags;
      if ((uint32_t)barriers[index].Flags &
          ~(uint32_t)(D3D12DDI_RESOURCE_BARRIER_FLAG_BEGIN_ONLY |
                      D3D12DDI_RESOURCE_BARRIER_FLAG_END_ONLY))
         goto invalid;
      switch (barriers[index].Type) {
      case D3D12DDI_RESOURCE_BARRIER_TYPE_TRANSITION:
         first = d12_umd_command_resource(
            list->device, barriers[index].Transition.hResource, false);
         if (first == (void *)(uintptr_t)1)
            goto invalid;
         translated[index].transition.resource = first->backend;
         translated[index].transition.subresource =
            barriers[index].Transition.Subresource;
         translated[index].transition.state_before =
            (uint32_t)barriers[index].Transition.StateBefore;
         translated[index].transition.state_after =
            (uint32_t)barriers[index].Transition.StateAfter;
         break;
      case D3D12DDI_RESOURCE_BARRIER_TYPE_ALIASING:
         first = d12_umd_command_resource(
            list->device, barriers[index].Aliasing.hResourceBefore, true);
         second = d12_umd_command_resource(
            list->device, barriers[index].Aliasing.hResourceAfter, true);
         if (first == (void *)(uintptr_t)1 || second == (void *)(uintptr_t)1)
            goto invalid;
         translated[index].aliasing.resource_before =
            first ? first->backend : NULL;
         translated[index].aliasing.resource_after =
            second ? second->backend : NULL;
         break;
      case D3D12DDI_RESOURCE_BARRIER_TYPE_UAV:
         first = d12_umd_command_resource(
            list->device, barriers[index].UAV.hResource, true);
         if (first == (void *)(uintptr_t)1)
            goto invalid;
         translated[index].uav.resource = first ? first->backend : NULL;
         break;
      default:
         goto invalid;
      }
   }
   list->device->renderer_ops->resource_barriers(
      list->backend, count, translated);
   if (translated != local)
      HeapFree(GetProcessHeap(), 0, translated);
   return;

invalid:
   if (translated != local)
      HeapFree(GetProcessHeap(), 0, translated);
   d12_umd_resource_command_error(list, E_INVALIDARG);
}

static bool
d12_umd_clear_arguments_valid(struct d12_umd_command_list *list,
                              uint64_t descriptor, UINT count,
                              const D3D12DDI_RECT *rects)
{
   return d12_umd_command_list_ready(list) && descriptor &&
          (!count || rects);
}

_Static_assert(sizeof(D3D12DDI_RECT) == sizeof(struct d12_frontend_rect),
               "DDI and frontend rectangles must match");

static void APIENTRY
d12_umd_clear_render_target(D3D12DDI_HCOMMANDLIST list_handle,
                            D3D12DDI_CPU_DESCRIPTOR_HANDLE descriptor,
                            const FLOAT color[4], UINT count,
                            const D3D12DDI_RECT *rects)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   if (!d12_umd_clear_arguments_valid(list, descriptor.ptr, count, rects) ||
       !color || !list->device->renderer_ops->clear_render_target) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->clear_render_target(
      list->backend, descriptor.ptr, color, count,
      (const struct d12_frontend_rect *)rects);
}

static void APIENTRY
d12_umd_clear_depth_stencil(D3D12DDI_HCOMMANDLIST list_handle,
                            D3D12DDI_CPU_DESCRIPTOR_HANDLE descriptor,
                            UINT flags, FLOAT depth, UINT8 stencil, UINT count,
                            const D3D12DDI_RECT *rects)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   if (!d12_umd_clear_arguments_valid(list, descriptor.ptr, count, rects) ||
       (flags & ~(D12_UMD_CLEAR_DEPTH | D12_UMD_CLEAR_STENCIL)) || !flags ||
       !list->device->renderer_ops->clear_depth_stencil) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   list->device->renderer_ops->clear_depth_stencil(
      list->backend, descriptor.ptr, flags, depth, stencil, count,
      (const struct d12_frontend_rect *)rects);
}

static void
d12_umd_clear_uav(D3D12DDI_HCOMMANDLIST list_handle,
                  D3D12DDI_GPU_DESCRIPTOR_HANDLE gpu_descriptor,
                  D3D12DDI_CPU_DESCRIPTOR_HANDLE cpu_descriptor,
                  D3D12DDI_HRESOURCE resource_handle, const void *values,
                  UINT count, const D3D12DDI_RECT *rects, bool is_float)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(list_handle);
   struct d12_umd_resource *resource;

   if (!d12_umd_clear_arguments_valid(list, cpu_descriptor.ptr, count,
                                      rects) ||
       !gpu_descriptor.ptr || !values) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   resource = d12_umd_command_resource(list->device, resource_handle, false);
   if (resource == (void *)(uintptr_t)1 ||
       (is_float ? !list->device->renderer_ops->clear_uav_float
                 : !list->device->renderer_ops->clear_uav_uint)) {
      d12_umd_resource_command_error(list, E_INVALIDARG);
      return;
   }
   if (is_float) {
      list->device->renderer_ops->clear_uav_float(
         list->backend, gpu_descriptor.ptr, cpu_descriptor.ptr,
         resource->backend, values, count,
         (const struct d12_frontend_rect *)rects);
   } else {
      list->device->renderer_ops->clear_uav_uint(
         list->backend, gpu_descriptor.ptr, cpu_descriptor.ptr,
         resource->backend, values, count,
         (const struct d12_frontend_rect *)rects);
   }
}

static void APIENTRY
d12_umd_clear_uav_uint(D3D12DDI_HCOMMANDLIST list_handle,
                       D3D12DDI_GPU_DESCRIPTOR_HANDLE gpu_descriptor,
                       D3D12DDI_CPU_DESCRIPTOR_HANDLE cpu_descriptor,
                       D3D12DDI_HRESOURCE resource, const UINT values[4],
                       UINT count, const D3D12DDI_RECT *rects)
{
   d12_umd_clear_uav(list_handle, gpu_descriptor, cpu_descriptor, resource,
                     values, count, rects, false);
}

static void APIENTRY
d12_umd_clear_uav_float(D3D12DDI_HCOMMANDLIST list_handle,
                        D3D12DDI_GPU_DESCRIPTOR_HANDLE gpu_descriptor,
                        D3D12DDI_CPU_DESCRIPTOR_HANDLE cpu_descriptor,
                        D3D12DDI_HRESOURCE resource, const FLOAT values[4],
                        UINT count, const D3D12DDI_RECT *rects)
{
   d12_umd_clear_uav(list_handle, gpu_descriptor, cpu_descriptor, resource,
                     values, count, rects, true);
}

void
d12_umd_install_resource_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table)
{
   table->pfnIASetVertexBuffers = d12_umd_set_vertex_buffers;
   table->pfnIASetIndexBuffer = d12_umd_set_index_buffer;
   table->pfnSOSetTargets = d12_umd_set_stream_output_targets;
   table->pfnDiscardResource = d12_umd_discard_resource;
   table->pfnOMSetRenderTargets = d12_umd_set_render_targets;
   table->pfnCopyBufferRegion = d12_umd_copy_buffer_region;
   table->pfnResourceCopy = d12_umd_copy_resource;
   table->pfnCopyTiles = d12_umd_copy_tiles;
   table->pfnCopyTextureRegion = d12_umd_copy_texture_region;
   table->pfnResourceResolveSubresource = d12_umd_resolve_subresource;
   table->pfnBlt = d12_umd_blt;
   table->pfnResourceBarrier = d12_umd_resource_barriers;
   table->pfnClearRenderTargetView = d12_umd_clear_render_target;
   table->pfnClearDepthStencilView = d12_umd_clear_depth_stencil;
   table->pfnClearUnorderedAccessViewUint = d12_umd_clear_uav_uint;
   table->pfnClearUnorderedAccessViewFloat = d12_umd_clear_uav_float;
}
