/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * ID3D12CommandQueue on an asahi hardware queue. Queue-side fence ops use
 * the same timeline-transfer scheme as hk_queue.c: the queue owns a progress
 * timeline; Signal() forwards the current progress point into the target
 * fence's timeline, Wait() pulls the target point into the queue's timeline
 * so later work depends on it.
 *
 * G1 limitation, on purpose: there is no execution engine yet (no command
 * lists), so the progress timeline only advances through Wait() transfers,
 * and transfers from not-yet-materialized points are an error rather than a
 * deferred dependency. G2's submission engine replaces that with the
 * wait-for-submit handling hk_queue implements.
 */

#include "d12_private.h"

#include <xf86drm.h>

static HRESULT STDMETHODCALLTYPE
d12_queue_QueryInterface(ID3D12CommandQueue *iface, REFIID riid, void **out)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12Pageable) ||
       d12_guid_equal(riid, &IID_ID3D12CommandQueue)) {
      p_atomic_inc(&queue->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12CommandQueue::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_queue_AddRef(ID3D12CommandQueue *iface)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   return p_atomic_inc_return(&queue->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_queue_Release(ID3D12CommandQueue *iface)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&queue->refcount);

   if (refs == 0) {
      struct agx_device *dev = &queue->device->dev;

      drmSyncobjDestroy(dev->fd, queue->syncobj);
      agx_destroy_command_queue(dev, queue->agx_queue_id);
      simple_mtx_destroy(&queue->lock);
      d12_device_release(queue->device);
      free(queue);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_GetPrivateData(ID3D12CommandQueue *iface, REFGUID guid, UINT *size,
                         void *data)
{
   mesa_logw("ID3D12CommandQueue::GetPrivateData: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_SetPrivateData(ID3D12CommandQueue *iface, REFGUID guid, UINT size,
                         const void *data)
{
   mesa_logw("ID3D12CommandQueue::SetPrivateData: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_SetPrivateDataInterface(ID3D12CommandQueue *iface, REFGUID guid,
                                  const IUnknown *data)
{
   mesa_logw("ID3D12CommandQueue::SetPrivateDataInterface: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_SetName(ID3D12CommandQueue *iface, LPCWSTR name)
{
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_GetDevice(ID3D12CommandQueue *iface, REFIID riid, void **out)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   return ID3D12Device_QueryInterface(&queue->device->iface, riid, out);
}

static void STDMETHODCALLTYPE
d12_queue_ExecuteCommandLists(ID3D12CommandQueue *iface, UINT count,
                              ID3D12CommandList *const *lists)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   struct agx_device *dev = &queue->device->dev;

   simple_mtx_lock(&queue->lock);

   for (UINT i = 0; i < count; i++) {
      /* Our lists are always ID3D12GraphicsCommandList objects. */
      struct d12_cmd_list *list = (struct d12_cmd_list *)lists[i];

      if (!list || list->state != D12_LIST_CLOSED) {
         mesa_loge("ExecuteCommandLists: list %u not closed; dropped", i);
         continue;
      }

      if (list->dispatches == 0)
         continue;

      uint8_t *stream_map = agx_bo_map(list->stream_bo);
      size_t stream_len = list->stream_cur - stream_map;

      /* One CDM command per list. In-sync on the queue's progress timeline
       * (honoring earlier Wait() transfers), out-sync advances it — the
       * same scheme hk_queue uses, so fence Signal() transfers now
       * represent real GPU completion.
       */
      struct drm_asahi_sync syncs[2] = {
         {
            .sync_type = DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ,
            .handle = queue->syncobj,
            .timeline_value = queue->timeline_value,
         },
         {
            .sync_type = DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ,
            .handle = queue->syncobj,
            .timeline_value = queue->timeline_value + 1,
         },
      };

      struct drm_asahi_cmd_header header = agx_cmd_header(true, 0, 0);
      struct drm_asahi_cmd_compute compute = {
         .cdm_ctrl_stream_base = list->stream_bo->va->addr,
         .cdm_ctrl_stream_end = list->stream_bo->va->addr + stream_len,
      };

      uint8_t payload[sizeof(header) + sizeof(compute)];
      memcpy(payload, &header, sizeof(header));
      memcpy(payload + sizeof(header), &compute, sizeof(compute));

      struct drm_asahi_submit submit = {
         .queue_id = queue->agx_queue_id,
         .in_sync_count = 1,
         .out_sync_count = 1,
         .cmdbuf_size = sizeof(payload),
         .syncs = (uint64_t)(uintptr_t)syncs,
         .cmdbuf = (uint64_t)(uintptr_t)payload,
      };

      struct agx_submit_virt virt = {.ring_idx = 0};
      int ret = dev->ops.submit(dev, &submit, &virt);
      if (ret) {
         mesa_loge("DRM_IOCTL_ASAHI_SUBMIT failed for list %u: %d", i, ret);
      } else {
         queue->timeline_value++;
      }
   }

   simple_mtx_unlock(&queue->lock);
}

static void STDMETHODCALLTYPE
d12_queue_SetMarker(ID3D12CommandQueue *iface, UINT metadata,
                    const void *data, UINT size)
{
   /* PIX marker: harmless no-op. */
}

static void STDMETHODCALLTYPE
d12_queue_BeginEvent(ID3D12CommandQueue *iface, UINT metadata,
                     const void *data, UINT size)
{
   /* PIX event: harmless no-op. */
}

static void STDMETHODCALLTYPE
d12_queue_EndEvent(ID3D12CommandQueue *iface)
{
   /* PIX event: harmless no-op. */
}

static HRESULT STDMETHODCALLTYPE
d12_queue_Signal(ID3D12CommandQueue *iface, ID3D12Fence *fence_iface,
                 UINT64 value)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   struct d12_fence *fence = d12_fence_from_iface(fence_iface);
   struct agx_device *dev = &queue->device->dev;
   HRESULT hr = S_OK;

   simple_mtx_lock(&queue->lock);

   /* Forward current queue progress into the fence's timeline: the fence
    * point signals when everything currently on the queue completes.
    */
   int ret = drmSyncobjTransfer(dev->fd, fence->syncobj, value,
                                queue->syncobj, queue->timeline_value, 0);
   if (ret) {
      mesa_loge("queue Signal: drmSyncobjTransfer failed: %d", ret);
      hr = E_FAIL;
   }

   simple_mtx_unlock(&queue->lock);
   return hr;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_Wait(ID3D12CommandQueue *iface, ID3D12Fence *fence_iface,
               UINT64 value)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   struct d12_fence *fence = d12_fence_from_iface(fence_iface);
   struct agx_device *dev = &queue->device->dev;
   HRESULT hr = S_OK;

   simple_mtx_lock(&queue->lock);

   /* Pull the fence point into the queue's progress timeline, so all later
    * queue work (and later Signals) depend on it.
    */
   uint64_t next = queue->timeline_value + 1;
   int ret = drmSyncobjTransfer(dev->fd, queue->syncobj, next,
                                fence->syncobj, value, 0);
   if (ret) {
      mesa_loge("queue Wait: drmSyncobjTransfer failed: %d (G1 cannot wait "
                "on unmaterialized points)",
                ret);
      hr = E_FAIL;
   } else {
      queue->timeline_value = next;
   }

   simple_mtx_unlock(&queue->lock);
   return hr;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_GetTimestampFrequency(ID3D12CommandQueue *iface, UINT64 *frequency)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   struct agx_device *dev = &queue->device->dev;

   if (!frequency)
      return E_POINTER;

   /* ns = ticks * num / den  =>  ticks/second = 1e9 * den / num */
   if (!dev->user_timestamp_to_ns.num) {
      mesa_loge("GetTimestampFrequency: no timestamp calibration from "
                "kernel");
      return E_FAIL;
   }

   *frequency = (UINT64)(1000000000ull * dev->user_timestamp_to_ns.den /
                         dev->user_timestamp_to_ns.num);
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_GetClockCalibration(ID3D12CommandQueue *iface,
                              UINT64 *gpu_timestamp, UINT64 *cpu_timestamp)
{
   mesa_logw("ID3D12CommandQueue::GetClockCalibration: not implemented");
   return E_NOTIMPL;
}

static D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE
d12_queue_GetDesc(ID3D12CommandQueue *iface)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   return queue->desc;
}

static const ID3D12CommandQueueVtbl d12_queue_vtbl = {
   .QueryInterface = d12_queue_QueryInterface,
   .AddRef = d12_queue_AddRef,
   .Release = d12_queue_Release,
   .GetPrivateData = d12_queue_GetPrivateData,
   .SetPrivateData = d12_queue_SetPrivateData,
   .SetPrivateDataInterface = d12_queue_SetPrivateDataInterface,
   .SetName = d12_queue_SetName,
   .GetDevice = d12_queue_GetDevice,
   .ExecuteCommandLists = d12_queue_ExecuteCommandLists,
   .SetMarker = d12_queue_SetMarker,
   .BeginEvent = d12_queue_BeginEvent,
   .EndEvent = d12_queue_EndEvent,
   .Signal = d12_queue_Signal,
   .Wait = d12_queue_Wait,
   .GetTimestampFrequency = d12_queue_GetTimestampFrequency,
   .GetClockCalibration = d12_queue_GetClockCalibration,
   .GetDesc = d12_queue_GetDesc,
   /* UpdateTileMappings/CopyTileMappings deliberately NULL until tiled
    * resources are a milestone.
    */
};

HRESULT
d12_queue_create(struct d12_device *device,
                 const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid,
                 void **out)
{
   if (!out)
      return E_POINTER;
   if (!desc)
      return E_INVALIDARG;

   switch (desc->Type) {
   case D3D12_COMMAND_LIST_TYPE_DIRECT:
   case D3D12_COMMAND_LIST_TYPE_COMPUTE:
   case D3D12_COMMAND_LIST_TYPE_COPY:
      /* All three run on the same asahi queue type for now; the render vs
       * compute split happens at command-buffer encoding time (G2+).
       */
      break;
   default:
      mesa_loge("d12_queue_create: queue type %d not supported",
                (int)desc->Type);
      return E_NOTIMPL;
   }

   if (desc->NodeMask > 1) {
      mesa_loge("d12_queue_create: multi-adapter NodeMask 0x%x unsupported",
                desc->NodeMask);
      return E_INVALIDARG;
   }

   struct d12_queue *queue = calloc(1, sizeof(*queue));
   if (!queue)
      return E_OUTOFMEMORY;

   struct agx_device *dev = &device->dev;

   queue->agx_queue_id =
      agx_create_command_queue(dev, DRM_ASAHI_PRIORITY_MEDIUM);

   if (drmSyncobjCreate(dev->fd, 0, &queue->syncobj)) {
      mesa_loge("d12_queue_create: drmSyncobjCreate failed");
      agx_destroy_command_queue(dev, queue->agx_queue_id);
      free(queue);
      return E_FAIL;
   }

   /* Materialize point 0 as signaled so "no work yet" transfers resolve
    * immediately, exactly as hk_queue does on init.
    */
   uint64_t zero = 0;
   drmSyncobjTimelineSignal(dev->fd, &queue->syncobj, &zero, 1);

   queue->iface.lpVtbl = (ID3D12CommandQueueVtbl *)&d12_queue_vtbl;
   queue->refcount = 1;
   queue->device = device;
   queue->desc = *desc;
   queue->timeline_value = 0;
   simple_mtx_init(&queue->lock, mtx_plain);
   d12_device_add_ref(device);

   HRESULT hr = d12_queue_QueryInterface(&queue->iface, riid, out);
   d12_queue_Release(&queue->iface);
   return hr;
}
