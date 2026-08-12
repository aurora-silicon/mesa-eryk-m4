/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_umd_descriptor_funcs.h"

#include "d12_umd_internal.h"

#include <string.h>

static void
d12_umd_descriptor_error(struct d12_umd_device *device, HRESULT result)
{
   if (device && device->core_callbacks.pfnSetErrorCb)
      device->core_callbacks.pfnSetErrorCb(device->runtime_device, result);
}

static bool
d12_umd_descriptor_heap_desc_valid(
   const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *desc)
{
   const UINT supported_flags =
      D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE |
      D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

   if (!desc || desc->Type >= D3D12DDI_DESCRIPTOR_HEAP_TYPE_NUM_TYPES ||
       !desc->NumDescriptors || desc->NodeMask > 1 ||
       ((UINT)desc->Flags & ~supported_flags))
      return false;
   if ((desc->Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) &&
       desc->Type != D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
       desc->Type != D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER)
      return false;
   if (desc->Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER)
      return desc->NumDescriptors <= 2048;
   return desc->NumDescriptors <= 1000000;
}

static struct d12_frontend_descriptor_heap_desc
d12_umd_frontend_descriptor_heap_desc(
   const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *desc)
{
   uint32_t flags = 0;
   if (desc->Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE)
      flags |= D12_FRONTEND_DESCRIPTOR_HEAP_CPU_VISIBLE;
   if (desc->Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
      flags |= D12_FRONTEND_DESCRIPTOR_HEAP_SHADER_VISIBLE;
   return (struct d12_frontend_descriptor_heap_desc){
      .type = (uint32_t)desc->Type,
      .num_descriptors = desc->NumDescriptors,
      .flags = flags,
      .node_mask = desc->NodeMask,
   };
}

static SIZE_T APIENTRY
d12_umd_calc_private_descriptor_heap_size(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *desc)
{
   return d12_umd_device_from_handle(handle) &&
                 d12_umd_descriptor_heap_desc_valid(desc)
             ? sizeof(struct d12_umd_descriptor_heap)
             : 0;
}

static HRESULT APIENTRY
d12_umd_create_descriptor_heap(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 *desc,
   D3D12DDI_HDESCRIPTORHEAP heap_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_descriptor_heap *heap =
      (struct d12_umd_descriptor_heap *)heap_handle.pDrvPrivate;
   struct d12_frontend_descriptor_heap_desc frontend;
   HRESULT result;

   if (!device || !heap || !d12_umd_descriptor_heap_desc_valid(desc))
      return E_INVALIDARG;
   if (!device->backend || !device->renderer_ops ||
       !device->renderer_ops->create_descriptor_heap)
      return E_NOTIMPL;

   memset(heap, 0, sizeof(*heap));
   frontend = d12_umd_frontend_descriptor_heap_desc(desc);
   result = device->renderer_ops->create_descriptor_heap(
      device->backend, &frontend, &heap->backend);
   if (FAILED(result))
      return result;

   heap->cpu_start = device->renderer_ops->descriptor_heap_cpu_start(
      heap->backend);
   heap->gpu_start = device->renderer_ops->descriptor_heap_gpu_start(
      heap->backend);
   if ((desc->Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE) &&
       !heap->cpu_start) {
      device->renderer_ops->destroy_descriptor_heap(heap->backend);
      SecureZeroMemory(heap, sizeof(*heap));
      return E_OUTOFMEMORY;
   }
   if ((desc->Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) &&
       !heap->gpu_start) {
      device->renderer_ops->destroy_descriptor_heap(heap->backend);
      SecureZeroMemory(heap, sizeof(*heap));
      return E_OUTOFMEMORY;
   }

   heap->signature = D12_UMD_DESCRIPTOR_HEAP_SIGNATURE;
   heap->device = device;
   heap->desc = *desc;
   return S_OK;
}

static void APIENTRY
d12_umd_destroy_descriptor_heap(D3D12DDI_HDEVICE handle,
                                D3D12DDI_HDESCRIPTORHEAP heap_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_descriptor_heap *heap =
      d12_umd_descriptor_heap_from_handle(heap_handle);

   if (!device || !heap || heap->device != device)
      return;
   if (heap->backend && device->renderer_ops &&
       device->renderer_ops->destroy_descriptor_heap)
      device->renderer_ops->destroy_descriptor_heap(heap->backend);
   SecureZeroMemory(heap, sizeof(*heap));
}

static UINT APIENTRY
d12_umd_get_descriptor_size(D3D12DDI_HDEVICE handle,
                            D3D12DDI_DESCRIPTOR_HEAP_TYPE type)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   if (!device || type >= D3D12DDI_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return 0;
   }
   return D12_FRONTEND_DESCRIPTOR_SIZE;
}

