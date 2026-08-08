/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12Resource for committed buffers: one agx_bo with a writeback (cached,
 * coherent on Apple UMA) CPU mapping. Textures are a later milestone (G3).
 */

#include "d12_private.h"

/* DirectX-Guids in the current build does not export this d3dcommon IID. */
static const GUID d12_iid_destruction_notifier = {
   0xa06eb39a, 0x50da, 0x425b,
   {0x8c, 0x31, 0x4e, 0xec, 0xd6, 0xc2, 0x70, 0xf3},
};

static ULONG STDMETHODCALLTYPE
d12_resource_AddRef(ID3D12Resource *iface);

static ULONG STDMETHODCALLTYPE
d12_notifier_AddRef(ID3DDestructionNotifier *iface);

static void
d12_notifier_notify_and_destroy(struct d12_destruction_notifier *notifier);

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

   if (d12_guid_equal(riid, &d12_iid_destruction_notifier)) {
      d12_notifier_AddRef(&res->destruction_notifier.iface);
      *out = &res->destruction_notifier.iface;
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
      /*
       * The callback observes the resource's implicit relationships, so run
       * it before dropping backing storage or the device reference. The
       * notifier detaches its list before calling user code: a callback must
       * never run while our mutex is held.
       */
      d12_notifier_notify_and_destroy(&res->destruction_notifier);

      /* A PLACED resource borrows the heap's BO -- unreferencing it here
       * would free storage the heap still owns and other placed resources
       * are still using. Drop the heap reference instead. */
      if (res->placed) {
         /* Drop the BO reference taken at creation; the heap object's own
          * refcount was never touched. */
         agx_bo_unreference(&res->device->dev, res->bo);
      } else {
         agx_bo_unreference(&res->device->dev, res->bo);
         if (res->stencil_bo)
            agx_bo_unreference(&res->device->dev, res->stencil_bo);
      }

      d12_private_data_store_destroy(&res->private_data);
      d12_device_release(res->device);
      free(res);
   }

   return refs;
}

static inline struct d12_destruction_notifier *
d12_notifier_from_iface(ID3DDestructionNotifier *iface)
{
   return (struct d12_destruction_notifier *)iface;
}

static HRESULT STDMETHODCALLTYPE
d12_notifier_QueryInterface(ID3DDestructionNotifier *iface, REFIID riid,
                            void **out)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);
   return d12_resource_QueryInterface(&notifier->resource->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE
d12_notifier_AddRef(ID3DDestructionNotifier *iface)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);
   return d12_resource_AddRef(&notifier->resource->iface);
}

static ULONG STDMETHODCALLTYPE
d12_notifier_Release(ID3DDestructionNotifier *iface)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);
   return d12_resource_Release(&notifier->resource->iface);
}

static HRESULT STDMETHODCALLTYPE
d12_notifier_RegisterDestructionCallback(ID3DDestructionNotifier *iface,
                                         PFN_DESTRUCTION_CALLBACK callback,
                                         void *data, UINT *callback_id)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);

   /* Reject before touching the caller's output value. */
   if (!callback)
      return DXGI_ERROR_INVALID_CALL;

   simple_mtx_lock(&notifier->lock);

   if (notifier->notified) {
      simple_mtx_unlock(&notifier->lock);
      return DXGI_ERROR_INVALID_CALL;
   }

   if (notifier->callback_count == notifier->callback_capacity) {
      uint32_t capacity = notifier->callback_capacity
                              ? 2 * notifier->callback_capacity
                              : 4;
      struct d12_destruction_callback *callbacks =
         realloc(notifier->callbacks, capacity * sizeof(*callbacks));

      if (!callbacks) {
         simple_mtx_unlock(&notifier->lock);
         return E_OUTOFMEMORY;
      }

      notifier->callbacks = callbacks;
      notifier->callback_capacity = capacity;
   }

   struct d12_destruction_callback *entry =
      &notifier->callbacks[notifier->callback_count++];
   entry->callback = callback;
   entry->data = data;
   entry->id = 0;

   if (callback_id) {
      /* Zero is reserved: it is never a valid callback ID. */
      if (!(++notifier->next_callback_id))
         ++notifier->next_callback_id;

      entry->id = notifier->next_callback_id;
      *callback_id = entry->id;
   }

   simple_mtx_unlock(&notifier->lock);
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_notifier_UnregisterDestructionCallback(ID3DDestructionNotifier *iface,
                                           UINT callback_id)
{
   struct d12_destruction_notifier *notifier = d12_notifier_from_iface(iface);

   if (!callback_id)
      return DXGI_ERROR_NOT_FOUND;

   simple_mtx_lock(&notifier->lock);

   for (uint32_t i = 0; i < notifier->callback_count; ++i) {
      if (notifier->callbacks[i].id != callback_id)
         continue;

      /* Preserve invocation order for the remaining registrations. */
      memmove(&notifier->callbacks[i], &notifier->callbacks[i + 1],
              (notifier->callback_count - i - 1) *
                 sizeof(notifier->callbacks[0]));
      --notifier->callback_count;
      simple_mtx_unlock(&notifier->lock);
      return S_OK;
   }

   simple_mtx_unlock(&notifier->lock);
   return DXGI_ERROR_NOT_FOUND;
}

