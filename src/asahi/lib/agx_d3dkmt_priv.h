/*
 * Copyright 2026 the Asahi Linux contributors
 * SPDX-License-Identifier: MIT
 */
#pragma once

/*
 * Internal state shared by agx_device_d3dkmt.c and agx_sync_d3dkmt.c. Not
 * installed, not included by anything outside those two files: the public
 * surface is agx_device_d3dkmt.h.
 */

#include <windows.h>

#include "util/simple_mtx.h"

#include "agx_d3dkmt.h"
#include "agx_d3dkmt_core.h"
#include "agx_d3dkmt_runtime.h"

/*
 * Staging ring for AGX command buffers.
 *
 * hk_queue.c builds the flat drm_asahi_cmd_header + payload stream in a
 * util_dynarray of plain host memory and hands the winsys a userspace pointer
 * (hk_queue.c:864-872). D3DKMTSubmitCommand instead takes a GPU virtual
 * address, so the winsys copies the stream into a GPU-mapped BO. Slots are
 * allocated lazily up to the renderer's 64-submit in-flight limit, so a
 * normal frame queue does not collapse to two-submit backpressure. A slot is
 * not reused until the context fence proves its previous use has retired.
 */
#define AGX_D3DKMT_STAGING_RING 64

struct agx_d3dkmt_ctx {
   agx_kmt_handle hContext; /* 0 = free slot */

   /* Per-context completion fence. Owned by the winsys and separate from
    * anything Honeykrisp signals, so staging reuse never depends on the
    * caller's choice of out-syncs. */
   agx_kmt_handle fence;
   const volatile uint64_t *fence_cpu;
   uint64_t fence_next;
   bool submit_failed;
   bool completion_unknown;
   uint32_t submit_refs;

   struct agx_bo *staging[AGX_D3DKMT_STAGING_RING];
   uint64_t staging_retire[AGX_D3DKMT_STAGING_RING];
   unsigned staging_slot;
};

struct agx_d3dkmt_process_device;
struct agx_d3dkmt_process_va;

struct agx_d3dkmt_winsys {
   struct agx_d3dkmt_fns fns;
   struct agx_d3dkmt_runtime_transport runtime;
   struct agx_d3dkmt_policy policy;
   struct agx_kmt_luid luid;

   agx_kmt_handle adapter;
   agx_kmt_handle device;

   /* PID+adapter lease for the one D3DKMT device backing the process GPU-VA
    * root.  The section is named so separately linked Mesa copies converge
    * on the same KMT handle rather than merely sharing an allocator. */
   struct agx_d3dkmt_process_device *process_device;

   /* Paging queue: every GPU VA and residency operation is asynchronous on
    * WDDM and completes on this queue's monitored fence. DRM's VM_BIND is
    * synchronous, so the winsys waits on it before returning -- see
    * d3dkmt_wait_paging_fence(). */
   agx_kmt_handle paging_queue;
   agx_kmt_handle paging_fence;
   const volatile uint64_t *paging_fence_cpu;

   /* UpdateGpuVirtualAddress is a rendering-context interlock, not a paging-
    * queue operation. Keep one ordinary WDDM virtual-addressing context and
    * its monitored fence alive for the device lifetime. Fence N is the value
    * the update waits for; successful completion signals N + 1. */
   agx_kmt_handle va_context;
   agx_kmt_handle va_fence;
   const volatile uint64_t *va_fence_cpu;
   uint64_t va_fence_next;

   simple_mtx_t lock; /* guards pool, syncs, ctxs and the staging rings */
   struct agx_d3dkmt_handle_pool pool;
   struct agx_d3dkmt_sync_table syncs;

   /* Serialises publication/copy of the canonical conservative read vector.
    * BO membership itself remains guarded by agx_device::bo_map_lock. */
   simple_mtx_t read_set_lock;
   struct agx_d3dkmt_read_set_cache read_set_cache;

   struct agx_d3dkmt_ctx *ctxs;
   uint32_t ctx_capacity;
   uint32_t active_submits;
   bool device_lost;

   /* Lazily created SynchronizationOnly context used to implement DRM
    * syncobj transfer as a GPU-side wait-then-signal. */
   agx_kmt_handle xfer_ctx;

   /* The user GPU VA window, reserved at VM_CREATE time. Up to two ranges:
    * CONTRACT.md SS4.2 puts a 16 KiB kernel/firmware carve-out page in the
    * MIDDLE of the window, so it is not one contiguous reservation. */
   struct agx_d3dkmt_va_range va[2];
   uint32_t va_count;

   /* PID+adapter process broker.  WDDM has one GPU-VA root per process, not
    * per D3DKMT device; this object joins every statically linked driver copy
    * to the same reservation, allocation bitmap and fixed ABI BOs. */
   struct agx_d3dkmt_process_va *process_va;

   /* KMTQAITYPE_UMDRIVERPRIVATE payload: the AGX params blob verbatim. */
   void *params;
   uint32_t params_size;

   /* A Direct3D UMD receives an already-created runtime device.  It owns
    * child allocations/contexts/synchronization objects and the paging queue
    * it creates through callbacks, but never the adapter or device itself. */
   bool runtime_owned;
};

#define AGX_D3DKMT_CALL(ws, member, ...)                                      \
   ((ws)->runtime.ops                                                         \
       ? (ws)->runtime.ops->member((ws)->runtime.userdata, __VA_ARGS__)       \
       : (ws)->fns.member(__VA_ARGS__))

static inline void
agx_d3dkmt_mark_device_lost(struct agx_d3dkmt_winsys *ws,
                            struct agx_d3dkmt_ctx *ctx)
{
   simple_mtx_lock(&ws->lock);
   ws->device_lost = true;
   if (ctx != NULL)
      ctx->submit_failed = true;
   simple_mtx_unlock(&ws->lock);
}

static inline void
agx_d3dkmt_mark_completion_unknown(struct agx_d3dkmt_winsys *ws,
                                    struct agx_d3dkmt_ctx *ctx)
{
   simple_mtx_lock(&ws->lock);
   ws->device_lost = true;
   ctx->submit_failed = true;
   ctx->completion_unknown = true;
   simple_mtx_unlock(&ws->lock);
}

static inline struct agx_d3dkmt_winsys *
agx_d3dkmt_ws(struct agx_device *dev)
{
   return (struct agx_d3dkmt_winsys *)dev->hdev;
}

/* Infinite waits use D3DKMT's synchronous form. Finite waits poll the
 * CPU-visible monitored-fence mapping because the asynchronous event form
 * has no documented cancellation operation. */
int agx_d3dkmt_wait_fence(struct agx_d3dkmt_winsys *ws, agx_kmt_handle fence,
                          const volatile uint64_t *fence_cpu, uint64_t value,
                          uint32_t timeout_ms);

int agx_d3dkmt_poll_cpu_fences(
   const volatile uint64_t *const *current, const uint64_t *wanted,
   uint32_t count, bool wait_any, uint32_t timeout_ms,
   uint32_t *first_signaled);

/* Wait for a paging-queue operation identified by `value` to complete. */
int agx_d3dkmt_wait_paging(struct agx_d3dkmt_winsys *ws, uint64_t value);

/* Map an NTSTATUS onto the errno a drmIoctl() caller would have seen, and
 * store it in errno. Returns -1 so call sites can `return
 * agx_d3dkmt_fail(st);`. */
int agx_d3dkmt_fail(agx_kmt_status st);