static D3D12DDI_CPU_DESCRIPTOR_HANDLE APIENTRY
d12_umd_get_descriptor_heap_cpu_start(
   D3D12DDI_HDEVICE handle, D3D12DDI_HDESCRIPTORHEAP heap_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_descriptor_heap *heap =
      d12_umd_descriptor_heap_from_handle(heap_handle);
   if (!device || !heap || heap->device != device ||
       !(heap->desc.Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE)) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return (D3D12DDI_CPU_DESCRIPTOR_HANDLE){0};
   }
   return (D3D12DDI_CPU_DESCRIPTOR_HANDLE){.ptr = (SIZE_T)heap->cpu_start};
}

static D3D12DDI_GPU_DESCRIPTOR_HANDLE APIENTRY
d12_umd_get_descriptor_heap_gpu_start(
   D3D12DDI_HDEVICE handle, D3D12DDI_HDESCRIPTORHEAP heap_handle)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_descriptor_heap *heap =
      d12_umd_descriptor_heap_from_handle(heap_handle);
   if (!device || !heap || heap->device != device ||
       !(heap->desc.Flags & D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return (D3D12DDI_GPU_DESCRIPTOR_HANDLE){0};
   }
   return (D3D12DDI_GPU_DESCRIPTOR_HANDLE){.ptr = heap->gpu_start};
}