static const ID3DDestructionNotifierVtbl d12_notifier_vtbl = {
   .QueryInterface = d12_notifier_QueryInterface,
   .AddRef = d12_notifier_AddRef,
   .Release = d12_notifier_Release,
   .RegisterDestructionCallback = d12_notifier_RegisterDestructionCallback,
   .UnregisterDestructionCallback = d12_notifier_UnregisterDestructionCallback,
};

static void
d12_notifier_init(struct d12_destruction_notifier *notifier,
                  struct d12_resource *resource)
{
   notifier->iface.lpVtbl = (ID3DDestructionNotifierVtbl *)&d12_notifier_vtbl;
   notifier->resource = resource;
   simple_mtx_init(&notifier->lock, mtx_plain);
}

static void
d12_notifier_notify_and_destroy(struct d12_destruction_notifier *notifier)
{
   struct d12_destruction_callback *callbacks;
   uint32_t callback_count;

   simple_mtx_lock(&notifier->lock);
   callbacks = notifier->callbacks;
   callback_count = notifier->callback_count;
   notifier->callbacks = NULL;
   notifier->callback_count = 0;
   notifier->callback_capacity = 0;
   notifier->notified = true;
   simple_mtx_unlock(&notifier->lock);

   for (uint32_t i = 0; i < callback_count; ++i)
      callbacks[i].callback(callbacks[i].data);

   free(callbacks);
   simple_mtx_destroy(&notifier->lock);
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_resource, ID3D12Resource,
                                d12_resource_from_iface)

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

   /* DEFAULT heaps have no application-visible CPU mapping even on UMA.
    * For an UNKNOWN-layout texture on a CPU-visible custom heap, Map(NULL)
    * is still legal (it establishes the mapping without exposing the VA),
    * but returning a pointer would falsely describe the opaque layout as a
    * linear byte array.  Rejections must leave the caller's sentinel alone. */
   if (res->heap_props.Type == D3D12_HEAP_TYPE_DEFAULT ||
       res->heap_props.CPUPageProperty ==
          D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE ||
       (data && res->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER &&
        res->desc.Layout == D3D12_TEXTURE_LAYOUT_UNKNOWN))
      return E_INVALIDARG;

   /* Persistent coherent mapping; ranges are an optimization hint only.
    * offset_B is nonzero for a PLACED resource, which is a window into a
    * heap -- mapping the BO base would hand back the start of the heap. */
   if (data)
      *data = (uint8_t *)agx_bo_map(res->bo) + res->offset_B;
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

struct d12_cpu_subresource {
   uint8_t *map;
   uint32_t x_el;
   uint32_t y_el;
   uint32_t width_el;
   uint32_t height_el;
   uint32_t blocksize_B;
   uint32_t texture_pitch_B;
   bool empty;
};

