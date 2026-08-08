/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12Fence userspace timeline.  Linux DRM timeline syncobjs are monotonic,
 * while D3D12 explicitly permits CPU and queue signals to move a fence value
 * backwards.  Queue work is ordered by d12_queue's worker and publishes its
 * completion here; CPU Signal publishes directly.
 */

#include "d12_private.h"

#include <xf86drm.h>

#include <errno.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <unistd.h>

#include "util/u_thread.h"

struct d12_fence_event_waiter {
   struct d12_fence_event_waiter *next;
   uint64_t value;
   int fd;
};

static void
d12_signal_event_fd(int fd)
{
   /* Native Linux tests represent an auto-reset event with eventfd.  Win32
    * SetEvent coalesces repeated signals while an event is already set, so
    * avoid accumulating another eventfd token in that case. */
   struct pollfd pfd = {.fd = fd, .events = POLLIN};
   if (poll(&pfd, 1, 0) > 0)
      return;

   const uint64_t one = 1;
   if (write(fd, &one, sizeof(one)) != sizeof(one) && errno != EAGAIN)
      mesa_loge("fence event write failed: %s", strerror(errno));
}

static void
d12_fence_signal_waiters_locked(struct d12_fence *fence, bool all)
{
   struct d12_fence_event_waiter **link = &fence->event_waiters;
   while (*link) {
      struct d12_fence_event_waiter *waiter = *link;
      if (!all && fence->completed_value < waiter->value) {
         link = &waiter->next;
         continue;
      }

      *link = waiter->next;
      d12_signal_event_fd(waiter->fd);
      free(waiter);
   }
}

static void
d12_fence_satisfy_tickets_locked(struct d12_fence *fence, bool all)
{
   struct d12_fence_ticket **link = &fence->tickets;
   while (*link) {
      struct d12_fence_ticket *ticket = *link;
      if (!all && fence->completed_value < ticket->value) {
         link = &ticket->next;
         continue;
      }

      *link = ticket->next;
      ticket->registered = false;
      p_atomic_set(&ticket->satisfied, 1);
   }
}

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
      /* Releasing the public object unblocks queue and event waiters, just
       * like closing the native D3D12 fence handle.  Those waiters hold
       * private lifetime references which deliberately do not affect the
       * COM refcount returned to applications. */
      mtx_lock(&fence->lock);
      fence->destroyed = true;
      d12_fence_signal_waiters_locked(fence, true);
      d12_fence_satisfy_tickets_locked(fence, true);
      cnd_broadcast(&fence->changed);
      mtx_unlock(&fence->lock);
      d12_fence_internal_unref(fence);
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
   mtx_lock(&fence->lock);
   uint64_t point = fence->completed_value;
   mtx_unlock(&fence->lock);

   if (getenv("D3D12AGX_DUMP_SYNC"))
      mesa_logi("SYNC GetCompletedValue: fence %p -> %llu", (void *)fence,
                (unsigned long long)point);

   return point;
}

void
d12_fence_internal_ref(struct d12_fence *fence)
{
   p_atomic_inc(&fence->lifetime_refcount);
}

void
d12_fence_internal_unref(struct d12_fence *fence)
{
   if (p_atomic_dec_return(&fence->lifetime_refcount) != 0)
      return;

   cnd_destroy(&fence->changed);
   mtx_destroy(&fence->lock);
   d12_private_data_store_destroy(&fence->private_data);
   d12_device_release(fence->device);
   free(fence);
}

void
d12_fence_signal_value(struct d12_fence *fence, uint64_t value)
{
   mtx_lock(&fence->lock);
   if (!fence->destroyed) {
      fence->completed_value = value;
      d12_fence_signal_waiters_locked(fence, false);
      d12_fence_satisfy_tickets_locked(fence, false);
   }
   cnd_broadcast(&fence->changed);
   mtx_unlock(&fence->lock);
}

bool
d12_fence_wait_value(struct d12_fence *fence, uint64_t value,
                     const uint32_t *cancel)
{
   bool completed = true;

   mtx_lock(&fence->lock);
   while (!fence->destroyed && fence->completed_value < value) {
      if (!cancel) {
         cnd_wait(&fence->changed, &fence->lock);
         continue;
      }

      if (p_atomic_read(cancel)) {
         completed = false;
         break;
      }

      /* Queue destruction must be able to cancel a worker which is parked
       * on another object's condition variable.  A short timed wait keeps
       * that path bounded without polling during normal operation. */
      struct timespec deadline;
      timespec_get(&deadline, TIME_UTC);
      deadline.tv_nsec += 10 * 1000 * 1000;
      if (deadline.tv_nsec >= 1000 * 1000 * 1000) {
         deadline.tv_sec++;
         deadline.tv_nsec -= 1000 * 1000 * 1000;
      }
      cnd_timedwait(&fence->changed, &fence->lock, &deadline);
   }
   mtx_unlock(&fence->lock);

   return completed;
}

void
d12_fence_ticket_init(struct d12_fence *fence,
                      struct d12_fence_ticket *ticket, uint64_t value)
{
   memset(ticket, 0, sizeof(*ticket));
   ticket->fence = fence;
   ticket->value = value;