static void APIENTRY
d12_umd_set_descriptor_heaps(D3D12DDI_HCOMMANDLIST handle, UINT count,
                             D3D12DDI_HDESCRIPTORHEAP *heap_handles)
{
   struct d12_umd_command_list *list =
      d12_umd_command_list_from_handle(handle);
   struct d12_umd_device *device = list ? list->device : NULL;
   void *heaps[2];
   bool seen_resource = false;
   bool seen_sampler = false;

   if (!list || !list->backend || !device || count > 2 ||
       (count && !heap_handles) || !device->renderer_ops ||
       !device->renderer_ops->set_descriptor_heaps) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   for (UINT index = 0; index < count; ++index) {
      struct d12_umd_descriptor_heap *heap =
         d12_umd_descriptor_heap_from_handle(heap_handles[index]);
      if (!heap || heap->device != device || !heap->backend ||
          !(heap->desc.Flags &
            D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ||
          (heap->desc.Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
              ? seen_resource
              : heap->desc.Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER
                   ? seen_sampler
                   : true)) {
         d12_umd_descriptor_error(device, E_INVALIDARG);
         return;
      }
      seen_resource |=
         heap->desc.Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      seen_sampler |=
         heap->desc.Type == D3D12DDI_DESCRIPTOR_HEAP_TYPE_SAMPLER;
      heaps[index] = heap->backend;
   }
   device->renderer_ops->set_descriptor_heaps(list->backend, count, heaps);
}

static struct d12_umd_resource *
d12_umd_descriptor_resource(struct d12_umd_device *device,
                            D3D12DDI_HRESOURCE handle)
{
   struct d12_umd_resource *resource =
      (struct d12_umd_resource *)handle.pDrvPrivate;
   if (!resource)
      return NULL;
   return resource->signature == D12_UMD_RESOURCE_SIGNATURE &&
                 resource->device == device && resource->backend
             ? resource
             : NULL;
}

static bool
d12_umd_descriptor_backend_ready(struct d12_umd_device *device)
{
   return device && device->backend && device->renderer_ops;
}

static void APIENTRY
d12_umd_create_srv(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 *arguments,
   D3D12DDI_CPU_DESCRIPTOR_HANDLE destination)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource;
   struct d12_frontend_srv_desc desc;

   if (!d12_umd_descriptor_backend_ready(device) || !arguments ||
       !destination.ptr || arguments->ResourceDimension < D3D12DDI_RD_BUFFER ||
       arguments->ResourceDimension > D3D12DDI_RD_TEXTURECUBE ||
       !device->renderer_ops->create_shader_resource_view) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   resource = d12_umd_descriptor_resource(device, arguments->hDrvResource);
   if (arguments->hDrvResource.pDrvPrivate && !resource) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   memset(&desc, 0, sizeof(desc));
   desc.format = (uint32_t)arguments->Format;
   desc.dimension = (uint32_t)arguments->ResourceDimension;
   desc.component_mapping = arguments->Shader4ComponentMapping;
   switch (arguments->ResourceDimension) {
   case D3D12DDI_RD_BUFFER:
      desc.buffer.first_element = arguments->Buffer.FirstElement;
      desc.buffer.num_elements = arguments->Buffer.NumElements;
      desc.buffer.stride = arguments->Buffer.StructureByteStride;
      desc.buffer.flags = (uint32_t)arguments->Buffer.Flags;
      if (desc.buffer.flags & ~(uint32_t)D3D12DDI_BUFFER_SRV_FLAG_RAW) {
         d12_umd_descriptor_error(device, E_INVALIDARG);
         return;
      }
      break;
   case D3D12DDI_RD_TEXTURE1D:
      desc.texture.first_mip = arguments->Tex1D.MostDetailedMip;
      desc.texture.first_slice = arguments->Tex1D.FirstArraySlice;
      desc.texture.mip_levels = arguments->Tex1D.MipLevels;
      desc.texture.array_size = arguments->Tex1D.ArraySize;
      desc.texture.min_lod = arguments->Tex1D.ResourceMinLODClamp;
      break;
   case D3D12DDI_RD_TEXTURE2D:
      desc.texture.first_mip = arguments->Tex2D.MostDetailedMip;
      desc.texture.first_slice = arguments->Tex2D.FirstArraySlice;
      desc.texture.mip_levels = arguments->Tex2D.MipLevels;
      desc.texture.array_size = arguments->Tex2D.ArraySize;
      desc.texture.plane_slice = arguments->Tex2D.PlaneSlice;
      desc.texture.min_lod = arguments->Tex2D.ResourceMinLODClamp;
      break;
   case D3D12DDI_RD_TEXTURE3D:
      desc.texture.first_mip = arguments->Tex3D.MostDetailedMip;
      desc.texture.mip_levels = arguments->Tex3D.MipLevels;
      desc.texture.array_size = 1;
      desc.texture.min_lod = arguments->Tex3D.ResourceMinLODClamp;
      break;
   case D3D12DDI_RD_TEXTURECUBE:
      desc.cube.first_mip = arguments->TexCube.MostDetailedMip;
      desc.cube.mip_levels = arguments->TexCube.MipLevels;
      desc.cube.first_face = arguments->TexCube.First2DArrayFace;
      desc.cube.num_cubes = arguments->TexCube.NumCubes;
      desc.cube.min_lod = arguments->TexCube.ResourceMinLODClamp;
      break;
   default:
      return;
   }
   device->renderer_ops->create_shader_resource_view(
      device->backend, resource ? resource->backend : NULL, &desc,
      destination.ptr);
}

