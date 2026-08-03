/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * ID3D12Device on a real agx_device, plus the D3D12CreateDevice entry point.
 * Device discovery mirrors hk_physical_device.c: enumerate DRM devices, take
 * the render node whose kernel driver is "asahi".
 */

#include "d12_private.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>

void
d12_log_unknown_riid(const char *where, REFIID riid)
{
   const GUID *g = riid;
   mesa_loge("%s: unsupported riid "
             "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             where, g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1],
             g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
             g->Data4[7]);
}

/*
 * Find the asahi render node. Returns an open fd or -1.
 */
static int
d12_open_asahi_fd(void)
{
   drmDevicePtr devices[8];
   int count = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   int result = -1;

   for (int i = 0; i < count; i++) {
      if (!(devices[i]->available_nodes & (1 << DRM_NODE_RENDER)))
         continue;

      int fd = open(devices[i]->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
      if (fd < 0)
         continue;

      drmVersionPtr version = drmGetVersion(fd);
      bool is_asahi = version && !strcmp(version->name, "asahi");
      drmFreeVersion(version);

      if (is_asahi) {
         result = fd;
         break;
      }
      close(fd);
   }

   if (count > 0)
      drmFreeDevices(devices, count);

   return result;
}

/*
 * IUnknown + ID3D12Object
 */

static HRESULT STDMETHODCALLTYPE
d12_device_QueryInterface(ID3D12Device *iface, REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12Device)) {
      d12_device_add_ref(device);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12Device::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_device_AddRef(ID3D12Device *iface)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return p_atomic_inc_return(&device->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_device_Release(ID3D12Device *iface)
{
   struct d12_device *device = d12_device_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&device->refcount);

   if (refs == 0) {
      mesa_logi("closing device %s", device->dev.name);
      agx_bo_unreference(&device->dev, device->oclqry_bo);
      agx_bg_eot_cleanup(&device->bg_eot);
      agx_close_device(&device->dev);
      free(device);
   }

   return refs;
}

void
d12_device_add_ref(struct d12_device *device)
{
   d12_device_AddRef(&device->iface);
}

void
d12_device_release(struct d12_device *device)
{
   d12_device_Release(&device->iface);
}

static HRESULT STDMETHODCALLTYPE
d12_device_GetPrivateData(ID3D12Device *iface, REFGUID guid, UINT *size,
                          void *data)
{
   mesa_logw("ID3D12Device::GetPrivateData: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_device_SetPrivateData(ID3D12Device *iface, REFGUID guid, UINT size,
                          const void *data)
{
   mesa_logw("ID3D12Device::SetPrivateData: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_device_SetPrivateDataInterface(ID3D12Device *iface, REFGUID guid,
                                   const IUnknown *data)
{
   mesa_logw("ID3D12Device::SetPrivateDataInterface: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_device_SetName(ID3D12Device *iface, LPCWSTR name)
{
   /* Accepted and ignored; nothing consumes debug names yet. */
   return S_OK;
}

/*
 * ID3D12Device
 */

static UINT STDMETHODCALLTYPE
d12_device_GetNodeCount(ID3D12Device *iface)
{
   return 1;
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommandQueue(ID3D12Device *iface,
                              const D3D12_COMMAND_QUEUE_DESC *desc,
                              REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_queue_create(device, desc, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommandAllocator(ID3D12Device *iface,
                                  D3D12_COMMAND_LIST_TYPE type, REFIID riid,
                                  void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_cmd_allocator_create(device, type, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommandList(ID3D12Device *iface, UINT node_mask,
                             D3D12_COMMAND_LIST_TYPE type,
                             ID3D12CommandAllocator *allocator,
                             ID3D12PipelineState *initial_state, REFIID riid,
                             void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (node_mask > 1)
      return E_INVALIDARG;
   if (!allocator)
      return E_INVALIDARG;
   if (initial_state)
      mesa_logw("CreateCommandList: initial pipeline state ignored (no PSOs "
                "until G4)");

   return d12_cmd_list_create(device, type, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CheckFeatureSupport(ID3D12Device *iface, D3D12_FEATURE feature,
                               void *data, UINT data_size)
{
   struct d12_device *device = d12_device_from_iface(iface);

   switch (feature) {
   case D3D12_FEATURE_D3D12_OPTIONS: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;

      memset(opts, 0, sizeof(*opts));
      /* Honest conservative shape; raised only when the backing exists. */
      opts->ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1;
      /* UMA: one memory pool, all heap types equivalent in hardware. */
      opts->ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2;
      return S_OK;
   }

   case D3D12_FEATURE_ARCHITECTURE: {
      D3D12_FEATURE_DATA_ARCHITECTURE *arch = data;
      if (data_size != sizeof(*arch))
         return E_INVALIDARG;
      if (arch->NodeIndex != 0)
         return E_INVALIDARG;

      arch->TileBasedRenderer = TRUE;
      arch->UMA = TRUE;
      arch->CacheCoherentUMA = TRUE;
      return S_OK;
   }

   case D3D12_FEATURE_ARCHITECTURE1: {
      D3D12_FEATURE_DATA_ARCHITECTURE1 *arch = data;
      if (data_size != sizeof(*arch))
         return E_INVALIDARG;
      if (arch->NodeIndex != 0)
         return E_INVALIDARG;

      arch->TileBasedRenderer = TRUE;
      arch->UMA = TRUE;
      arch->CacheCoherentUMA = TRUE;
      /* The GPU sits behind its own MMU (GPU VM / DART). */
      arch->IsolatedMMU = TRUE;
      return S_OK;
   }

   case D3D12_FEATURE_FEATURE_LEVELS: {
      D3D12_FEATURE_DATA_FEATURE_LEVELS *levels = data;
      if (data_size != sizeof(*levels))
         return E_INVALIDARG;
      if (!levels->pFeatureLevelsRequested || !levels->NumFeatureLevels)
         return E_INVALIDARG;

      D3D_FEATURE_LEVEL best = 0;
      for (UINT i = 0; i < levels->NumFeatureLevels; i++) {
         D3D_FEATURE_LEVEL fl = levels->pFeatureLevelsRequested[i];
         if (fl <= device->feature_level && fl > best)
            best = fl;
      }

      if (!best)
         return E_INVALIDARG;

      levels->MaxSupportedFeatureLevel = best;
      return S_OK;
   }

   default:
      mesa_logw("ID3D12Device::CheckFeatureSupport: feature %d not "
                "implemented",
                (int)feature);
      return E_INVALIDARG;
   }
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateComputePipelineState(
   ID3D12Device *iface, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
   REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (!desc || !desc->CS.pShaderBytecode || !desc->CS.BytecodeLength)
      return E_INVALIDARG;

   return d12_pipeline_state_create_compute(device, desc->CS.pShaderBytecode,
                                            desc->CS.BytecodeLength, riid,
                                            out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateRootSignature(ID3D12Device *iface, UINT node_mask,
                               const void *blob, SIZE_T blob_size,
                               REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (node_mask > 1)
      return E_INVALIDARG;

   return d12_root_signature_create(device, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateDescriptorHeap(ID3D12Device *iface,
                                const D3D12_DESCRIPTOR_HEAP_DESC *desc,
                                REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_descriptor_heap_create(device, desc, riid, out);
}

static void STDMETHODCALLTYPE
d12_device_CreateRenderTargetView(ID3D12Device *iface,
                                  ID3D12Resource *resource,
                                  const D3D12_RENDER_TARGET_VIEW_DESC *desc,
                                  D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_rtv *slot = (struct d12_rtv *)dest.ptr;

   if (!slot)
      return;

   if (!resource) {
      /* Null RTVs are legal placeholders; record an empty slot. */
      slot->resource = NULL;
      return;
   }

   struct d12_resource *res = d12_resource_from_iface(resource);

   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       res->desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
      mesa_loge("CreateRenderTargetView: only RGBA8 2D targets in G3");
      slot->resource = NULL;
      return;
   }

   if (desc && desc->ViewDimension != D3D12_RTV_DIMENSION_TEXTURE2D) {
      mesa_loge("CreateRenderTargetView: view dimension %d unsupported",
                (int)desc->ViewDimension);
      slot->resource = NULL;
      return;
   }

   slot->resource = res;
   slot->format = DXGI_FORMAT_R8G8B8A8_UNORM;
}

static UINT STDMETHODCALLTYPE
d12_device_GetDescriptorHandleIncrementSize(
   ID3D12Device *iface, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   /* Placeholder stride until the descriptor model lands (G3/G4); apps only
    * require a stable nonzero constant per heap type.
    */
   switch (type) {
   case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
   case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
   case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
   case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
      return 32;
   default:
      return 0;
   }
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommittedResource(ID3D12Device *iface,
                                   const D3D12_HEAP_PROPERTIES *heap_props,
                                   D3D12_HEAP_FLAGS heap_flags,
                                   const D3D12_RESOURCE_DESC *desc,
                                   D3D12_RESOURCE_STATES initial_state,
                                   const D3D12_CLEAR_VALUE *clear_value,
                                   REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_resource_create_committed(device, heap_props, desc, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateFence(ID3D12Device *iface, UINT64 initial_value,
                       D3D12_FENCE_FLAGS flags, REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_fence_create(device, initial_value, flags, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_GetDeviceRemovedReason(ID3D12Device *iface)
{
   return S_OK;
}

static LUID STDMETHODCALLTYPE
d12_device_GetAdapterLuid(ID3D12Device *iface)
{
   LUID luid = {0};
   return luid;
}

static const ID3D12DeviceVtbl d12_device_vtbl = {
   .QueryInterface = d12_device_QueryInterface,
   .AddRef = d12_device_AddRef,
   .Release = d12_device_Release,
   .GetPrivateData = d12_device_GetPrivateData,
   .SetPrivateData = d12_device_SetPrivateData,
   .SetPrivateDataInterface = d12_device_SetPrivateDataInterface,
   .SetName = d12_device_SetName,
   .GetNodeCount = d12_device_GetNodeCount,
   .CreateCommandQueue = d12_device_CreateCommandQueue,
   .CreateCommandAllocator = d12_device_CreateCommandAllocator,
   .CreateCommandList = d12_device_CreateCommandList,
   .CheckFeatureSupport = d12_device_CheckFeatureSupport,
   .CreateComputePipelineState = d12_device_CreateComputePipelineState,
   .CreateRootSignature = d12_device_CreateRootSignature,
   .CreateDescriptorHeap = d12_device_CreateDescriptorHeap,
   .CreateRenderTargetView = d12_device_CreateRenderTargetView,
   .GetDescriptorHandleIncrementSize =
      d12_device_GetDescriptorHandleIncrementSize,
   .CreateCommittedResource = d12_device_CreateCommittedResource,
   .CreateFence = d12_device_CreateFence,
   .GetDeviceRemovedReason = d12_device_GetDeviceRemovedReason,
   .GetAdapterLuid = d12_device_GetAdapterLuid,
   /* Everything unset is NULL: a call is a loud crash, not silent lies. */
};

/*
 * Entry points
 */

PUBLIC HRESULT WINAPI
D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
                  REFIID riid, void **out)
{
   /* We support up to 11_0 for now; raised as milestones land. */
   const D3D_FEATURE_LEVEL max_fl = D3D_FEATURE_LEVEL_11_0;

   if (adapter) {
      mesa_logw("D3D12CreateDevice: ignoring explicit adapter, using the "
                "asahi render node");
   }

   if (minimum_feature_level > max_fl) {
      mesa_loge("D3D12CreateDevice: feature level 0x%x not supported (max "
                "0x%x)",
                minimum_feature_level, max_fl);
      return E_INVALIDARG;
   }

   int fd = d12_open_asahi_fd();
   if (fd < 0) {
      mesa_loge("D3D12CreateDevice: no asahi render node found");
      return E_FAIL;
   }

   /* NULL out pointer = capability probe, per the D3D12 contract. */
   if (!out) {
      close(fd);
      return S_FALSE;
   }

   struct d12_device *device = calloc(1, sizeof(*device));
   if (!device) {
      close(fd);
      return E_OUTOFMEMORY;
   }

   device->dev.fd = fd;
   if (!agx_open_device(NULL, &device->dev)) {
      mesa_loge("D3D12CreateDevice: agx_open_device failed");
      close(fd);
      free(device);
      return E_FAIL;
   }

   /* Precompiled internal kernels (copy/fill engines) */
   agx_bg_eot_init(&device->bg_eot, &device->dev);
   if (!device->bg_eot.ht) {
      mesa_loge("D3D12CreateDevice: agx_bg_eot_init failed");
      agx_close_device(&device->dev);
      free(device);
      return E_FAIL;
   }

   /* Render commands need a valid occlusion-query scratch base. */
   device->oclqry_bo =
      agx_bo_create(&device->dev, 0x4000, 0, 0, "d3d12 oclqry scratch");
   if (!device->oclqry_bo) {
      agx_bg_eot_cleanup(&device->bg_eot);
      agx_close_device(&device->dev);
      free(device);
      return E_OUTOFMEMORY;
   }

   device->iface.lpVtbl = (ID3D12DeviceVtbl *)&d12_device_vtbl;
   device->refcount = 1;
   device->feature_level = max_fl;

   mesa_logi("opened %s (chip 0x%x) as D3D12 device, feature level 0x%x",
             device->dev.name, device->dev.params.chip_id,
             device->feature_level);

   HRESULT hr =
      d12_device_QueryInterface(&device->iface, riid, out);
   /* Drop the construction ref; QueryInterface holds the caller's. */
   d12_device_Release(&device->iface);
   return hr;
}

PUBLIC HRESULT WINAPI
D3D12GetDebugInterface(REFIID riid, void **out)
{
   /* No debug layer. Callers treat this as optional. */
   if (out)
      *out = NULL;
   return E_NOINTERFACE;
}