   mtx_lock(&fence->lock);
   if (fence->destroyed || fence->completed_value >= value) {
      p_atomic_set(&ticket->satisfied, 1);
   } else {
      ticket->next = fence->tickets;
      fence->tickets = ticket;
      ticket->registered = true;
   }
   mtx_unlock(&fence->lock);
}

void
d12_fence_ticket_fini(struct d12_fence_ticket *ticket)
{
   struct d12_fence *fence = ticket->fence;
   if (!fence)
      return;

   mtx_lock(&fence->lock);
   if (ticket->registered) {
      struct d12_fence_ticket **link = &fence->tickets;
      while (*link && *link != ticket)
         link = &(*link)->next;
      if (*link == ticket)
         *link = ticket->next;
      ticket->registered = false;
   }
   mtx_unlock(&fence->lock);
   ticket->fence = NULL;
}

bool
d12_fence_ticket_wait(struct d12_fence_ticket *ticket,
                      const uint32_t *cancel)
{
   struct d12_fence *fence = ticket->fence;
   bool completed = true;

   mtx_lock(&fence->lock);
   while (!p_atomic_read(&ticket->satisfied) && !fence->destroyed) {
      if (cancel && p_atomic_read(cancel)) {
         completed = false;
         break;
      }

      if (!cancel) {
         cnd_wait(&fence->changed, &fence->lock);
         continue;
      }

      struct timespec deadline;
      timespec_get(&deadline, TIME_UTC);
      deadline.tv_nsec += 10 * 1000 * 1000;
      if (deadline.tv_nsec >= 1000 * 1000 * 1000) {
         deadline.tv_sec++;
         deadline.tv_nsec -= 1000 * 1000 * 1000;
      }
      cnd_timedwait(&fence->changed, &fence->lock, &deadline);
   }
   mtx_unlock(&fence->lock);
   return completed;
}

static HRESULT STDMETHODCALLTYPE
d12_fence_SetEventOnCompletion(ID3D12Fence *iface, UINT64 value,
                               HANDLE event)
{
   struct d12_fence *fence = d12_fence_from_iface(iface);

   if (event != NULL) {
      struct d12_fence_event_waiter *waiter = calloc(1, sizeof(*waiter));
      if (!waiter)
         return E_OUTOFMEMORY;
      waiter->value = value;
      waiter->fd = (int)(intptr_t)event;

      mtx_lock(&fence->lock);
      if (fence->destroyed || fence->completed_value >= value) {
         d12_signal_event_fd(waiter->fd);
         free(waiter);
      } else {
         waiter->next = fence->event_waiters;
         fence->event_waiters = waiter;
      }
      mtx_unlock(&fence->lock);
      return S_OK;
   }

   /* NULL event: block until the fence reaches the value. */
   d12_fence_wait_value(fence, value, NULL);
   return S_OK;
}

struct d12_fence_multi_waiter {
   struct d12_device *device;
   struct d12_fence **fences;
   uint64_t *values;
   unsigned count;
   bool wait_all;
   int fd;
   thrd_t thread;
};

static int
d12_fence_multi_wait(struct d12_fence_multi_waiter *w)
{
   const struct timespec pause = {.tv_nsec = 1000 * 1000};

   for (;;) {
      bool any = false;
      bool all = true;

      for (unsigned i = 0; i < w->count; i++) {
         struct d12_fence *fence = w->fences[i];
         mtx_lock(&fence->lock);
         bool reached = fence->destroyed ||
                        fence->completed_value >= w->values[i];
         mtx_unlock(&fence->lock);
         any |= reached;
         all &= reached;
      }

      if ((w->wait_all && all) || (!w->wait_all && any))
         return 0;

      thrd_sleep(&pause, NULL);
   }
}

static void
d12_fence_multi_waiter_destroy(struct d12_fence_multi_waiter *w)
{
   for (unsigned i = 0; i < w->count; i++)
      d12_fence_internal_unref(w->fences[i]);

   free(w->values);
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

   d12_signal_event_fd(w->fd);

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
   w->values = calloc(count, sizeof(*w->values));
   if (!w->fences || !w->values) {
      free(w->values);
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

      d12_fence_internal_ref(fence);
      w->fences[i] = fence;
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
   d12_fence_signal_value(fence, value);
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

   if (mtx_init(&fence->lock, mtx_plain) != thrd_success) {
      free(fence);
      return E_FAIL;
   }
   if (cnd_init(&fence->changed) != thrd_success) {
      mtx_destroy(&fence->lock);
      free(fence);
      return E_FAIL;
   }

   fence->iface.lpVtbl = (ID3D12FenceVtbl *)&d12_fence_vtbl;
   fence->refcount = 1;
   fence->lifetime_refcount = 1;
   fence->device = device;
   fence->completed_value = initial_value;
   d12_private_data_store_init(&fence->private_data);
   d12_device_add_ref(device);

   HRESULT hr = d12_fence_QueryInterface(&fence->iface, riid, out);
   d12_fence_Release(&fence->iface);
   return hr;
}