static void APIENTRY
d12_umd_create_uav(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 *arguments,
   D3D12DDI_CPU_DESCRIPTOR_HANDLE destination)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource;
   struct d12_umd_resource *counter = NULL;
   struct d12_frontend_uav_desc desc;

   if (!d12_umd_descriptor_backend_ready(device) || !arguments ||
       !destination.ptr || arguments->ResourceDimension < D3D12DDI_RD_BUFFER ||
       arguments->ResourceDimension > D3D12DDI_RD_TEXTURE3D ||
       !device->renderer_ops->create_unordered_access_view) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   resource = d12_umd_descriptor_resource(device, arguments->hDrvResource);
   if (arguments->hDrvResource.pDrvPrivate && !resource) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   memset(&desc, 0, sizeof(desc));
   desc.format = (uint32_t)arguments->Format;
   desc.dimension = (uint32_t)arguments->ResourceDimension;
   switch (arguments->ResourceDimension) {
   case D3D12DDI_RD_BUFFER:
      counter = d12_umd_descriptor_resource(
         device, arguments->Buffer.hDrvCounterResource);
      if (arguments->Buffer.hDrvCounterResource.pDrvPrivate && !counter) {
         d12_umd_descriptor_error(device, E_INVALIDARG);
         return;
      }
      desc.buffer.first_element = arguments->Buffer.FirstElement;
      desc.buffer.num_elements = arguments->Buffer.NumElements;
      desc.buffer.stride = arguments->Buffer.StructureByteStride;
      desc.buffer.counter_offset = arguments->Buffer.CounterOffsetInBytes;
      desc.buffer.flags = (uint32_t)arguments->Buffer.Flags;
      if (desc.buffer.flags & ~(uint32_t)D3D12DDI_BUFFER_UAV_FLAG_RAW) {
         d12_umd_descriptor_error(device, E_INVALIDARG);
         return;
      }
      break;
   case D3D12DDI_RD_TEXTURE1D:
      desc.texture.mip_slice = arguments->Tex1D.MipSlice;
      desc.texture.first_slice = arguments->Tex1D.FirstArraySlice;
      desc.texture.array_size = arguments->Tex1D.ArraySize;
      break;
   case D3D12DDI_RD_TEXTURE2D:
      desc.texture.mip_slice = arguments->Tex2D.MipSlice;
      desc.texture.first_slice = arguments->Tex2D.FirstArraySlice;
      desc.texture.array_size = arguments->Tex2D.ArraySize;
      desc.texture.plane_slice = arguments->Tex2D.PlaneSlice;
      break;
   case D3D12DDI_RD_TEXTURE3D:
      desc.texture.mip_slice = arguments->Tex3D.MipSlice;
      desc.texture.first_slice = arguments->Tex3D.FirstW;
      desc.texture.array_size = arguments->Tex3D.WSize;
      break;
   default:
      return;
   }
   device->renderer_ops->create_unordered_access_view(
      device->backend, resource ? resource->backend : NULL,
      counter ? counter->backend : NULL, &desc, destination.ptr);
}

static void APIENTRY
d12_umd_create_rtv(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002 *arguments,
   D3D12DDI_CPU_DESCRIPTOR_HANDLE destination)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource;
   struct d12_frontend_rtv_desc desc;

   if (!d12_umd_descriptor_backend_ready(device) || !arguments ||
       !destination.ptr || arguments->ResourceDimension < D3D12DDI_RD_BUFFER ||
       arguments->ResourceDimension > D3D12DDI_RD_TEXTURECUBE ||
       !device->renderer_ops->create_render_target_view) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   resource = d12_umd_descriptor_resource(device, arguments->hDrvResource);
   if (arguments->hDrvResource.pDrvPrivate && !resource) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   memset(&desc, 0, sizeof(desc));
   desc.format = (uint32_t)arguments->Format;
   desc.dimension = (uint32_t)arguments->ResourceDimension;
   switch (arguments->ResourceDimension) {
   case D3D12DDI_RD_BUFFER:
      desc.buffer.first_element = arguments->Buffer.FirstElement;
      desc.buffer.num_elements = arguments->Buffer.NumElements;
      break;
   case D3D12DDI_RD_TEXTURE1D:
      desc.texture.mip_slice = arguments->Tex1D.MipSlice;
      desc.texture.first_slice = arguments->Tex1D.FirstArraySlice;
      desc.texture.array_size = arguments->Tex1D.ArraySize;
      break;
   case D3D12DDI_RD_TEXTURE2D:
      desc.texture.mip_slice = arguments->Tex2D.MipSlice;
      desc.texture.first_slice = arguments->Tex2D.FirstArraySlice;
      desc.texture.array_size = arguments->Tex2D.ArraySize;
      desc.texture.plane_slice = arguments->Tex2D.PlaneSlice;
      break;
   case D3D12DDI_RD_TEXTURE3D:
      desc.texture.mip_slice = arguments->Tex3D.MipSlice;
      desc.texture.first_slice = arguments->Tex3D.FirstW;
      desc.texture.array_size = arguments->Tex3D.WSize;
      break;
   case D3D12DDI_RD_TEXTURECUBE:
      desc.texture.mip_slice = arguments->TexCube.MipSlice;
      desc.texture.first_slice = arguments->TexCube.FirstArraySlice;
      desc.texture.array_size = arguments->TexCube.ArraySize;
      break;
   default:
      return;
   }
   device->renderer_ops->create_render_target_view(
      device->backend, resource ? resource->backend : NULL, &desc,
      destination.ptr);
}

