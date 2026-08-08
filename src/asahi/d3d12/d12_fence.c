/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12Fence as a DRM timeline syncobj. The mapping is exact: the fence's
 * completed value is the syncobj's highest signaled point, CPU Signal() is a
 * timeline signal, and queue-side Signal()/Wait() are syncobj transfers
 * (see d12_queue.c, mirroring hk_queue.c's timeline scheme).
 */

#include "d12_private.h"

#include <xf86drm.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include "util/u_thread.h"

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
      d12_private_data_store_destroy(&fence->private_data);
      d12_device_release(fence->device);
      free(fence);
   }

   return refs;
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_fence, ID3D12Fence,
                                d12_fence_from_iface)

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

   if (getenv("D3D12AGX_DUMP_SYNC"))
      mesa_logi("SYNC GetCompletedValue: fence %p -> %llu", (void *)fence,
                (unsigned long long)point);

   return point;
}

struct d12_fence_waiter {
   struct d12_fence *fence;
   uint64_t value;
   int fd;
   thrd_t thread;
};

static ULONG STDMETHODCALLTYPE d12_fence_AddRef(ID3D12Fence *iface);
static ULONG STDMETHODCALLTYPE d12_fence_Release(ID3D12Fence *iface);

static int
d12_fence_wait_thread(void *data)
{
   struct d12_fence_waiter *w = data;
   uint64_t point = w->value;

   if (getenv("D3D12AGX_DUMP_SYNC"))
      mesa_logi("SYNC event wait ENTER: fence %p value %llu", (void *)w->fence,
                (unsigned long long)point);

   int ret = drmSyncobjTimelineWait(
      w->fence->device->dev.fd, &w->fence->syncobj, &point, 1, INT64_MAX,
      DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);

   if (getenv("D3D12AGX_DUMP_SYNC")) {
      uint64_t now = 0;
      drmSyncobjQuery(w->fence->device->dev.fd, &w->fence->syncobj, &now, 1);
      mesa_logi("SYNC event wait EXIT:  fence %p value %llu ret %d "
                "(syncobj now at %llu)",
                (void *)w->fence, (unsigned long long)point, ret,
                (unsigned long long)now);
   }

   if (ret)
      mesa_loge("fence event wait failed: %d", ret);

   /* Signal even on failure: a waiter that is never woken hangs the
    * application, which is strictly worse than waking it early. */
   const uint64_t one = 1;
   if (write(w->fd, &one, sizeof(one)) != sizeof(one))
      mesa_loge("fence event write failed");

   d12_fence_Release(&w->fence->iface);
   free(w);
   return 0;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_SetEventOnCompletion(ID3D12Fence *iface, UINT64 value,
                               HANDLE event)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);

   if (event != NULL) {
      /*
       * D3D12 says this returns IMMEDIATELY and signals the event later.
       * A blocking implementation deadlocks the perfectly legal pattern
       * "arm the event, then submit the work that signals it", so the
       * wait happens on its own thread.
       *
       * On this platform the HANDLE is an eventfd -- that is what
       * CreateEventA maps to here, and what the waiter reads. Signalling
       * is a single 8-byte write, the same one signal_event() does.
       *
       * Until this existed, every test that waited on a real event got
       * E_NOTIMPL and then behaved as though the GPU had finished, which
       * is why so many of them died downstream rather than here.
       */
      struct d12_fence_waiter *w = malloc(sizeof(*w));
      if (!w)
         return E_OUTOFMEMORY;

      w->fence = fence;
      w->value = value;
      w->fd = (int)(intptr_t)event;

      /* The thread outlives this call, so it holds its own reference. */
      d12_fence_AddRef(iface);

      if (thrd_create(&w->thread, d12_fence_wait_thread, w) != thrd_success) {
         d12_fence_Release(iface);
         free(w);
         return E_FAIL;
      }

      thrd_detach(w->thread);
      return S_OK;
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

struct d12_fence_multi_waiter {
   struct d12_device *device;
   struct d12_fence **fences;
   uint32_t *syncobjs;
   uint64_t *values;
   unsigned count;
   bool wait_all;
   int fd;
   thrd_t thread;
};

static int
d12_fence_multi_wait(struct d12_fence_multi_waiter *w)
{
   unsigned flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
   if (w->wait_all)
      flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;

   return drmSyncobjTimelineWait(w->device->dev.fd, w->syncobjs, w->values,
                                 w->count, INT64_MAX, flags, NULL);
}

static void
d12_fence_multi_waiter_destroy(struct d12_fence_multi_waiter *w)
{
   for (unsigned i = 0; i < w->count; i++)
      d12_fence_Release(&w->fences[i]->iface);

   free(w->values);
   free(w->syncobjs);
   free(w->fences);
   free(w);
}

static int
d12_fence_multi_wait_thread(void *data)
{
   struct d12_fence_multi_waiter *w = data;
   int ret = d12_fence_multi_wait(w);

   if (ret)
      mesa_loge("multiple fence event wait failed: %d", ret);

   /* As with the single-fence waiter, wake on a kernel error rather than
    * leaving the application permanently blocked on an event which can no
    * longer become signaled.
    */
   const uint64_t one = 1;
   if (write(w->fd, &one, sizeof(one)) != sizeof(one))
      mesa_loge("multiple fence event write failed");

   d12_fence_multi_waiter_destroy(w);
   return 0;
}

HRESULT
d12_fence_set_event_multiple(struct d12_device *device,
                             ID3D12Fence *const *fences,
                             const UINT64 *values, UINT count,
                             D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags,
                             HANDLE event)
{
   if (!fences || !values || count == 0 ||
       (flags != D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL &&
        flags != D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY))
      return E_INVALIDARG;

   struct d12_fence_multi_waiter *w = calloc(1, sizeof(*w));
   if (!w)
      return E_OUTOFMEMORY;

   w->fences = calloc(count, sizeof(*w->fences));
   w->syncobjs = calloc(count, sizeof(*w->syncobjs));
   w->values = calloc(count, sizeof(*w->values));
   if (!w->fences || !w->syncobjs || !w->values) {
      free(w->values);
      free(w->syncobjs);
      free(w->fences);
      free(w);
      return E_OUTOFMEMORY;
   }

   w->device = device;
   w->count = count;
   w->wait_all = flags == D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL;
   w->fd = (int)(intptr_t)event;

   for (unsigned i = 0; i < count; i++) {
      if (!fences[i]) {
         w->count = i;
         d12_fence_multi_waiter_destroy(w);
         return E_INVALIDARG;
      }

      struct d12_fence *fence = d12_fence_from_iface(fences[i]);
      if (fence->device != device) {
         w->count = i;
         d12_fence_multi_waiter_destroy(w);
         return E_INVALIDARG;
      }

      d12_fence_AddRef(fences[i]);
      w->fences[i] = fence;
      w->syncobjs[i] = fence->syncobj;
      w->values[i] = values[i];
   }

   if (!event) {
      int ret = d12_fence_multi_wait(w);
      d12_fence_multi_waiter_destroy(w);
      if (ret) {
         mesa_loge("multiple fence wait failed: %d", ret);
         return E_FAIL;
      }
      return S_OK;
   }

   if (thrd_create(&w->thread, d12_fence_multi_wait_thread, w) !=
       thrd_success) {
      d12_fence_multi_waiter_destroy(w);
      return E_FAIL;
   }

   thrd_detach(w->thread);
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
   /* every unimplemented slot fails safely */
   D12_ID3D12Fence_DEFAULTS,
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
   /* CreateFence is the odd D3D12 creation call whose NULL output contract is
    * S_FALSE, even when riid itself is unsupported. QueryInterface retains
    * the normal E_POINTER rule; this applies only to creation.
    */
   if (!out)
      return S_FALSE;

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
   d12_private_data_store_init(&fence->private_data);
   d12_device_add_ref(device);

   HRESULT hr = d12_fence_QueryInterface(&fence->iface, riid, out);
   d12_fence_Release(&fence->iface);
   return hr;
}