static HRESULT
d12_resource_cpu_subresource(struct d12_resource *res, UINT subresource,
                             const D3D12_BOX *box,
                             struct d12_cpu_subresource *out)
{
   memset(out, 0, sizeof(*out));

   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       res->heap_props.Type != D3D12_HEAP_TYPE_CUSTOM ||
       res->heap_props.CPUPageProperty ==
          D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE ||
       subresource != 0)
      return E_INVALIDARG;

   const enum pipe_format format = res->layout.format;
   const uint32_t block_width = util_format_get_blockwidth(format);
   const uint32_t block_height = util_format_get_blockheight(format);
   const uint32_t blocksize = util_format_get_blocksize(format);
   const uint32_t width = res->layout.width_px;
   const uint32_t height = res->layout.height_px;
   const D3D12_BOX whole = {0, 0, 0, width, height, 1};
   const D3D12_BOX *b = box ? box : &whole;

   /* Coordinates must still name the resource even when the box is empty:
    * {width,height,depth,width,height,depth} is a valid no-op, while an empty
    * box at depth + 1 is invalid. This distinction is covered explicitly by
    * the conformance test.
    */
   if (b->left > width || b->right > width || b->top > height ||
       b->bottom > height || b->front > 1 || b->back > 1)
      return E_INVALIDARG;

   if (b->left >= b->right || b->top >= b->bottom || b->front >= b->back) {
      out->empty = true;
      return S_OK;
   }

   if (b->front != 0 || b->back != 1)
      return E_INVALIDARG;

   /* BC regions are expressed in pixels but copied in whole format blocks.
    * The right/bottom edge may be the image edge; interior coordinates must
    * be block aligned.
    */
   if (b->left % block_width || b->top % block_height ||
       (b->right % block_width && b->right != width) ||
       (b->bottom % block_height && b->bottom != height))
      return E_INVALIDARG;

   if (res->layout.tiling != AIL_TILING_LINEAR)
      return E_INVALIDARG;

   const uint32_t right_el = DIV_ROUND_UP(b->right, block_width);
   const uint32_t bottom_el = DIV_ROUND_UP(b->bottom, block_height);
   *out = (struct d12_cpu_subresource){
      .map = (uint8_t *)agx_bo_map(res->bo) + res->offset_B,
      .x_el = b->left / block_width,
      .y_el = b->top / block_height,
      .width_el = right_el - b->left / block_width,
      .height_el = bottom_el - b->top / block_height,
      .blocksize_B = blocksize,
      .texture_pitch_B = ail_get_linear_stride_B(&res->layout, 0),
   };

   const uint64_t last =
      (uint64_t)(out->y_el + out->height_el - 1) * out->texture_pitch_B +
      (uint64_t)(out->x_el + out->width_el) * blocksize;
   if (last > res->layout.size_B ||
       res->offset_B > res->bo->size ||
       last > res->bo->size - res->offset_B)
      return E_INVALIDARG;

   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_WriteToSubresource(ID3D12Resource *iface, UINT dst_subresource,
                                const D3D12_BOX *dst_box, const void *src,
                                UINT src_row_pitch, UINT src_depth_pitch)
{
   (void)src_depth_pitch;

   struct d12_resource *res = d12_resource_from_iface(iface);
   struct d12_cpu_subresource surface;
   HRESULT hr = d12_resource_cpu_subresource(res, dst_subresource, dst_box,
                                             &surface);
   if (FAILED(hr) || surface.empty)
      return hr;
   if (!src)
      return E_INVALIDARG;

   const uint64_t row_bytes =
      (uint64_t)surface.width_el * surface.blocksize_B;
   if (src_row_pitch < row_bytes)
      return E_INVALIDARG;

   uint8_t *dst = surface.map +
                  (uint64_t)surface.y_el * surface.texture_pitch_B +
                  (uint64_t)surface.x_el * surface.blocksize_B;
   for (uint32_t y = 0; y < surface.height_el; ++y)
      memcpy(dst + (uint64_t)y * surface.texture_pitch_B,
             (const uint8_t *)src + (uint64_t)y * src_row_pitch, row_bytes);

   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_resource_ReadFromSubresource(ID3D12Resource *iface, void *dst,
                                 UINT dst_row_pitch, UINT dst_depth_pitch,
                                 UINT src_subresource,
                                 const D3D12_BOX *src_box)
{
   (void)dst_depth_pitch;

   struct d12_resource *res = d12_resource_from_iface(iface);
   struct d12_cpu_subresource surface;
   HRESULT hr = d12_resource_cpu_subresource(res, src_subresource, src_box,
                                             &surface);
   if (FAILED(hr) || surface.empty)
      return hr;
   if (!dst)
      return E_INVALIDARG;

   const uint64_t row_bytes =
      (uint64_t)surface.width_el * surface.blocksize_B;
   if (dst_row_pitch < row_bytes)
      return E_INVALIDARG;

   const uint8_t *src = surface.map +
                        (uint64_t)surface.y_el * surface.texture_pitch_B +
                        (uint64_t)surface.x_el * surface.blocksize_B;
   for (uint32_t y = 0; y < surface.height_el; ++y)
      memcpy((uint8_t *)dst + (uint64_t)y * dst_row_pitch,
             src + (uint64_t)y * surface.texture_pitch_B, row_bytes);

   return S_OK;
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
      *heap_flags = res->heap_flags;
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

static HRESULT
d12_resource_create(struct d12_device *device,
                    const D3D12_HEAP_PROPERTIES *heap_props,
                    D3D12_HEAP_FLAGS heap_flags,
                    const D3D12_RESOURCE_DESC *desc, struct d12_heap *heap,
                    uint64_t heap_offset_B, REFIID riid, void **out)
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

      const unsigned samples = MAX2(desc->SampleDesc.Count, 1);
      const unsigned full_levels =
         util_logbase2(MAX2((uint32_t)MAX2(desc->Width, 1),
                            MAX2(desc->Height, 1))) + 1;
      const unsigned levels = desc->MipLevels ? desc->MipLevels : full_levels;
      if ((samples != 1 && samples != 2 && samples != 4) || !desc->Width ||
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
         .sample_count_sa = samples,
         .levels = levels,
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
            .sample_count_sa = samples,
            .levels = levels,
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

      if (pipe == PIPE_FORMAT_NONE || !desc->Width || !desc->Height) {
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
      /*
       * TILING FOLLOWS THE FORMAT AND SHAPE, and the rule is ail's own,
       * read from its assertions in ail_make_miptree rather than guessed:
       * a LINEAR layout requires levels == 1, sample_count == 1, and a 1x1
       * block. Anything else must be twiddled.
       *
       * This matters beyond mips. The block-compressed formats added to
       * d12_format.c are 4x4-block, so creating one on the linear path
       * would trip `util_format_get_blockwidth(format) == 1` -- an assert
       * in a debug build and a mis-sized allocation in a release one.
       *
       * Single-level uncompressed 2D stays LINEAR so the CPU can still
       * read pixels by indexing rows, which is what the existing texture
       * tests rely on.
       */
      /* MipLevels == 0 means the FULL chain, the same rule
       * GetResourceAllocationInfo applies -- resolving it in only one of
       * the two places makes the reported description disagree with the
       * allocation behind it. */
      unsigned full_levels =
         util_logbase2(MAX2((uint32_t)MAX2(desc->Width, 1),
                            MAX2(desc->Height, 1))) + 1;
      unsigned levels = desc->MipLevels ? desc->MipLevels : full_levels;
      unsigned samples = MAX2(desc->SampleDesc.Count, 1);
      bool blocked = util_format_get_blockwidth(pipe) > 1 ||
                     util_format_get_blockheight(pipe) > 1;
      bool writeable_image =
         desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      /* Layered resources must remain twiddled. AGX's sampled cube and cube-
       * array descriptors have no usable linear-face addressing mode; a
       * linear six-layer allocation makes every face sample as zero. */
      bool linear_ok = levels == 1 && samples == 1 &&
                       desc->DepthOrArraySize == 1 && !blocked &&
                       !writeable_image;

      layout = (struct ail_layout){
         .width_px = (uint32_t)desc->Width,
         .height_px = desc->Height,
         .depth_px = MAX2(desc->DepthOrArraySize, 1),
         /* ail sizes a multisampled layout; the RENDER path does not
          * support MSAA yet, which is why CreateRenderTargetView still
          * refuses these. Allocating one is legal and is what the
          * placement tests need. */
         .sample_count_sa = samples,
         .levels = levels,
         .tiling = linear_ok ? AIL_TILING_LINEAR : AIL_TILING_TWIDDLED,
         .format = pipe,
         .writeable_image = writeable_image,
         /* 64-byte-aligned rows; the smoke tests mirror this constant */
         /* ail asserts this is ZERO for a non-linear layout, so it has
          * to follow the tiling decision above. */
         .linear_stride_B =
            linear_ok ? align((uint32_t)desc->Width *
                                 d12_format_bytes_per_pixel(desc->Format), 64)
                      : 0,
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

   uint64_t stencil_offset_B = 0;

   if (heap) {
      /*
       * PLACED: no allocation, a window into the heap's BO. The stencil
       * plane, when there is one, follows the depth plane inside the same
       * window -- D3D12 sizes the resource for both, which is what
       * GetResourceAllocationInfo reports.
       */
      uint64_t need = size_B;
      if (is_depth_stencil && stencil_layout.size_B) {
         stencil_offset_B = heap_offset_B + size_B;
         need += stencil_layout.size_B;
      }

      /*
       * The resource's REQUIRED alignment is part of whether it fits, not
       * just its raw size. desc->Alignment == 0 means "pick for me", which
       * for a multisampled resource is 4 MB -- so a heap sized from a
       * 64 KB-aligned query cannot hold it, and D3D12 refuses with
       * E_INVALIDARG.
       *
       * test_placed_msaa_alignment_workaround asserts exactly that, and
       * conditionally: it expects S_OK only from vkd3d-proton, which
       * deliberately allows what native rejects. We are not vkd3d-proton,
       * so the correct answer here is the refusal.
       */
      uint64_t required_align =
         desc->Alignment ? desc->Alignment
         : (desc->SampleDesc.Count > 1
               ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
               : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

      /*
       * The HEAP's alignment must be able to host the resource. A heap
       * created with Alignment == 0 is a 64 KB heap, and a resource that
       * requires 4 MB placement cannot go in one -- native D3D12 refuses
       * this even when the sizes happen to work out, which they do here
       * because the 64 KB- and 4 MB-aligned sizes of this texture
       * coincide. Checking only the size therefore accepted a placement
       * native rejects.
       */
      uint64_t heap_align = heap->desc.Alignment
                               ? heap->desc.Alignment
                               : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

      if (required_align > heap_align) {
         mesa_loge("CreatePlacedResource: resource needs %" PRIu64
                   "-byte placement, heap provides %" PRIu64,
                   required_align, heap_align);
         free(res);
         return E_INVALIDARG;
      }

      if (heap_offset_B % required_align) {
         mesa_loge("CreatePlacedResource: offset %" PRIu64 " is not %"
                   PRIu64 "-aligned", heap_offset_B, required_align);
         free(res);
         return E_INVALIDARG;
      }

      /*
       * Alignment constrains where the resource starts, not how much of the
       * heap its payload occupies.  In particular, D3D12 permits a 1 KiB
       * buffer at offset 0 in a 1 KiB heap even though the buffer's placement
       * alignment is 64 KiB.  Texture backing sizes already come from AIL;
       * rounding either kind here rejects an otherwise in-bounds resource.
       */

      if (heap_offset_B + need > heap->bo->size) {
         mesa_loge("CreatePlacedResource: %" PRIu64 " bytes at offset %"
                   PRIu64 " does not fit a %zu-byte heap",
                   need, heap_offset_B, (size_t)heap->bo->size);
         free(res);
         return E_INVALIDARG;
      }

      res->bo = heap->bo;
      res->stencil_bo = (is_depth_stencil && stencil_layout.size_B)
                           ? heap->bo : NULL;
   } else {
      /* UMA: every heap type is the same cached, coherent memory. */
      enum agx_bo_flags bo_flags = AGX_BO_WRITEBACK;
      if (!(heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED))
         bo_flags |= AGX_BO_ZERO_ON_REUSE;

      res->bo = agx_bo_create(&device->dev, size_B, 0, bo_flags,
                              "d3d12 resource");
      if (!res->bo) {
         free(res);
         return E_OUTOFMEMORY;
      }

      if (is_depth_stencil && stencil_layout.size_B) {
         res->stencil_bo =
            agx_bo_create(&device->dev, stencil_layout.size_B, 0,
                          bo_flags, "d3d12 stencil plane");
         if (!res->stencil_bo) {
            agx_bo_unreference(&device->dev, res->bo);
            free(res);
            return E_OUTOFMEMORY;
         }
      }
   }

   /* A committed resource owns its whole BO, so it sits at offset 0. The
    * resolved addresses still go through these fields rather than being
    * read from the BO downstream, so a placed resource -- which does not --
    * needs no second code path. */
   if (heap) {
      res->addr = heap->bo->va->addr + heap_offset_B;
      res->offset_B = heap_offset_B;
      res->placed = true;
      res->heap = heap;
      res->stencil_addr =
         res->stencil_bo ? heap->bo->va->addr + stencil_offset_B : 0;

      /*
       * Reference the STORAGE, not the heap object.
       *
       * The storage must outlive the heap -- D3D12 lets the application
       * release ID3D12Heap while placed resources are still alive -- but
       * the heap's PUBLIC refcount must not move: test_suballocate_small_
       * textures creates ten placed resources and then asserts
       * get_refcount(heap) == 1. Taking ID3D12Heap_AddRef here reported 11.
       *
       * agx_bo_reference is invisible to COM refcounting and does exactly
       * what is needed: the BO survives until the last placed resource
       * releases it.
       */
      agx_bo_reference(heap->bo);
   } else {
      res->addr = res->bo->va->addr;
      res->offset_B = 0;
      res->placed = false;
      res->heap = NULL;
      res->stencil_addr = res->stencil_bo ? res->stencil_bo->va->addr : 0;
   }

   res->iface.lpVtbl = (ID3D12ResourceVtbl *)&d12_resource_vtbl;
   res->refcount = 1;
   d12_private_data_store_init(&res->private_data);
   d12_notifier_init(&res->destruction_notifier, res);
   res->device = device;
   res->desc = *desc;

   /*
    * GetDesc reports the RESOLVED description, not the one passed in.
    * D3D12 fills in what the application left for the runtime to choose:
    * MipLevels == 0 means the full chain, and Alignment == 0 means the
    * runtime picked one. test_create_committed_resource reads both back
    * and checks them -- returning the caller's zeroes is wrong even
    * though it looks like faithfully echoing the request.
    */
   if (desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
      /* Report what was ASKED for when it was explicit. ail clamps
       * layout.levels to the natural chain, so echoing that turns a
       * request for 10 levels on a 32x32 texture into a reported 6 --
       * D3D12 echoes the request. Only MipLevels == 0 is ours to
       * resolve. */
      res->desc.MipLevels =
         desc->MipLevels ? desc->MipLevels
                         : (layout.levels ? layout.levels : 1);
      if (!res->desc.Alignment) {
         res->desc.Alignment =
            desc->SampleDesc.Count > 1
               ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
               : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      }
   } else if (!res->desc.Alignment) {
      res->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
   }
   res->heap_props = *heap_props;
   res->heap_flags = heap_flags;
   d12_device_add_ref(device);

   HRESULT hr = d12_resource_QueryInterface(&res->iface, riid, out);
   d12_resource_Release(&res->iface);
   return hr;
}

/*
 * D3D12 description validation, shared by the committed and placed paths.
 *
 * It lives in one function on purpose. These rules were originally written
 * only into CreateCommittedResource, and CreatePlacedResource discarded
 * initial_state and the clear value exactly as the committed path once did
 * -- the same silently-dropped-parameter shape, one call site along. A
 * second copy would drift the moment either is touched.
 */
static HRESULT
d12_validate_resource_desc(const D3D12_HEAP_PROPERTIES *heap_props,
                           const D3D12_RESOURCE_DESC *desc,
                           D3D12_RESOURCE_STATES initial_state,
                           const D3D12_CLEAR_VALUE *clear_value)
{
   if (!heap_props || !desc)
      return E_INVALIDARG;

   bool is_buffer = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;

   /*
    * BUFFER-SPECIFIC RULES.
    *
    * A buffer has exactly one mip level -- 0 is not "resolve for me" here,
    * it is invalid -- and an optimized clear value is meaningless for one,
    * so passing a non-NULL clear value is an error rather than something to
    * ignore. Both are checked directly by the suite.
    */
   if (is_buffer) {
      if (desc->MipLevels != 1)
         return E_INVALIDARG;

      if (clear_value)
         return E_INVALIDARG;

      /* D3D12 oddly permits ALLOW_RENDER_TARGET on a buffer description,
       * but a buffer can never be a depth/stencil resource.
       */
      if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
         return E_INVALIDARG;
   }

   /*
    * GPU_UPLOAD is a capability we do not advertise (CheckFeatureSupport
    * reports it unsupported), so nothing may be created on such a heap --
    * buffers included. Refusing only textures left buffers accepted on a
    * heap type this driver does not implement.
    */
   if (heap_props->Type == D3D12_HEAP_TYPE_GPU_UPLOAD)
      return E_INVALIDARG;

   /*
    * FLAG / HEAP-TYPE COMBINATIONS. An UPLOAD or READBACK heap is CPU-side
    * staging: a resource there cannot also be a render target or a UAV.
    * ALLOW_SIMULTANEOUS_ACCESS is not meaningful for a buffer at all, on
    * any heap.
    */
   bool staging_heap = heap_props->Type == D3D12_HEAP_TYPE_UPLOAD ||
                       heap_props->Type == D3D12_HEAP_TYPE_READBACK;

   if (staging_heap &&
       (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)))
      return E_INVALIDARG;

   if (is_buffer && (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS))
      return E_INVALIDARG;

   /*
    * A READBACK heap is a destination: the GPU writes, the CPU reads. Only
    * COPY_DEST (and COMMON, which newer runtimes accept) make sense, so a
    * read state on one is an error. UPLOAD is the mirror image and is
    * permissive about read states, which is why it is not checked here.
    */
   if (heap_props->Type == D3D12_HEAP_TYPE_READBACK &&
       initial_state != D3D12_RESOURCE_STATE_COMMON &&
       initial_state != D3D12_RESOURCE_STATE_COPY_DEST)
      return E_INVALIDARG;

   /*
    * A block-compressed format needs a 2D grid of blocks, so D3D12 rejects
    * it on a 1D texture. This is an INVALID description, not a dimension we
    * merely have not implemented -- answering E_NOTIMPL would tell the
    * caller to try again on better hardware when the request is illegal
    * everywhere.
    */
   /*
    * ROW_MAJOR TEXTURES. D3D12 permits a linear-laid-out texture only in a
    * narrow corner -- one mip, one layer, 2D only -- because it exists for
    * cross-adapter sharing rather than for general use. desc->Layout was
    * being ignored entirely, which is the same dropped-parameter shape as
    * initial_state and clear_value.
    */
   if (!is_buffer && desc->Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR) {
      if (desc->MipLevels != 1 || desc->DepthOrArraySize != 1 ||
          desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
          !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER) ||
          heap_props->Type != D3D12_HEAP_TYPE_CUSTOM ||
          heap_props->CPUPageProperty !=
             D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
         return E_INVALIDARG;
   }

   if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
      enum pipe_format pf = d12_format_to_pipe(desc->Format);
      if (pf != PIPE_FORMAT_NONE && (util_format_get_blockwidth(pf) > 1 ||
                                     util_format_get_blockheight(pf) > 1))
         return E_INVALIDARG;
   }

   /*
    * D3D12 VALIDATION. These are refusals the runtime owes the caller, and
    * accepting them is not harmless leniency -- an application that gets
    * S_OK for an illegal description carries on and fails somewhere less
    * obvious. Each rule below is one the suite checks directly.
    */

   /* A sample count of zero is meaningless; 1 is the minimum. */
   if (desc->SampleDesc.Count == 0)
      return E_INVALIDARG;

   bool is_texture = !is_buffer;

   /*
    * Write states are exclusive: RENDER_TARGET, DEPTH_WRITE, UNORDERED_ACCESS,
    * COPY_DEST and STREAM_OUT cannot be combined with anything else.
    */
   const D3D12_RESOURCE_STATES write_states =
      D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_DEPTH_WRITE |
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_COPY_DEST |
      D3D12_RESOURCE_STATE_STREAM_OUT;

   D3D12_RESOURCE_STATES write_bits = initial_state & write_states;
   if (write_bits && (initial_state & ~write_bits))
      return E_INVALIDARG;
   if (write_bits & (write_bits - 1))
      return E_INVALIDARG;

   /* A state the resource was not created to support is illegal. */
   if ((initial_state & D3D12_RESOURCE_STATE_RENDER_TARGET) &&
       !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
      return E_INVALIDARG;

   if ((initial_state & (D3D12_RESOURCE_STATE_DEPTH_WRITE |
                         D3D12_RESOURCE_STATE_DEPTH_READ)) &&
       !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
      return E_INVALIDARG;

   if ((initial_state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
       !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
      return E_INVALIDARG;

   /*
    * Heap type restricts what may live there. UPLOAD and READBACK are
    * buffer-only, and GPU_UPLOAD is a capability this driver does not
    * advertise, so a resource on one is refused rather than silently
    * placed somewhere else.
    */
   /*
    * An explicitly requested alignment must be one the resource can
    * actually use. 4 KB small placement is only legal when a single slice
    * of level 0 fits in 64 KB -- the same rule GetResourceAllocationInfo
    * applies, and it has to agree: a description the query refuses cannot
    * be one creation accepts.
    */
   if (is_texture &&
       desc->Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
      enum pipe_format pf = d12_format_is_depth_stencil(desc->Format)
                               ? d12_format_depth_plane(desc->Format)
                               : d12_format_to_pipe(desc->Format);

      if (pf == PIPE_FORMAT_NONE || desc->SampleDesc.Count > 1 ||
          (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                          D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
         return E_INVALIDARG;

      uint64_t slice0 =
         (uint64_t)util_format_get_nblocksx(pf, MAX2((uint32_t)desc->Width, 1)) *
         util_format_get_nblocksy(pf, MAX2(desc->Height, 1)) *
         util_format_get_blocksize(pf);

      if (slice0 > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
         return E_INVALIDARG;
   }

   if (is_texture && (heap_props->Type == D3D12_HEAP_TYPE_UPLOAD ||
                      heap_props->Type == D3D12_HEAP_TYPE_READBACK))
      return E_INVALIDARG;

   return S_OK;
}

HRESULT
d12_resource_create_committed(struct d12_device *device,
                              const D3D12_HEAP_PROPERTIES *heap_props,
                              D3D12_HEAP_FLAGS heap_flags,
                              const D3D12_RESOURCE_DESC *desc,
                              D3D12_RESOURCE_STATES initial_state,
                              const D3D12_CLEAR_VALUE *clear_value,
                              REFIID riid, void **out)
{
   HRESULT valid = d12_validate_resource_desc(heap_props, desc, initial_state,
                                              clear_value);
   if (FAILED(valid))
      return valid;

   return d12_resource_create(device, heap_props, heap_flags, desc, NULL, 0,
                              riid, out);
}

HRESULT
d12_resource_create_placed(struct d12_device *device, struct d12_heap *heap,
                           uint64_t offset_B,
                           const D3D12_RESOURCE_DESC *desc,
                           D3D12_RESOURCE_STATES initial_state,
                           const D3D12_CLEAR_VALUE *clear_value, REFIID riid,
                           void **out)
{
   if (!heap || !desc)
      return E_INVALIDARG;

   /* The ALLOW_ONLY_* heap flags are aliases for combinations of these
    * three DENY flags. Validate the actual resource class before allocating
    * anything: placed resources do not get to cross a heap's declared type
    * boundary just because this UMA implementation uses the same BO kind
    * underneath.
    */
   bool is_buffer = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
   bool is_rt_ds = !is_buffer &&
      (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));

   if ((is_buffer && (heap->desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS)) ||
       (is_rt_ds &&
        (heap->desc.Flags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES)) ||
       (!is_buffer && !is_rt_ds &&
        (heap->desc.Flags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES)))
      return E_INVALIDARG;

   /* The same rules the committed path enforces. A placed resource is not
    * exempt from them just because its storage comes from elsewhere. */
   HRESULT valid = d12_validate_resource_desc(&heap->desc.Properties, desc,
                                              initial_state, clear_value);
   if (FAILED(valid))
      return valid;

   return d12_resource_create(device, &heap->desc.Properties,
                              heap->desc.Flags, desc, heap, offset_B, riid,
                              out);
}


/*
 * ID3D12Heap. One agx_bo; on UMA every heap type is the same memory, so a
 * heap is an allocation and nothing more. Placed resources carve windows
 * out of it and hold a reference, because D3D12 lets the application
 * release the heap while they are still alive.
 */

static HRESULT STDMETHODCALLTYPE
d12_heap_QueryInterface(ID3D12Heap *iface, REFIID riid, void **out)
{
   struct d12_heap *heap = d12_memheap_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12Pageable) ||
       d12_guid_equal(riid, &IID_ID3D12Heap)) {
      p_atomic_inc(&heap->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12Heap::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_heap_AddRef(ID3D12Heap *iface)
{
   return p_atomic_inc_return(&d12_memheap_from_iface(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_heap_Release(ID3D12Heap *iface)
{
   struct d12_heap *heap = d12_memheap_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&heap->refcount);

   if (refs == 0) {
      agx_bo_unreference(&heap->device->dev, heap->bo);
      d12_private_data_store_destroy(&heap->private_data);
      d12_device_release(heap->device);
      free(heap);
   }

   return refs;
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_heap, ID3D12Heap,
                                d12_memheap_from_iface)

static HRESULT STDMETHODCALLTYPE
d12_heap_GetDevice(ID3D12Heap *iface, REFIID riid, void **out)
{
   struct d12_heap *heap = d12_memheap_from_iface(iface);
   return ID3D12Device_QueryInterface(&heap->device->iface, riid, out);
}

static D3D12_HEAP_DESC STDMETHODCALLTYPE
d12_heap_GetDesc(ID3D12Heap *iface)
{
   return d12_memheap_from_iface(iface)->desc;
}

static const ID3D12HeapVtbl d12_heap_vtbl = {
   .QueryInterface = d12_heap_QueryInterface,
   .AddRef = d12_heap_AddRef,
   .Release = d12_heap_Release,
   .GetPrivateData = d12_heap_GetPrivateData,
   .SetPrivateData = d12_heap_SetPrivateData,
   .SetPrivateDataInterface = d12_heap_SetPrivateDataInterface,
   .SetName = d12_heap_SetName,
   .GetDevice = d12_heap_GetDevice,
   .GetDesc = d12_heap_GetDesc,
};

HRESULT
d12_heap_create(struct d12_device *device, const D3D12_HEAP_DESC *desc,
                REFIID riid, void **out)
{
   if (!out)
      return E_POINTER;
   if (!desc || !desc->SizeInBytes)
      return E_INVALIDARG;

   /*
    * HEAP VALIDATION -- desc->Flags and desc->Properties were both being
    * ignored, the same dropped-parameter shape as the resource path.
    *
    * Cross-adapter memory is shared with another device, so it cannot also
    * be CPU-visible: a CUSTOM heap carrying SHARED_CROSS_ADAPTER must have
    * CPUPageProperty NOT_AVAILABLE. test_map_texture_validation checks both
    * halves -- WRITE_BACK is refused, NOT_AVAILABLE is accepted.
    */
   if (desc->Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) {
      if (desc->Properties.Type == D3D12_HEAP_TYPE_CUSTOM) {
         if (desc->Properties.CPUPageProperty !=
             D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
            return E_INVALIDARG;
      } else if (desc->Properties.Type != D3D12_HEAP_TYPE_DEFAULT) {
         return E_INVALIDARG;
      }
   }

   /* GPU_UPLOAD is a capability we do not advertise, so no heap of that
    * type either -- matching the resource path. */
   if (desc->Properties.Type == D3D12_HEAP_TYPE_GPU_UPLOAD)
      return E_INVALIDARG;

   D3D12_HEAP_DESC resolved = *desc;

   /* A zero node mask selects node 0 on this single-node device. Store the
    * canonical spelling returned by the native runtime rather than echoing
    * the caller's shorthand from GetDesc().
    */
   if (!resolved.Properties.CreationNodeMask)
      resolved.Properties.CreationNodeMask = 1;
   if (!resolved.Properties.VisibleNodeMask)
      resolved.Properties.VisibleNodeMask = 1;

   if (!resolved.Alignment)
      resolved.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

   if (resolved.Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT &&
       resolved.Alignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT)
      return E_INVALIDARG;

   /* ALLOW_DISPLAY heaps have platform/display ownership requirements that
    * this driver does not implement. Refuse the capability instead of
    * returning an ordinary private BO under a display-capable descriptor.
    */
   if (resolved.Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY)
      return E_INVALIDARG;

   struct d12_heap *heap = calloc(1, sizeof(*heap));
   if (!heap)
      return E_OUTOFMEMORY;

   enum agx_bo_flags bo_flags = AGX_BO_WRITEBACK;
   if (!(resolved.Flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED))
      bo_flags |= AGX_BO_ZERO_ON_REUSE;

   heap->bo = agx_bo_create(&device->dev, resolved.SizeInBytes,
                            resolved.Alignment, bo_flags, "d3d12 heap");
   if (!heap->bo) {
      free(heap);
      return E_OUTOFMEMORY;
   }

   heap->iface.lpVtbl = (ID3D12HeapVtbl *)&d12_heap_vtbl;
   heap->refcount = 1;
   heap->device = device;
   heap->desc = resolved;
   d12_private_data_store_init(&heap->private_data);
   d12_device_add_ref(device);

   HRESULT hr = d12_heap_QueryInterface(&heap->iface, riid, out);
   d12_heap_Release(&heap->iface);
   return hr;
}