static void APIENTRY
d12_umd_create_dsv(
   D3D12DDI_HDEVICE handle,
   const D3D12DDIARG_CREATE_DEPTH_STENCIL_VIEW *arguments,
   D3D12DDI_CPU_DESCRIPTOR_HANDLE destination)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_umd_resource *resource;
   struct d12_frontend_dsv_desc desc;

   if (!d12_umd_descriptor_backend_ready(device) || !arguments ||
       !destination.ptr || arguments->ResourceDimension < D3D12DDI_RD_TEXTURE1D ||
       arguments->ResourceDimension > D3D12DDI_RD_TEXTURECUBE ||
       arguments->ResourceDimension == D3D12DDI_RD_TEXTURE3D ||
       ((UINT)arguments->Flags &
        ~(UINT)D3D12DDI_CREATE_DSV_FLAG_MASK) ||
       !device->renderer_ops->create_depth_stencil_view) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   resource = d12_umd_descriptor_resource(device, arguments->hDrvResource);
   if (arguments->hDrvResource.pDrvPrivate && !resource) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   desc = (struct d12_frontend_dsv_desc){
      .format = (uint32_t)arguments->Format,
      .dimension = (uint32_t)arguments->ResourceDimension,
      .flags = (uint32_t)arguments->Flags,
   };
   if (arguments->ResourceDimension == D3D12DDI_RD_TEXTURE1D) {
      desc.mip_slice = arguments->Tex1D.MipSlice;
      desc.first_slice = arguments->Tex1D.FirstArraySlice;
      desc.array_size = arguments->Tex1D.ArraySize;
   } else if (arguments->ResourceDimension == D3D12DDI_RD_TEXTURE2D) {
      desc.mip_slice = arguments->Tex2D.MipSlice;
      desc.first_slice = arguments->Tex2D.FirstArraySlice;
      desc.array_size = arguments->Tex2D.ArraySize;
   } else {
      desc.mip_slice = arguments->TexCube.MipSlice;
      desc.first_slice = arguments->TexCube.FirstArraySlice;
      desc.array_size = arguments->TexCube.ArraySize;
   }
   device->renderer_ops->create_depth_stencil_view(
      device->backend, resource ? resource->backend : NULL, &desc,
      destination.ptr);
}

static void APIENTRY
d12_umd_create_cbv(D3D12DDI_HDEVICE handle,
                   const D3D12DDI_CONSTANT_BUFFER_VIEW_DESC *arguments,
                   D3D12DDI_CPU_DESCRIPTOR_HANDLE destination)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   if (!d12_umd_descriptor_backend_ready(device) || !destination.ptr ||
       !device->renderer_ops->create_constant_buffer_view) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   device->renderer_ops->create_constant_buffer_view(
      device->backend, arguments ? arguments->BufferLocation : 0,
      arguments ? arguments->SizeInBytes : 0, destination.ptr);
}

static void APIENTRY
d12_umd_create_sampler(D3D12DDI_HDEVICE handle,
                       const D3D12DDIARG_CREATE_SAMPLER *arguments,
                       D3D12DDI_CPU_DESCRIPTOR_HANDLE destination)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   struct d12_frontend_sampler_desc desc;
   const D3D12DDI_SAMPLER_DESC *source;

   if (!d12_umd_descriptor_backend_ready(device) || !arguments ||
       !arguments->pSamplerDesc || !destination.ptr ||
       !device->renderer_ops->create_sampler) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   source = arguments->pSamplerDesc;
   desc = (struct d12_frontend_sampler_desc){
      .filter = (uint32_t)source->Filter,
      .address_u = (uint32_t)source->AddressU,
      .address_v = (uint32_t)source->AddressV,
      .address_w = (uint32_t)source->AddressW,
      .mip_lod_bias = source->MipLODBias,
      .max_anisotropy = source->MaxAnisotropy,
      .comparison_func = (uint32_t)source->ComparisonFunc,
      .min_lod = source->MinLOD,
      .max_lod = source->MaxLOD,
   };
   memcpy(desc.border_color, source->BorderColor,
          sizeof(desc.border_color));
   device->renderer_ops->create_sampler(
      device->backend, &desc, destination.ptr);
}

