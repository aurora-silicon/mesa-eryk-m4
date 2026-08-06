/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12Resource for committed buffers: one agx_bo with a writeback (cached,
 * coherent on Apple UMA) CPU mapping. Textures are a later milestone (G3).
 */

#include "d12_private.h"

static HRESULT STDMETHODCALLTYPE
d12_resource_QueryInterface(ID3D12Resource *iface, REFIID riid, void **out)
{
   struct d12_resource *res = d12_resource_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12Pageable) ||
       d12_guid_equal(riid, &IID_ID3D12Resource)) {
      p_atomic_inc(&res->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12Resource::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_resource_AddRef(ID3D12Resource *iface)
{
   struct d12_resource *res = d12_resource_from_iface(iface);
   return p_atomic_inc_return(&res->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_resource_Release(ID3D12Resource *iface)
{
   struct d12_resource *res = d12_resource_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&res->refcount);

   if (refs == 0) {
      agx_bo_unreference(&res->device->dev, res->bo);
      if (res->stencil_bo)
         agx_bo_unreference(&res->device->dev, res->stencil_bo);
      d12_device_release(res->device);
      free(res);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_GetPrivateData(ID3D12Resource *iface, REFGUID guid, UINT *size,
                            void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_SetPrivateData(ID3D12Resource *iface, REFGUID guid, UINT size,
                            const void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_SetPrivateDataInterface(ID3D12Resource *iface, REFGUID guid,
                                     const IUnknown *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_SetName(ID3D12Resource *iface, LPCWSTR name)
{
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_GetDevice(ID3D12Resource *iface, REFIID riid, void **out)
{
   struct d12_resource *res = d12_resource_from_iface(iface);
   return ID3D12Device_QueryInterface(&res->device->iface, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_resource_Map(ID3D12Resource *iface, UINT subresource,
                 const D3D12_RANGE *read_range, void **data)
{
   struct d12_resource *res = d12_resource_from_iface(iface);

   if (subresource != 0)
      return E_INVALIDARG;

   /* Persistent coherent mapping; ranges are an optimization hint only. */
   if (data)
      *data = agx_bo_map(res->bo);
   return S_OK;
}

static void STDMETHODCALLTYPE
d12_resource_Unmap(ID3D12Resource *iface, UINT subresource,
                   const D3D12_RANGE *written_range)
{
   /* Coherent UMA: nothing to flush. */
}

static D3D12_RESOURCE_DESC STDMETHODCALLTYPE
d12_resource_GetDesc(ID3D12Resource *iface)
{
   struct d12_resource *res = d12_resource_from_iface(iface);
   return res->desc;
}

static D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE
d12_resource_GetGPUVirtualAddress(ID3D12Resource *iface)
{
   struct d12_resource *res = d12_resource_from_iface(iface);

   /* Per the D3D12 contract this is only valid for buffers. */
   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
      return 0;

   return res->addr;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_WriteToSubresource(ID3D12Resource *iface, UINT dst_subresource,
                                const D3D12_BOX *dst_box, const void *src,
                                UINT src_row_pitch, UINT src_depth_pitch)
{
   mesa_logw("ID3D12Resource::WriteToSubresource: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_ReadFromSubresource(ID3D12Resource *iface, void *dst,
                                 UINT dst_row_pitch, UINT dst_depth_pitch,
                                 UINT src_subresource,
                                 const D3D12_BOX *src_box)
{
   mesa_logw("ID3D12Resource::ReadFromSubresource: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_GetHeapProperties(ID3D12Resource *iface,
                               D3D12_HEAP_PROPERTIES *heap_props,
                               D3D12_HEAP_FLAGS *heap_flags)
{
   struct d12_resource *res = d12_resource_from_iface(iface);

   if (heap_props)
      *heap_props = res->heap_props;
   if (heap_flags)
      *heap_flags = D3D12_HEAP_FLAG_NONE;
   return S_OK;
}

static const ID3D12ResourceVtbl d12_resource_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12Resource_DEFAULTS,
   .QueryInterface = d12_resource_QueryInterface,
   .AddRef = d12_resource_AddRef,
   .Release = d12_resource_Release,
   .GetPrivateData = d12_resource_GetPrivateData,
   .SetPrivateData = d12_resource_SetPrivateData,
   .SetPrivateDataInterface = d12_resource_SetPrivateDataInterface,
   .SetName = d12_resource_SetName,
   .GetDevice = d12_resource_GetDevice,
   .Map = d12_resource_Map,
   .Unmap = d12_resource_Unmap,
   .GetDesc = d12_resource_GetDesc,
   .GetGPUVirtualAddress = d12_resource_GetGPUVirtualAddress,
   .WriteToSubresource = d12_resource_WriteToSubresource,
   .ReadFromSubresource = d12_resource_ReadFromSubresource,
   .GetHeapProperties = d12_resource_GetHeapProperties,
};

HRESULT
d12_resource_create_committed(struct d12_device *device,
                              const D3D12_HEAP_PROPERTIES *heap_props,
                              const D3D12_RESOURCE_DESC *desc, REFIID riid,
                              void **out)
{
   if (!out)
      return E_POINTER;
   if (!heap_props || !desc)
      return E_INVALIDARG;

   uint64_t size_B;
   struct ail_layout layout = {0};
   struct ail_layout stencil_layout = {0};
   bool is_depth_stencil = false;

   if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      if (desc->Width == 0)
         return E_INVALIDARG;
      size_B = desc->Width;
   } else if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
              d12_format_is_depth_stencil(desc->Format)) {
      /*
       * Depth/stencil. Two constraints come straight from the hardware:
       *
       * - The ZLS unit cannot address a linear buffer at all (Mesa's
       *   agx_translate_zls_tiling has no linear case), so unlike our colour
       *   targets these must be TWIDDLED. The CPU consequently cannot read
       *   depth by indexing rows; tests detwiddle with ail_detile.
       * - Stencil is its own plane with its own allocation.
       */
      enum pipe_format z = d12_format_depth_plane(desc->Format);
      enum pipe_format s = d12_format_stencil_plane(desc->Format);

      if (desc->MipLevels > 1 || desc->SampleDesc.Count > 1 || !desc->Width ||
          !desc->Height) {
         mesa_loge("CreateCommittedResource: depth/stencil shape unsupported "
                   "(mips %u, layers %u, samples %u)",
                   desc->MipLevels, desc->DepthOrArraySize,
                   desc->SampleDesc.Count);
         return E_NOTIMPL;
      }

      if (d12_format_depth_is_substituted(desc->Format)) {
         static bool warned = false;
         if (!warned) {
            warned = true;
            mesa_logw("depth format %d has no native equivalent on AGX; "
                      "using 32-bit float depth. Comparisons and clears "
                      "quantize as float32, not unorm24.",
                      (int)desc->Format);
         }
      }

      is_depth_stencil = true;

      /* Array layers, as for colour. The ZLS unit addresses one layer at a
       * time; the DSV selects which, and layers sit at layer_stride_B.
       */
      layout = (struct ail_layout){
         .width_px = (uint32_t)desc->Width,
         .height_px = desc->Height,
         .depth_px = MAX2(desc->DepthOrArraySize, 1),
         .sample_count_sa = 1,
         .levels = 1,
         .tiling = AIL_TILING_TWIDDLED,
         .format = z,
      };
      ail_make_miptree(&layout);
      size_B = layout.size_B;

      if (s != PIPE_FORMAT_NONE) {
         stencil_layout = (struct ail_layout){
            .width_px = (uint32_t)desc->Width,
            .height_px = desc->Height,
            .depth_px = MAX2(desc->DepthOrArraySize, 1),
            .sample_count_sa = 1,
            .levels = 1,
            .tiling = AIL_TILING_TWIDDLED,
            .format = s,
         };
         ail_make_miptree(&stencil_layout);
      }
   } else if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
      /* Single-level, single-layer, single-sample. LINEAR tiling so the
       * CPU can read pixels directly; twiddled layouts arrive with real
       * texturing. The FORMAT is now whatever the hardware can handle.
       */
      enum pipe_format pipe = d12_format_to_pipe(desc->Format);

      if (pipe == PIPE_FORMAT_NONE || desc->MipLevels > 1 ||
          desc->SampleDesc.Count > 1 || !desc->Width || !desc->Height) {
         mesa_loge("CreateCommittedResource: texture shape unsupported "
                   "(fmt %d, mips %u, layers %u, samples %u)",
                   (int)desc->Format, desc->MipLevels,
                   desc->DepthOrArraySize, desc->SampleDesc.Count);
         return E_NOTIMPL;
      }

      /* Refuse a render target the hardware cannot render to, rather than
       * accepting it and producing wrong pixels.
       */
      if ((desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) &&
          !d12_format_is_renderable(desc->Format)) {
         mesa_loge("CreateCommittedResource: format %d is not renderable on "
                   "this GPU", (int)desc->Format);
         return E_INVALIDARG;
      }

      /*
       * ARRAY LAYERS. ail computes a linear array as
       * layer_stride_B = align(linear_stride_B * height_px) and
       * size_B = layer_stride_B * depth_px, so an array costs nothing here
       * beyond passing the layer count through; the views are what has to
       * know about slices (see CreateRenderTargetView).
       *
       * Arrays were the single largest refusal cluster in the suite --
       * ~21 tests, measured per test rather than per message.
       */
      layout = (struct ail_layout){
         .width_px = (uint32_t)desc->Width,
         .height_px = desc->Height,
         .depth_px = MAX2(desc->DepthOrArraySize, 1),
         .sample_count_sa = 1,
         .levels = 1,
         .tiling = AIL_TILING_LINEAR,
         .format = pipe,
         /* 64-byte-aligned rows; the smoke tests mirror this constant */
         .linear_stride_B =
            align((uint32_t)desc->Width *
                     d12_format_bytes_per_pixel(desc->Format), 64),
      };
      ail_make_miptree(&layout);
      size_B = layout.size_B;
   } else {
      mesa_loge("CreateCommittedResource: dimension %d unsupported",
                (int)desc->Dimension);
      return E_NOTIMPL;
   }

   struct d12_resource *res = calloc(1, sizeof(*res));
   if (!res)
      return E_OUTOFMEMORY;

   res->layout = layout;
   res->stencil_layout = stencil_layout;
   res->is_depth_stencil = is_depth_stencil;

   /* UMA: every heap type is the same cached, coherent memory. */
   res->bo = agx_bo_create(&device->dev, size_B, 0, AGX_BO_WRITEBACK,
                           "d3d12 resource");
   if (!res->bo) {
      free(res);
      return E_OUTOFMEMORY;
   }

   if (is_depth_stencil && stencil_layout.size_B) {
      res->stencil_bo =
         agx_bo_create(&device->dev, stencil_layout.size_B, 0,
                       AGX_BO_WRITEBACK, "d3d12 stencil plane");
      if (!res->stencil_bo) {
         agx_bo_unreference(&device->dev, res->bo);
         free(res);
         return E_OUTOFMEMORY;
      }
   }

   /* A committed resource owns its whole BO, so it sits at offset 0. The
    * resolved addresses still go through these fields rather than being
    * read from the BO downstream, so a placed resource -- which does not --
    * needs no second code path. */
   res->addr = res->bo->va->addr;
   res->offset_B = 0;
   res->placed = false;
   res->stencil_addr = res->stencil_bo ? res->stencil_bo->va->addr : 0;

   res->iface.lpVtbl = (ID3D12ResourceVtbl *)&d12_resource_vtbl;
   res->refcount = 1;
   res->device = device;
   res->desc = *desc;
   res->heap_props = *heap_props;
   d12_device_add_ref(device);

   HRESULT hr = d12_resource_QueryInterface(&res->iface, riid, out);
   d12_resource_Release(&res->iface);
   return hr;
}
