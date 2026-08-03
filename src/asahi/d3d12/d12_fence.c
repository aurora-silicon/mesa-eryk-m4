/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * ID3D12Fence as a DRM timeline syncobj. The mapping is exact: the fence's
 * completed value is the syncobj's highest signaled point, CPU Signal() is a
 * timeline signal, and queue-side Signal()/Wait() are syncobj transfers
 * (see d12_queue.c, mirroring hk_queue.c's timeline scheme).
 */

#include "d12_private.h"

#include <xf86drm.h>

static HRESULT STDMETHODCALLTYPE
d12_fence_QueryInterface(ID3D12Fence *iface, REFIID riid, void **out)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12Pageable) ||
       d12_guid_equal(riid, &IID_ID3D12Fence)) {
      p_atomic_inc(&fence->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12Fence::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_fence_AddRef(ID3D12Fence *iface)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);
   return p_atomic_inc_return(&fence->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_fence_Release(ID3D12Fence *iface)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&fence->refcount);

   if (refs == 0) {
      drmSyncobjDestroy(fence->device->dev.fd, fence->syncobj);
      d12_device_release(fence->device);
      free(fence);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_GetPrivateData(ID3D12Fence *iface, REFGUID guid, UINT *size,
                         void *data)
{
   mesa_logw("ID3D12Fence::GetPrivateData: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_SetPrivateData(ID3D12Fence *iface, REFGUID guid, UINT size,
                         const void *data)
{
   mesa_logw("ID3D12Fence::SetPrivateData: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_SetPrivateDataInterface(ID3D12Fence *iface, REFGUID guid,
                                  const IUnknown *data)
{
   mesa_logw("ID3D12Fence::SetPrivateDataInterface: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_SetName(ID3D12Fence *iface, LPCWSTR name)
{
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_GetDevice(ID3D12Fence *iface, REFIID riid, void **out)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);
   return ID3D12Device_QueryInterface(&fence->device->iface, riid, out);
}

static UINT64 STDMETHODCALLTYPE
d12_fence_GetCompletedValue(ID3D12Fence *iface)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);
   uint64_t point = 0;

   if (drmSyncobjQuery(fence->device->dev.fd, &fence->syncobj, &point, 1)) {
      mesa_loge("drmSyncobjQuery failed");
      return UINT64_MAX; /* the documented device-removed sentinel */
   }

   return point;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_SetEventOnCompletion(ID3D12Fence *iface, UINT64 value,
                               HANDLE event)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);

   if (event != NULL) {
      /* Event objects need a real eventing layer (campaign OQ); native
       * tests use the documented NULL-event blocking form.
       */
      mesa_loge("ID3D12Fence::SetEventOnCompletion: non-NULL event not "
                "implemented");
      return E_NOTIMPL;
   }

   /* NULL event: block until the fence reaches the value. */
   uint64_t point = value;
   int ret = drmSyncobjTimelineWait(
      fence->device->dev.fd, &fence->syncobj, &point, 1, INT64_MAX,
      DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
   if (ret) {
      mesa_loge("drmSyncobjTimelineWait failed: %d", ret);
      return E_FAIL;
   }

   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_Signal(ID3D12Fence *iface, UINT64 value)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);
   uint64_t point = value;

   if (drmSyncobjTimelineSignal(fence->device->dev.fd, &fence->syncobj,
                                &point, 1)) {
      mesa_loge("drmSyncobjTimelineSignal failed");
      return E_FAIL;
   }

   return S_OK;
}

static const ID3D12FenceVtbl d12_fence_vtbl = {
   .QueryInterface = d12_fence_QueryInterface,
   .AddRef = d12_fence_AddRef,
   .Release = d12_fence_Release,
   .GetPrivateData = d12_fence_GetPrivateData,
   .SetPrivateData = d12_fence_SetPrivateData,
   .SetPrivateDataInterface = d12_fence_SetPrivateDataInterface,
   .SetName = d12_fence_SetName,
   .GetDevice = d12_fence_GetDevice,
   .GetCompletedValue = d12_fence_GetCompletedValue,
   .SetEventOnCompletion = d12_fence_SetEventOnCompletion,
   .Signal = d12_fence_Signal,
};

HRESULT
d12_fence_create(struct d12_device *device, UINT64 initial_value,
                 D3D12_FENCE_FLAGS flags, REFIID riid, void **out)
{
   if (!out)
      return E_POINTER;

   if (flags != D3D12_FENCE_FLAG_NONE) {
      mesa_loge("d12_fence_create: fence flags 0x%x not implemented",
                (unsigned)flags);
      return E_NOTIMPL;
   }

   struct d12_fence *fence = calloc(1, sizeof(*fence));
   if (!fence)
      return E_OUTOFMEMORY;

   if (drmSyncobjCreate(device->dev.fd, 0, &fence->syncobj)) {
      mesa_loge("drmSyncobjCreate failed");
      free(fence);
      return E_FAIL;
   }

   /* Materialize the initial value as an already-signaled point. */
   uint64_t point = initial_value;
   if (point &&
       drmSyncobjTimelineSignal(device->dev.fd, &fence->syncobj, &point, 1)) {
      mesa_loge("initial drmSyncobjTimelineSignal failed");
      drmSyncobjDestroy(device->dev.fd, fence->syncobj);
      free(fence);
      return E_FAIL;
   }

   fence->iface.lpVtbl = (ID3D12FenceVtbl *)&d12_fence_vtbl;
   fence->refcount = 1;
   fence->device = device;
   d12_device_add_ref(device);

   HRESULT hr = d12_fence_QueryInterface(&fence->iface, riid, out);
   d12_fence_Release(&fence->iface);
   return hr;
}