static void APIENTRY
d12_umd_copy_descriptors_simple(
   D3D12DDI_HDEVICE handle, UINT count,
   D3D12DDI_CPU_DESCRIPTOR_HANDLE destination,
   D3D12DDI_CPU_DESCRIPTOR_HANDLE source,
   D3D12DDI_DESCRIPTOR_HEAP_TYPE type)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   if (!d12_umd_descriptor_backend_ready(device) || !count ||
       !destination.ptr || !source.ptr ||
       type >= D3D12DDI_DESCRIPTOR_HEAP_TYPE_NUM_TYPES ||
       !device->renderer_ops->copy_descriptors_simple) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   device->renderer_ops->copy_descriptors_simple(
      device->backend, count, destination.ptr, source.ptr, (uint32_t)type);
}

static void APIENTRY
d12_umd_copy_descriptors(
   D3D12DDI_HDEVICE handle, UINT destination_range_count,
   const D3D12DDI_CPU_DESCRIPTOR_HANDLE *destination_starts,
   const UINT *destination_sizes, UINT source_range_count,
   const D3D12DDI_CPU_DESCRIPTOR_HANDLE *source_starts,
   const UINT *source_sizes, D3D12DDI_DESCRIPTOR_HEAP_TYPE type)
{
   struct d12_umd_device *device = d12_umd_device_from_handle(handle);
   UINT destination_range = 0, source_range = 0;
   UINT destination_offset = 0, source_offset = 0;

   if (!d12_umd_descriptor_backend_ready(device) ||
       !destination_range_count || !source_range_count ||
       !destination_starts || !source_starts ||
       type >= D3D12DDI_DESCRIPTOR_HEAP_TYPE_NUM_TYPES ||
       !device->renderer_ops->copy_descriptors_simple) {
      d12_umd_descriptor_error(device, E_INVALIDARG);
      return;
   }
   while (destination_range < destination_range_count &&
          source_range < source_range_count) {
      const UINT destination_count =
         destination_sizes ? destination_sizes[destination_range] : 1;
      const UINT source_count = source_sizes ? source_sizes[source_range] : 1;
      if (destination_offset >= destination_count) {
         ++destination_range;
         destination_offset = 0;
         continue;
      }
      if (source_offset >= source_count) {
         ++source_range;
         source_offset = 0;
         continue;
      }
      const UINT count =
         min(destination_count - destination_offset,
             source_count - source_offset);
      const uint64_t destination =
         destination_starts[destination_range].ptr +
         (uint64_t)destination_offset * D12_FRONTEND_DESCRIPTOR_SIZE;
      const uint64_t source = source_starts[source_range].ptr +
         (uint64_t)source_offset * D12_FRONTEND_DESCRIPTOR_SIZE;
      if (!destination || !source) {
         d12_umd_descriptor_error(device, E_INVALIDARG);
         return;
      }
      device->renderer_ops->copy_descriptors_simple(
         device->backend, count, destination, source, (uint32_t)type);
      destination_offset += count;
      source_offset += count;
   }
}

void
d12_umd_install_descriptor_device_functions(
   D3D12DDI_DEVICE_FUNCS_CORE_0003 *table)
{
   table->pfnCalcPrivateDescriptorHeapSize =
      d12_umd_calc_private_descriptor_heap_size;
   table->pfnCreateDescriptorHeap = d12_umd_create_descriptor_heap;
   table->pfnDestroyDescriptorHeap = d12_umd_destroy_descriptor_heap;
   table->pfnGetDescriptorSizeInBytes = d12_umd_get_descriptor_size;
   table->pfnGetCPUDescriptorHandleForHeapStart =
      d12_umd_get_descriptor_heap_cpu_start;
   table->pfnGetGPUDescriptorHandleForHeapStart =
      d12_umd_get_descriptor_heap_gpu_start;
   table->pfnCreateShaderResourceView = d12_umd_create_srv;
   table->pfnCreateConstantBufferView = d12_umd_create_cbv;
   table->pfnCreateSampler = d12_umd_create_sampler;
   table->pfnCreateUnorderedAccessView = d12_umd_create_uav;
   table->pfnCreateRenderTargetView = d12_umd_create_rtv;
   table->pfnCreateDepthStencilView = d12_umd_create_dsv;
   table->pfnCopyDescriptors = d12_umd_copy_descriptors;
   table->pfnCopyDescriptorsSimple = d12_umd_copy_descriptors_simple;
}

void
d12_umd_install_descriptor_command_list_functions(
   D3D12DDI_COMMAND_LIST_FUNCS_3D_0003 *table)
{
   table->pfnSetDescriptorHeaps = d12_umd_set_descriptor_heaps;
}
