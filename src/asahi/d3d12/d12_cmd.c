/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * ID3D12CommandAllocator and ID3D12GraphicsCommandList.
 *
 * A command list IS a CDM control stream, encoded at record time the same
 * way Honeykrisp encodes its internal dispatches (hk_dispatch_precomp):
 * per dispatch, upload the kernel args, build USC words with
 * agx_usc_words_precomp, emit agx_cdm_launch + agx_cdm_barrier; Close()
 * emits agx_cdm_terminate. CopyBufferRegion/CopyResource run the
 * precompiled libagx byte-copy kernel — real GPU work, no CPU memcpy lies.
 */

#include "d12_private.h"

#include "agx_usc.h"
#include "libagx_dgc.h"
#include "libagx_shaders.h"

#define D12_STREAM_BO_SIZE (1024 * 1024)
#define D12_USC_BO_SIZE    (256 * 1024)
/* Worst-case control words per dispatch (launch + barrier), with slack. */
#define D12_DISPATCH_STREAM_MARGIN 0x300

/*
 * ID3D12CommandAllocator. The real allocations live on the list for now;
 * the allocator is lifetime bookkeeping to honor the API shape.
 */

static struct d12_cmd_allocator *
d12_cmd_allocator_from_iface(ID3D12CommandAllocator *iface)
{
   return (struct d12_cmd_allocator *)iface;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_QueryInterface(ID3D12CommandAllocator *iface, REFIID riid,
                         void **out)
{
   struct d12_cmd_allocator *alloc = d12_cmd_allocator_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12Pageable) ||
       d12_guid_equal(riid, &IID_ID3D12CommandAllocator)) {
      p_atomic_inc(&alloc->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12CommandAllocator::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_alloc_AddRef(ID3D12CommandAllocator *iface)
{
   struct d12_cmd_allocator *alloc = d12_cmd_allocator_from_iface(iface);
   return p_atomic_inc_return(&alloc->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_alloc_Release(ID3D12CommandAllocator *iface)
{
   struct d12_cmd_allocator *alloc = d12_cmd_allocator_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&alloc->refcount);

   if (refs == 0) {
      d12_device_release(alloc->device);
      free(alloc);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_GetPrivateData(ID3D12CommandAllocator *iface, REFGUID guid,
                         UINT *size, void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_SetPrivateData(ID3D12CommandAllocator *iface, REFGUID guid,
                         UINT size, const void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_SetPrivateDataInterface(ID3D12CommandAllocator *iface, REFGUID guid,
                                  const IUnknown *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_SetName(ID3D12CommandAllocator *iface, LPCWSTR name)
{
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_GetDevice(ID3D12CommandAllocator *iface, REFIID riid, void **out)
{
   struct d12_cmd_allocator *alloc = d12_cmd_allocator_from_iface(iface);
   return ID3D12Device_QueryInterface(&alloc->device->iface, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_Reset(ID3D12CommandAllocator *iface)
{
   return S_OK;
}

static const ID3D12CommandAllocatorVtbl d12_alloc_vtbl = {
   .QueryInterface = d12_alloc_QueryInterface,
   .AddRef = d12_alloc_AddRef,
   .Release = d12_alloc_Release,
   .GetPrivateData = d12_alloc_GetPrivateData,
   .SetPrivateData = d12_alloc_SetPrivateData,
   .SetPrivateDataInterface = d12_alloc_SetPrivateDataInterface,
   .SetName = d12_alloc_SetName,
   .GetDevice = d12_alloc_GetDevice,
   .Reset = d12_alloc_Reset,
};

HRESULT
d12_cmd_allocator_create(struct d12_device *device,
                         D3D12_COMMAND_LIST_TYPE type, REFIID riid,
                         void **out)
{
   if (!out)
      return E_POINTER;

   switch (type) {
   case D3D12_COMMAND_LIST_TYPE_DIRECT:
   case D3D12_COMMAND_LIST_TYPE_COMPUTE:
   case D3D12_COMMAND_LIST_TYPE_COPY:
      break;
   default:
      mesa_loge("CreateCommandAllocator: type %d not supported", (int)type);
      return E_NOTIMPL;
   }

   struct d12_cmd_allocator *alloc = calloc(1, sizeof(*alloc));
   if (!alloc)
      return E_OUTOFMEMORY;

   alloc->iface.lpVtbl = (ID3D12CommandAllocatorVtbl *)&d12_alloc_vtbl;
   alloc->refcount = 1;
   alloc->device = device;
   alloc->type = type;
   d12_device_add_ref(device);

   HRESULT hr = d12_alloc_QueryInterface(&alloc->iface, riid, out);
   d12_alloc_Release(&alloc->iface);
   return hr;
}

/*
 * ID3D12GraphicsCommandList
 */

static uint8_t *
d12_list_stream_map(struct d12_cmd_list *list)
{
   return agx_bo_map(list->stream_bo);
}

static void
d12_list_begin_recording(struct d12_cmd_list *list)
{
   list->stream_cur = d12_list_stream_map(list);
   list->data_floor = D12_STREAM_BO_SIZE;
   list->usc_offset = 0;
   list->dispatches = 0;
   list->state = D12_LIST_RECORDING;
}

/* Upload an arg blob into the top of the stream BO; returns its GPU VA. */
static uint64_t
d12_list_upload(struct d12_cmd_list *list, const void *data, size_t size,
                size_t align)
{
   size_t floor = (list->data_floor - size) & ~(align - 1);
   size_t used = list->stream_cur - d12_list_stream_map(list);

   if (floor <= used + D12_DISPATCH_STREAM_MARGIN) {
      mesa_loge("command list out of stream/data space");
      list->state = D12_LIST_ERROR;
      return 0;
   }

   memcpy(d12_list_stream_map(list) + floor, data, size);
   list->data_floor = floor;
   return list->stream_bo->va->addr + floor;
}

/*
 * The G2 execution engine: dispatch a precompiled libagx kernel, exactly
 * as hk_dispatch_precomp does.
 */
static void
d12_list_dispatch_precomp(struct d12_cmd_list *list, enum libagx_program idx,
                          const void *args, size_t args_size,
                          struct agx_grid grid)
{
   struct d12_device *device = list->device;

   if (list->state != D12_LIST_RECORDING) {
      mesa_loge("recording into a non-recording command list");
      return;
   }

   struct agx_precompiled_shader *prog =
      agx_get_precompiled(&device->bg_eot, idx);
   if (!prog) {
      mesa_loge("failed to get precompiled program %d", (int)idx);
      list->state = D12_LIST_ERROR;
      return;
   }

   uint64_t data_gpu = d12_list_upload(list, args, args_size, 4);
   if (!data_gpu)
      return;

   /* USC words in the LOW_VA BO */
   size_t usc_size = agx_usc_size(15);
   size_t usc_off = ALIGN_POT(list->usc_offset, 64);
   if (usc_off + usc_size > D12_USC_BO_SIZE) {
      mesa_loge("command list out of USC space");
      list->state = D12_LIST_ERROR;
      return;
   }
   list->usc_offset = usc_off + usc_size;

   uint8_t *usc_cpu = (uint8_t *)agx_bo_map(list->usc_bo) + usc_off;
   uint64_t usc_gpu = list->usc_bo->va->addr + usc_off;

   agx_usc_words_precomp((uint32_t *)usc_cpu, &prog->b, data_gpu, args_size);

   /* Stream-space check, then launch + cache-flush barrier */
   size_t used = list->stream_cur - d12_list_stream_map(list);
   if (used + D12_DISPATCH_STREAM_MARGIN > list->data_floor) {
      mesa_loge("command list out of control-stream space");
      list->state = D12_LIST_ERROR;
      return;
   }

   list->stream_cur = (uint8_t *)agx_cdm_launch(
      (uint32_t *)list->stream_cur, device->dev.chip, grid, prog->b.workgroup,
      prog->b.launch, agx_usc_addr(&device->dev, usc_gpu));
   list->stream_cur =
      (uint8_t *)agx_cdm_barrier((uint32_t *)list->stream_cur,
                                 device->dev.chip);
   list->dispatches++;
}

static HRESULT STDMETHODCALLTYPE
d12_list_QueryInterface(ID3D12GraphicsCommandList *iface, REFIID riid,
                        void **out)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12CommandList) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList)) {
      p_atomic_inc(&list->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12GraphicsCommandList::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_list_AddRef(ID3D12GraphicsCommandList *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return p_atomic_inc_return(&list->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_list_Release(ID3D12GraphicsCommandList *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&list->refcount);

   if (refs == 0) {
      agx_bo_unreference(&list->device->dev, list->stream_bo);
      agx_bo_unreference(&list->device->dev, list->usc_bo);
      d12_device_release(list->device);
      free(list);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_list_GetPrivateData(ID3D12GraphicsCommandList *iface, REFGUID guid,
                        UINT *size, void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_list_SetPrivateData(ID3D12GraphicsCommandList *iface, REFGUID guid,
                        UINT size, const void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_list_SetPrivateDataInterface(ID3D12GraphicsCommandList *iface,
                                 REFGUID guid, const IUnknown *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_list_SetName(ID3D12GraphicsCommandList *iface, LPCWSTR name)
{
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_list_GetDevice(ID3D12GraphicsCommandList *iface, REFIID riid, void **out)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return ID3D12Device_QueryInterface(&list->device->iface, riid, out);
}

static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE
d12_list_GetType(ID3D12GraphicsCommandList *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return list->type;
}

static HRESULT STDMETHODCALLTYPE
d12_list_Close(ID3D12GraphicsCommandList *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state == D12_LIST_ERROR) {
      mesa_loge("Close on errored command list");
      return E_FAIL;
   }
   if (list->state != D12_LIST_RECORDING)
      return E_FAIL;

   list->stream_cur =
      (uint8_t *)agx_cdm_terminate((uint32_t *)list->stream_cur);
   list->state = D12_LIST_CLOSED;
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_list_Reset(ID3D12GraphicsCommandList *iface,
               ID3D12CommandAllocator *allocator,
               ID3D12PipelineState *initial_state)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!allocator)
      return E_INVALIDARG;
   if (initial_state)
      mesa_logw("Reset: initial pipeline state ignored (no PSOs until G4)");

   d12_list_begin_recording(list);
   return S_OK;
}

static void STDMETHODCALLTYPE
d12_list_CopyBufferRegion(ID3D12GraphicsCommandList *iface,
                          ID3D12Resource *dst, UINT64 dst_offset,
                          ID3D12Resource *src, UINT64 src_offset,
                          UINT64 num_bytes)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   struct d12_resource *dstr = d12_resource_from_iface(dst);
   struct d12_resource *srcr = d12_resource_from_iface(src);

   if (!dst || !src || !num_bytes) {
      mesa_loge("CopyBufferRegion: bad args");
      if (list)
         list->state = D12_LIST_ERROR;
      return;
   }

   if (dst_offset + num_bytes > dstr->desc.Width ||
       src_offset + num_bytes > srcr->desc.Width) {
      mesa_loge("CopyBufferRegion: out of bounds");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct libagx_copy_uchar_args args = {
      .dest = dstr->bo->va->addr + dst_offset,
      .src = srcr->bo->va->addr + src_offset,
   };

   d12_list_dispatch_precomp(list, LIBAGX_COPY_UCHAR, &args, sizeof(args),
                             agx_1d(num_bytes));
}

static void STDMETHODCALLTYPE
d12_list_CopyResource(ID3D12GraphicsCommandList *iface, ID3D12Resource *dst,
                      ID3D12Resource *src)
{
   struct d12_resource *dstr = d12_resource_from_iface(dst);
   struct d12_resource *srcr = d12_resource_from_iface(src);

   if (!dst || !src)
      return;

   if (dstr->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
       dstr->desc.Width != srcr->desc.Width) {
      mesa_loge("CopyResource: only equal-size buffers until G3");
      d12_cmd_list_from_iface(iface)->state = D12_LIST_ERROR;
      return;
   }

   d12_list_CopyBufferRegion(iface, dst, 0, src, 0, dstr->desc.Width);
}

static void STDMETHODCALLTYPE
d12_list_ResourceBarrier(ID3D12GraphicsCommandList *iface, UINT num_barriers,
                         const D3D12_RESOURCE_BARRIER *barriers)
{
   /* Every dispatch already ends in agx_cdm_barrier (cache flush), memory
    * is coherent UMA, and cross-list ordering is queue-sequential. State
    * transitions are accepted and recorded as no-ops until G3 needs image
    * layout work.
    */
   for (UINT i = 0; i < num_barriers; i++) {
      if (barriers[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
          barriers[i].Type != D3D12_RESOURCE_BARRIER_TYPE_UAV) {
         mesa_logw("ResourceBarrier: type %d ignored",
                   (int)barriers[i].Type);
      }
   }
}

static const ID3D12GraphicsCommandListVtbl d12_list_vtbl = {
   .QueryInterface = d12_list_QueryInterface,
   .AddRef = d12_list_AddRef,
   .Release = d12_list_Release,
   .GetPrivateData = d12_list_GetPrivateData,
   .SetPrivateData = d12_list_SetPrivateData,
   .SetPrivateDataInterface = d12_list_SetPrivateDataInterface,
   .SetName = d12_list_SetName,
   .GetDevice = d12_list_GetDevice,
   .GetType = d12_list_GetType,
   .Close = d12_list_Close,
   .Reset = d12_list_Reset,
   .CopyBufferRegion = d12_list_CopyBufferRegion,
   .CopyResource = d12_list_CopyResource,
   .ResourceBarrier = d12_list_ResourceBarrier,
   /* Everything unset is NULL: loud crash over silent lie. */
};

HRESULT
d12_cmd_list_create(struct d12_device *device, D3D12_COMMAND_LIST_TYPE type,
                    REFIID riid, void **out)
{
   if (!out)
      return E_POINTER;

   struct d12_cmd_list *list = calloc(1, sizeof(*list));
   if (!list)
      return E_OUTOFMEMORY;

   list->stream_bo = agx_bo_create(&device->dev, D12_STREAM_BO_SIZE, 0,
                                   AGX_BO_WRITEBACK, "d3d12 cmdlist stream");
   list->usc_bo = agx_bo_create(&device->dev, D12_USC_BO_SIZE, 0,
                                AGX_BO_LOW_VA | AGX_BO_WRITEBACK,
                                "d3d12 cmdlist usc");
   if (!list->stream_bo || !list->usc_bo) {
      if (list->stream_bo)
         agx_bo_unreference(&device->dev, list->stream_bo);
      if (list->usc_bo)
         agx_bo_unreference(&device->dev, list->usc_bo);
      free(list);
      return E_OUTOFMEMORY;
   }

   list->iface.lpVtbl = (ID3D12GraphicsCommandListVtbl *)&d12_list_vtbl;
   list->refcount = 1;
   list->device = device;
   list->type = type;
   d12_device_add_ref(device);

   /* D3D12 command lists are created in the recording state. */
   d12_list_begin_recording(list);

   HRESULT hr = d12_list_QueryInterface(&list->iface, riid, out);
   d12_list_Release(&list->iface);
   return hr;
}
