/*
 * Copyright 2026 the Asahi Linux contributors
 * SPDX-License-Identifier: MIT
 */

/*
 * agx_device_ops_t over D3DKMT. See agx_device_d3dkmt.h for why, and
 * agx_d3dkmt.h's CONTRACT ASSUMPTIONS block for every kernel-side guess this
 * file makes, tagged with the contract question it answers.
 *
 * Each op below is annotated with the AGX ioctl it replaces and with the one
 * thing that changes semantically, because in three places the change is real
 * rather than cosmetic:
 *
 *   VM_BIND    DRM binds synchronously; WDDM page-table updates complete on a
 *              paging fence. This file restores synchrony by waiting.
 *   SUBMIT     DRM submits waits, work and signals as one atomic ioctl; WDDM
 *              has no sync list on SubmitCommand and expresses the same thing
 *              as three ordered calls on one context.
 *   GEM handle DRM GEM handles are dense small integers; D3DKMT handles are
 *              not, and agx_device.c iterates the handle space.
 *
 * NOT RUNTIME-VERIFIED: there is no WDDM miniport for this GPU yet and no
 * hardware available this session. Everything here cross-compiles for
 * aarch64-w64-mingw32 as part of the real ICD; nothing here has executed.
 */

#include "agx_device_d3dkmt.h"
#include "agx_d3dkmt_priv.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "util/macros.h"
#include "util/u_math.h"
#include "agx_bo.h"
#include "layout.h"

/* The mirrored constants in agx_d3dkmt_core.h must equal the real ones. This
 * is the tripwire that keeps the Mesa-free core honest. */
static_assert(AGX_D3DKMT_BIND_UNBIND == DRM_ASAHI_BIND_UNBIND, "BIND_UNBIND");
static_assert(AGX_D3DKMT_BIND_READ == DRM_ASAHI_BIND_READ, "BIND_READ");
static_assert(AGX_D3DKMT_BIND_WRITE == DRM_ASAHI_BIND_WRITE, "BIND_WRITE");
static_assert(AGX_D3DKMT_BIND_SINGLE_PAGE == DRM_ASAHI_BIND_SINGLE_PAGE,
              "BIND_SINGLE_PAGE");
static_assert(AGX_D3DKMT_BO_SHARED == AGX_BO_SHARED, "BO_SHARED");
static_assert(AGX_D3DKMT_BO_LOW_VA == AGX_BO_LOW_VA, "BO_LOW_VA");
static_assert(AGX_D3DKMT_BO_EXEC == AGX_BO_EXEC, "BO_EXEC");
static_assert(AGX_D3DKMT_BO_WRITEBACK == AGX_BO_WRITEBACK, "BO_WRITEBACK");
static_assert(AGX_D3DKMT_BO_SHAREABLE == AGX_BO_SHAREABLE, "BO_SHAREABLE");
static_assert(AGX_D3DKMT_BO_READONLY == AGX_BO_READONLY, "BO_READONLY");
static_assert(AGX_D3DKMT_PAGE_SIZE == AIL_PAGESIZE, "page size");
static_assert(AGXW_PARAMS_GLOBAL_SIZE == sizeof(struct drm_asahi_params_global),
              "GET_PARAMS escape payload size");
static_assert(AGXW_GET_TIME_SIZE == sizeof(struct drm_asahi_get_time),
              "GET_TIME escape payload size");
static_assert((int)AGXW_SYNC_BINARY == (int)DRM_ASAHI_SYNC_SYNCOBJ,
              "SYNC_SYNCOBJ");
static_assert((int)AGXW_SYNC_TIMELINE == (int)DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ,
              "SYNC_TIMELINE_SYNCOBJ");
static_assert(sizeof(struct agxw_sync) == sizeof(struct drm_asahi_sync),
              "agxw_sync mirrors drm_asahi_sync");
static_assert((int)AGXW_PRIORITY_LOW == (int)DRM_ASAHI_PRIORITY_LOW,
              "priority LOW");
static_assert((int)AGXW_PRIORITY_REALTIME == (int)DRM_ASAHI_PRIORITY_REALTIME,
              "priority REALTIME");

/* -------------------------------------------------------------------------
 * Error mapping and waits.
 * ------------------------------------------------------------------------- */
int
agx_d3dkmt_fail(agx_kmt_status st)
{
   switch ((uint32_t)st) {
   case 0xC0000017u: /* STATUS_NO_MEMORY */
   case 0xC000009Au: /* STATUS_INSUFFICIENT_RESOURCES */
      errno = ENOMEM;
      break;
   case 0xC000000Du: /* STATUS_INVALID_PARAMETER */
   case 0xC0000008u: /* STATUS_INVALID_HANDLE */
      errno = EINVAL;
      break;
   case 0xC0000022u: /* STATUS_ACCESS_DENIED */
      errno = EACCES;
      break;
   case 0x00000102u: /* STATUS_TIMEOUT */
      errno = ETIME;
      break;
   case 0xC00000BBu: /* STATUS_NOT_SUPPORTED */
      errno = ENOTSUP;
      break;
   case 0xC01E0116u: /* STATUS_GRAPHICS_ALLOCATION_INVALID */
   case 0xC01E0110u: /* STATUS_GRAPHICS_NO_VIDEO_MEMORY */
      errno = ENOMEM;
      break;
   default:
      errno = EIO;
      break;
   }

   return -1;
}

int
agx_d3dkmt_wait_fence(struct agx_d3dkmt_winsys *ws, agx_kmt_handle fence,
                      uint64_t value, uint32_t timeout_ms)
{
   const agx_kmt_handle handles[1] = {fence};
   const uint64_t values[1] = {value};

   struct agx_kmt_waitsyncfromcpu wait = {
      .hDevice = ws->device,
      .ObjectCount = 1,
      .ObjectHandleArray = handles,
      .FenceValueArray = values,
      .hAsyncEvent = timeout_ms == AGX_D3DKMT_INFINITE ? NULL : ws->wait_event,
   };

   /*
    * D3DKMTWaitForSynchronizationObjectFromCpu has no timeout parameter at
    * all: with hAsyncEvent NULL it blocks until satisfied, and with an event
    * it returns immediately and signals the event later. DRM's absolute
    * deadline therefore becomes "arm the event, then wait on the event",
    * which is why the winsys keeps one around.
    */
   if (wait.hAsyncEvent != NULL)
      ResetEvent(ws->wait_event);

   agx_kmt_status st = ws->fns.WaitForSynchronizationObjectFromCpu(&wait);
   if (!AGX_KMT_SUCCESS(st))
      return agx_d3dkmt_fail(st);

   if (wait.hAsyncEvent == NULL)
      return 0;

   DWORD r = WaitForSingleObject(ws->wait_event, timeout_ms);
   if (r == WAIT_OBJECT_0)
      return 0;
   if (r == WAIT_TIMEOUT) {
      errno = ETIME;
      return -1;
   }

   errno = EIO;
   return -1;
}

int
agx_d3dkmt_wait_paging(struct agx_d3dkmt_winsys *ws, uint64_t value)
{
   if (value == 0)
      return 0;

   /* Fast path: the paging fence has a CPU-visible value, so most waits cost
    * a load rather than a kernel transition. */
   if (ws->paging_fence_cpu != NULL && *ws->paging_fence_cpu >= value)
      return 0;

   return agx_d3dkmt_wait_fence(ws, ws->paging_fence, value,
                                AGX_D3DKMT_INFINITE);
}

/* -------------------------------------------------------------------------
 * Contexts.
 * ------------------------------------------------------------------------- */
static struct agx_d3dkmt_ctx *
ctx_get(struct agx_d3dkmt_winsys *ws, uint32_t queue_id)
{
   if (queue_id == 0 || queue_id > ws->ctx_capacity)
      return NULL;

   struct agx_d3dkmt_ctx *ctx = &ws->ctxs[queue_id - 1];
   return ctx->hContext ? ctx : NULL;
}

/* -------------------------------------------------------------------------
 * GEM_CREATE / GEM_CLOSE.
 * ------------------------------------------------------------------------- */
static agx_kmt_handle
d3dkmt_create_allocation(struct agx_d3dkmt_winsys *ws, uint64_t size,
                         enum agx_bo_flags flags)
{
   /*
    * CONTRACT.md SS12.1: the private-data size is pinned, and treated as a
    * memory-safety property rather than a convenience, because the D3D
    * runtime does not validate what the UMD claims against what the kernel
    * reads. Assert the width; never infer it.
    */
   struct agxw_alloc_private priv = {
      .version = AGXW_ALLOC_PRIVATE_VERSION,
      .struct_size = sizeof(priv),
      .size = size,
      .flags = agx_d3dkmt_agxw_alloc_flags((uint32_t)flags),
      .align_log2 = AGXW_PAGE_BITS,
   };
   static_assert(sizeof(priv) == AGXW_ALLOC_PRIVATE_SIZE,
                 "allocation private data width");

   struct agx_kmt_allocationinfo2 info = {
      .pPrivateDriverData = &priv,
      .PrivateDriverDataSize = sizeof(priv),
   };

   /* Honeykrisp has no resource/allocation split at this layer, so the
    * resource-level blob is absent -- explicitly legal (CONTRACT.md SS3.2). */
   struct agx_kmt_createallocation create = {
      .hDevice = ws->device,
      .NumAllocations = 1,
      .pAllocationInfo2 = &info,
      .Flags = agx_d3dkmt_create_flags((uint32_t)flags),
   };

   agx_kmt_status st = ws->fns.CreateAllocation2(&create);
   if (!AGX_KMT_SUCCESS(st)) {
      agx_d3dkmt_fail(st);
      return 0;
   }

   /*
    * CONTRACT.md SS7.2 item 3: residency is expressed by MakeResident, not by
    * a per-submit allocation list. SS4.8 notes the v0.1 aperture never evicts
    * so the paging fence is normally already signalled -- but the call is not
    * optional, and getting it wrong faults the GPU rather than returning an
    * error.
    */
   if (ws->policy.needs_residency) {
      const agx_kmt_handle list[1] = {info.hAllocation};
      struct agx_kmt_makeresident mr = {
         .hPagingQueue = ws->paging_queue,
         .NumAllocations = 1,
         .AllocationList = list,
      };

      st = ws->fns.MakeResident(&mr);
      if (!AGX_KMT_SUCCESS(st)) {
         struct agx_kmt_destroyallocation2 destroy = {
            .hDevice = ws->device,
            .phAllocationList = list,
            .AllocationCount = 1,
         };
         ws->fns.DestroyAllocation2(&destroy);
         agx_d3dkmt_fail(st);
         return 0;
      }

      if (agx_d3dkmt_wait_paging(ws, mr.PagingFenceValue))
         return 0;
   }

   return info.hAllocation;
}

/*
 * Clone of agx_bo_alloc()/win_bo_alloc() with GEM_CREATE replaced by
 * D3DKMTCreateAllocation2. The handle split is the only structural change:
 * bo->handle stays a dense index into dev->bo_map (see agx_d3dkmt_core.h),
 * bo->uapi_handle carries the D3DKMT allocation handle that every kernel-
 * facing call site already passes.
 */
static struct agx_bo *
d3dkmt_bo_alloc(struct agx_device *dev, size_t size, size_t align,
                enum agx_bo_flags flags)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);
   struct agx_bo *bo;

   /* executable implies low va */
   assert(!(flags & AGX_BO_EXEC) || (flags & AGX_BO_LOW_VA));

   agx_kmt_handle alloc = d3dkmt_create_allocation(ws, size, flags);
   if (alloc == 0) {
      fprintf(stderr, "D3DKMTCreateAllocation2 failed\n");
      return NULL;
   }

   simple_mtx_lock(&ws->lock);
   uint32_t handle = agx_d3dkmt_pool_alloc(&ws->pool, alloc);
   simple_mtx_unlock(&ws->lock);

   if (handle == 0) {
      const agx_kmt_handle list[1] = {alloc};
      struct agx_kmt_destroyallocation2 destroy = {
         .hDevice = ws->device,
         .phAllocationList = list,
         .AllocationCount = 1,
      };
      ws->fns.DestroyAllocation2(&destroy);
      return NULL;
   }

   simple_mtx_lock(&dev->bo_map_lock);
   bo = agx_lookup_bo(dev, handle);
   dev->max_handle = MAX2(dev->max_handle, handle);
   simple_mtx_unlock(&dev->bo_map_lock);

   /* Fresh handle */
   assert(!memcmp(bo, &((struct agx_bo){}), sizeof(*bo)));

   bo->dev = dev;
   bo->size = size;
   bo->align = align;
   bo->flags = flags;
   bo->handle = handle;
   bo->uapi_handle = alloc;
   bo->prime_fd = -1;

   enum agx_va_flags va_flags = flags & AGX_BO_LOW_VA ? AGX_VA_USC : 0;
   bo->va = agx_va_alloc(dev, size, bo->align, va_flags, 0);
   if (!bo->va) {
      fprintf(stderr, "Failed to allocate BO VMA\n");
      agx_bo_free(dev, bo);
      return NULL;
   }

   uint32_t bind = DRM_ASAHI_BIND_READ;
   if (!(flags & AGX_BO_READONLY))
      bind |= DRM_ASAHI_BIND_WRITE;

   if (agx_bo_bind(dev, bo, bo->va->addr, bo->size, 0, bind)) {
      agx_bo_free(dev, bo);
      return NULL;
   }

   return bo;
}

/*
 * GEM_CLOSE. Takes the DENSE handle, because agx_bo_free() memsets struct
 * agx_bo to zero before calling this (agx_device.c:100-105, deliberately, to
 * avoid a lookup race) -- so bo->uapi_handle is already gone and the pool is
 * the only remaining way to recover the D3DKMT handle.
 */
static void
d3dkmt_bo_free(struct agx_device *dev, uint32_t handle)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);

   simple_mtx_lock(&ws->lock);
   agx_kmt_handle alloc = agx_d3dkmt_pool_release(&ws->pool, handle);
   simple_mtx_unlock(&ws->lock);

   if (alloc == 0)
      return;

   const agx_kmt_handle list[1] = {alloc};
   struct agx_kmt_destroyallocation2 destroy = {
      .hDevice = ws->device,
      .phAllocationList = list,
      .AllocationCount = 1,
   };

   agx_kmt_status st = ws->fns.DestroyAllocation2(&destroy);
   if (!AGX_KMT_SUCCESS(st))
      agx_d3dkmt_fail(st);
}

/* -------------------------------------------------------------------------
 * VM_BIND.
 * ------------------------------------------------------------------------- */
static int
d3dkmt_bo_bind(struct agx_device *dev, struct drm_asahi_gem_bind_op *ops,
               uint32_t count)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);
   struct agx_d3dkmt_bind stack_binds[8];
   struct agx_kmt_updategpuva_op stack_ops[16];
   struct agx_d3dkmt_bind *binds = stack_binds;
   struct agx_kmt_updategpuva_op *uops = stack_ops;
   uint32_t nops = 0;
   int ret = -1;

   if (count == 0)
      return 0;

   if (count > ARRAY_SIZE(stack_binds)) {
      binds = malloc(count * sizeof(*binds));
      if (!binds) {
         errno = ENOMEM;
         return -1;
      }
   }

   /* Resolve dense GEM handles to D3DKMT allocation handles. */
   simple_mtx_lock(&ws->lock);
   for (uint32_t i = 0; i < count; ++i) {
      binds[i] = (struct agx_d3dkmt_bind){
         .flags = ops[i].flags,
         .kmt_handle = ops[i].handle,
         .offset = ops[i].offset,
         .range = ops[i].range,
         .addr = ops[i].addr,
      };
   }
   simple_mtx_unlock(&ws->lock);

   if (agx_d3dkmt_translate_binds(binds, count, &ws->policy, NULL, 0, &nops) !=
       AGX_D3DKMT_OK) {
      errno = EINVAL;
      goto out;
   }

   if (nops > ARRAY_SIZE(stack_ops)) {
      uops = malloc((size_t)nops * sizeof(*uops));
      if (!uops) {
         errno = ENOMEM;
         goto out;
      }
   }

   if (agx_d3dkmt_translate_binds(binds, count, &ws->policy, uops, nops,
                                  &nops) != AGX_D3DKMT_OK) {
      errno = EINVAL;
      goto out;
   }

   /*
    * The one real semantic change in this op. DRM_IOCTL_ASAHI_VM_BIND returns
    * with the binding in effect; D3DKMTUpdateGpuVirtualAddress queues page-
    * table work that completes on a monitored fence. agx_bo_alloc() maps and
    * then immediately writes through the mapping, so the winsys must restore
    * the synchronous contract rather than push asynchrony up into Honeykrisp.
    */
   simple_mtx_lock(&ws->lock);
   uint64_t fence_value = ++dev->next_blob_id;
   struct agx_kmt_updategpuva update = {
      .hDevice = ws->device,
      .hFenceObject = ws->paging_fence,
      .NumOperations = nops,
      .Operations = uops,
      .FenceValue = fence_value,
   };
   agx_kmt_status st = ws->fns.UpdateGpuVirtualAddress(&update);
   simple_mtx_unlock(&ws->lock);

   if (!AGX_KMT_SUCCESS(st)) {
      fprintf(stderr, "D3DKMTUpdateGpuVirtualAddress failed\n");
      ret = agx_d3dkmt_fail(st);
      goto out;
   }

   ret = agx_d3dkmt_wait_paging(ws, fence_value);

out:
   if (binds != stack_binds)
      free(binds);
   if (uops != stack_ops)
      free(uops);
   return ret;
}

/* -------------------------------------------------------------------------
 * GEM_MMAP / GEM_MUNMAP.
 *
 * Linux does GEM_MMAP_OFFSET plus mmap(2); the AGXKMD port invented a
 * GEM_MMAP ioctl. WDDM already has the operation: D3DKMTLock2 returns a CPU
 * mapping of an allocation. It does NOT accept a placement hint, which is why
 * VK_EXT_map_memory_placed stays disabled on Windows (already the case,
 * patch 0007) -- agx_bo_map_placed()'s fixed_addr is unusable here and is
 * asserted away rather than silently ignored.
 * ------------------------------------------------------------------------- */
static void
d3dkmt_bo_mmap(struct agx_device *dev, struct agx_bo *bo, void *fixed_addr)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);

   assert(bo->_map == NULL && "not double mapped");
   assert(fixed_addr == NULL &&
          "placed maps need VK_EXT_map_memory_placed, disabled on Windows");
   (void)fixed_addr;

   struct agx_kmt_lock2 lock = {
      .hDevice = ws->device,
      .hAllocation = bo->uapi_handle,
   };

   agx_kmt_status st = ws->fns.Lock2(&lock);
   if (!AGX_KMT_SUCCESS(st)) {
      bo->_map = NULL;
      agx_d3dkmt_fail(st);
      fprintf(stderr, "D3DKMTLock2 failed: handle=%u\n", bo->uapi_handle);
      return;
   }

   bo->_map = lock.pData;
}

static void
d3dkmt_bo_munmap(struct agx_device *dev, struct agx_bo *bo)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);

   struct agx_kmt_unlock2 unlock = {
      .hDevice = ws->device,
      .hAllocation = bo->uapi_handle,
   };

   ws->fns.Unlock2(&unlock);
   bo->_map = NULL;
}

/* -------------------------------------------------------------------------
 * GET_PARAMS.
 *
 * KMTQAITYPE_UMDRIVERPRIVATE is the WDDM-native miniport->UMD capability
 * channel (it is what DxgkDdiQueryAdapterInfo(DXGKQAITYPE_UMDRIVERPRIVATE)
 * exists to serve, and what every vendor UMD uses for exactly this). It is
 * queried once at open; this op is a copy out of that cached blob.
 * ------------------------------------------------------------------------- */
static int
d3dkmt_escape(struct agx_d3dkmt_winsys *ws, uint32_t op, void *payload,
              uint32_t payload_size)
{
   /*
    * CONTRACT.md SS8: exactly two escapes exist, both pure device-property
    * reads with no WDDM equivalent -- GET_PARAMS and GET_TIME. Nothing that
    * allocates, binds, submits or synchronises goes through one, because an
    * escape for any of those would rebuild the private-side-channel split
    * this whole port exists to remove.
    */
   const uint32_t total = (uint32_t)sizeof(struct agxw_escape_header) +
                          payload_size;
   uint8_t *blob = malloc(total);
   if (blob == NULL) {
      errno = ENOMEM;
      return -1;
   }

   struct agxw_escape_header hdr = {
      .magic = AGXW_ESCAPE_MAGIC,
      .version = AGXW_ESCAPE_VERSION,
      .op = op,
      .payload_size = payload_size,
   };
   memcpy(blob, &hdr, sizeof(hdr));
   memset(blob + sizeof(hdr), 0, payload_size);

   struct agx_kmt_escape escape = {
      .hAdapter = ws->adapter,
      .pPrivateDriverData = blob,
      .PrivateDriverDataSize = total,
   };

   agx_kmt_status st = ws->fns.Escape(&escape);
   if (!AGX_KMT_SUCCESS(st)) {
      free(blob);
      return agx_d3dkmt_fail(st);
   }

   memcpy(&hdr, blob, sizeof(hdr));
   if (hdr.status != 0) {
      free(blob);
      errno = -hdr.status;
      return -1;
   }

   memcpy(payload, blob + sizeof(hdr), payload_size);
   free(blob);
   return 0;
}

/*
 * GET_PARAMS. One of the two escapes -- the AGX global parameter block has no
 * WDDM equivalent at all, and KMTQAITYPE_UMDRIVERPRIVATE is spoken for by the
 * adapter identity blob (CONTRACT.md SS2.4, SS8).
 *
 * Fetched once at open, because agx_open_device() calls this before anything
 * else and the values never change; this op is a copy out of that cache.
 */
static ssize_t
d3dkmt_get_params(struct agx_device *dev, void *buf, size_t size)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);

   memset(buf, 0, size);

   if (ws->params == NULL || ws->params_size == 0)
      return -EINVAL;

   memcpy(buf, ws->params, MIN2(size, (size_t)ws->params_size));
   return (ssize_t)ws->params_size;
}

/* -------------------------------------------------------------------------
 * GEM_BIND_OBJECT (timestamp objects).
 *
 * AGX registers a BO with the firmware's timestamp-object table and gets back
 * a firmware-scoped handle that goes into drm_asahi_cmd_render::ts_vtx /
 * ts_frag.
 *
 * CONTRACT.md SS3.5 puts this on AGXW_ALLOC_TIMESTAMP: the KMD does the
 * context-0 bind at ALLOCATION time and returns object_handle in the output
 * private data. That is the right shape, and it is not an escape -- but it
 * does not fit the current call flow, and this is a contract gap to close
 * rather than something the UMD can paper over:
 *
 *   1. struct agxw_alloc_private has no object_handle field. It is
 *      {version, struct_size, size, flags, align_log2, reserved}, so there is
 *      nowhere for SS3.5's promised output to go.
 *   2. hk_query_pool.c:287 calls agx_bind_timestamps() on a BO that
 *      agx_bo_create() has ALREADY created, so the winsys cannot know at
 *      create time that an allocation will be a timestamp buffer. Making it
 *      knowable means a new agx_bo_flags bit plumbed through
 *      hk_create_query_pool -- small, but a Honeykrisp change, not a winsys
 *      one.
 *
 * Until both are settled this op is an identity: the "object handle" IS the
 * allocation handle, which is a value the miniport already has and can map to
 * a firmware slot lazily when it parses ts_vtx/ts_frag out of the command
 * stream. That is still not an escape, and it keeps timestamp queries on the
 * same allocator as everything else.
 * ------------------------------------------------------------------------- */
static int
d3dkmt_bo_bind_object(struct agx_device *dev,
                      struct drm_asahi_gem_bind_object *bind)
{
   (void)dev;

   if (bind->op != DRM_ASAHI_BIND_OBJECT_OP_BIND) {
      errno = EINVAL;
      return -1;
   }

   if (bind->handle == 0) {
      errno = EINVAL;
      return -1;
   }

   bind->object_handle = bind->handle;
   return 0;
}

static int
d3dkmt_bo_unbind_object(struct agx_device *dev, uint32_t object_handle)
{
   (void)dev;
   (void)object_handle;
   return 0;
}

/* -------------------------------------------------------------------------
 * SUBMIT.
 * ------------------------------------------------------------------------- */
static struct agx_bo *
staging_for(struct agx_device *dev, struct agx_d3dkmt_ctx *ctx, uint32_t size)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);
   unsigned slot = ctx->staging_slot;
   struct agx_bo *bo = ctx->staging[slot];

   if (bo != NULL && bo->size < size) {
      /* Wait for the slot to retire before recycling its memory. */
      if (agx_d3dkmt_wait_fence(ws, ctx->fence, ctx->staging_retire[slot],
                                AGX_D3DKMT_INFINITE))
         return NULL;
      agx_bo_unreference(dev, bo);
      bo = ctx->staging[slot] = NULL;
   }

   if (bo == NULL) {
      size_t alloc = ALIGN_POT(MAX2((size_t)size, (size_t)AIL_PAGESIZE),
                               AIL_PAGESIZE);
      bo = agx_bo_create(dev, alloc, 0, AGX_BO_WRITEBACK, "D3DKMT cmdbuf");
      if (bo == NULL)
         return NULL;
      ctx->staging[slot] = bo;
      ctx->staging_retire[slot] = 0;
   } else if (ctx->staging_retire[slot] != 0) {
      if (agx_d3dkmt_wait_fence(ws, ctx->fence, ctx->staging_retire[slot],
                                AGX_D3DKMT_INFINITE))
         return NULL;
   }

   return bo;
}

/*
 * DRM_IOCTL_ASAHI_SUBMIT carries in-syncs, the command stream and out-syncs in
 * ONE ioctl. Under the contract that becomes:
 *
 *   1. D3DKMTWaitForSynchronizationObjectFromGpu on the context, for the
 *      in-syncs. NOT the submit private data's in_sync_count, which
 *      CONTRACT.md SS7.3 requires to be 0 in contract version 1 -- AppleAgxGpu
 *      rejects a non-zero one (submit.c:629-630) and the WDDM front end
 *      inherits that gap. The contract notes this is the idiomatic shape
 *      regardless, and stays the recommendation after the gap closes.
 *   2. D3DKMTSubmitCommand, with Commands pointing at an AGXW_DMA_HEADER
 *      followed by the verbatim AGX command stream. No allocation list, no
 *      patch locations: final GPU VAs are already in the stream, exactly as on
 *      Linux (CONTRACT.md SS7.2 item 3).
 *   3. D3DKMTSignalSynchronizationObjectFromGpu, on the same context, for
 *      every out-sync and the staging-retire fence. This queues an ordered
 *      dxgkrnl software signal packet behind the submitted DMA. D3DKMT sync
 *      handles are dxgkrnl handles and MUST NOT appear in KMD private data.
 *
 * A D3DKMT context is a strictly serial queue, so (1)-(3) are equivalent
 * to the atomic ioctl PROVIDED nothing fails in between -- a failure after the
 * wait would leave a dangling dependency on the context. Everything under
 * UMD control (context lookup, staging allocation, sync resolution, blob
 * sizing) is therefore done BEFORE the first D3DKMT call. A thunk failure
 * after sync values are reserved poisons the context fail-closed.
 */
static int
d3dkmt_submit(struct agx_device *dev, struct drm_asahi_submit *submit,
              struct agx_submit_virt *virt)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);
   const struct drm_asahi_sync *syncs =
      (const struct drm_asahi_sync *)(uintptr_t)submit->syncs;
   agx_kmt_handle *waits = NULL;
   uint64_t *wait_values = NULL;
   agx_kmt_handle *signals = NULL;
   uint64_t *signal_values = NULL;
   int ret = -1;

   (void)virt; /* no vdrm concept here, same as the Linux drm backend */

   const uint32_t n_in = submit->in_sync_count;
   const uint32_t n_out = submit->out_sync_count;

   /* +1 for the winsys's own staging-retire fence. */
   if ((uint64_t)n_in + n_out + 1 > AGXW_MAX_SYNCS_PER_SUBMIT ||
       submit->cmdbuf_size > AGXW_MAX_CMDBUF_BYTES) {
      errno = EINVAL;
      return -1;
   }

   struct agxw_submit_private priv = {
      .version = AGXW_SUBMIT_PRIVATE_VERSION,
      .struct_size = sizeof(priv),
      .in_sync_count = 0,
      .out_sync_count = 0,
   };
   if (agxw_submit_private_validate(&priv, sizeof(priv)) != 0) {
      errno = EINVAL;
      return -1;
   }

   simple_mtx_lock(&ws->lock);
   struct agx_d3dkmt_ctx *ctx = ctx_get(ws, submit->queue_id);
   if (ctx == NULL) {
      simple_mtx_unlock(&ws->lock);
      errno = EINVAL;
      return -1;
   }
   if (ws->device_lost || ctx->submit_failed || ctx->submit_refs != 0) {
      simple_mtx_unlock(&ws->lock);
      errno = (ws->device_lost || ctx->submit_failed) ? EIO : EBUSY;
      return -1;
   }
   ctx->submit_refs = 1;
   ws->active_submits++;
   const unsigned staging_slot = ctx->staging_slot;
   simple_mtx_unlock(&ws->lock);

   signals = malloc((size_t)(n_out + 1) * sizeof(*signals));
   signal_values = malloc((size_t)(n_out + 1) * sizeof(*signal_values));
   if (n_in > 0) {
      waits = malloc((size_t)n_in * sizeof(*waits));
      wait_values = malloc((size_t)n_in * sizeof(*wait_values));
   }
   if (signals == NULL || signal_values == NULL ||
       (n_in > 0 && (waits == NULL || wait_values == NULL))) {
      errno = ENOMEM;
      goto out;
   }

   /* The DMA buffer: header + verbatim AGX command stream, in a BO this
    * winsys owns and has already mapped to GPU VA. */
   struct agx_bo *staging =
      staging_for(dev, ctx,
                  (uint32_t)sizeof(struct agxw_dma_header) +
                     submit->cmdbuf_size);
   if (staging == NULL) {
      errno = ENOMEM;
      goto out;
   }

   uint8_t *map = agx_bo_map(staging);
   if (map == NULL) {
      errno = ENOMEM;
      goto out;
   }

   struct agxw_dma_header dma = {
      .magic = AGXW_DMA_MAGIC,
      .header_size = sizeof(dma),
      .cmdbuf_size = submit->cmdbuf_size,
   };
   memcpy(map, &dma, sizeof(dma));
   memcpy(map + sizeof(dma), (const void *)(uintptr_t)submit->cmdbuf,
          submit->cmdbuf_size);

   /* Resolve every syncobj handle before the first D3DKMT call. */
   uint32_t n_sig = 0;
   uint64_t retire;

   simple_mtx_lock(&ws->lock);
   for (uint32_t i = 0; i < n_in; ++i) {
      struct agx_d3dkmt_sync *s =
         agx_d3dkmt_sync_table_get(&ws->syncs, syncs[i].handle);
      if (s == NULL) {
         simple_mtx_unlock(&ws->lock);
         errno = EINVAL;
         goto out;
      }
      waits[i] = s->kmt;
      wait_values[i] = syncs[i].sync_type == DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ
                          ? syncs[i].timeline_value
                          : agx_d3dkmt_binary_wait_value(s);
   }

   for (uint32_t i = 0; i < n_out; ++i) {
      const struct drm_asahi_sync *src = &syncs[n_in + i];
      struct agx_d3dkmt_sync *e =
         agx_d3dkmt_sync_table_get(&ws->syncs, src->handle);
      if (e == NULL) {
         simple_mtx_unlock(&ws->lock);
         errno = EINVAL;
         goto out;
      }

      uint64_t v = src->sync_type == DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ
                      ? src->timeline_value
                      : agx_d3dkmt_binary_signal_value(e, NULL);

      signals[n_sig] = e->kmt;
      signal_values[n_sig] = v;
      if (v > e->last_request)
         e->last_request = v;
      n_sig++;
   }

   retire = ++ctx->fence_next;
   signals[n_sig] = ctx->fence;
   signal_values[n_sig] = retire;
   n_sig++;
   simple_mtx_unlock(&ws->lock);

   if (n_in > 0) {
      struct agx_kmt_waitsyncfromgpu wait = {
         .hContext = ctx->hContext,
         .ObjectCount = n_in,
         .ObjectHandleArray = waits,
         .MonitoredFenceValueArray = wait_values,
      };
      agx_kmt_status st = ws->fns.WaitForSynchronizationObjectFromGpu(&wait);
      if (!AGX_KMT_SUCCESS(st)) {
         ret = agx_d3dkmt_fail(st);
         agx_d3dkmt_mark_device_lost(ws, ctx);
         goto out;
      }
   }

   struct agx_kmt_submitcommand cmd = {
      .Commands = staging->va->addr,
      .CommandLength =
         (uint32_t)sizeof(struct agxw_dma_header) + submit->cmdbuf_size,
      .pPrivateDriverData = &priv,
      .PrivateDriverDataSize = sizeof(priv),
      .BroadcastContextCount = 1,
      .BroadcastContext = {ctx->hContext},
   };

   agx_kmt_status st = ws->fns.SubmitCommand(&cmd);
   if (!AGX_KMT_SUCCESS(st)) {
      ret = agx_d3dkmt_fail(st);
      agx_d3dkmt_mark_device_lost(ws, ctx);
      goto out;
   }

   struct agx_kmt_signalsyncfromgpu signal = {
      .hContext = ctx->hContext,
      .ObjectCount = n_sig,
      .ObjectHandleArray = signals,
      .MonitoredFenceValueArray = signal_values,
   };
   st = ws->fns.SignalSynchronizationObjectFromGpu(&signal);
   if (!AGX_KMT_SUCCESS(st)) {
      /* The DMA is already in flight but its completion signals were not
       * queued. Poison the context so staging can never be reused unsafely. */
      ret = agx_d3dkmt_fail(st);
      agx_d3dkmt_mark_completion_unknown(ws, ctx);
      goto out;
   }

   simple_mtx_lock(&ws->lock);
   ctx->staging_retire[staging_slot] = retire;
   ctx->staging_slot = (staging_slot + 1) % AGX_D3DKMT_STAGING_RING;
   simple_mtx_unlock(&ws->lock);
   ret = 0;

out:
   simple_mtx_lock(&ws->lock);
   assert(ctx->submit_refs == 1 && ws->active_submits > 0);
   ctx->submit_refs = 0;
   ws->active_submits--;
   simple_mtx_unlock(&ws->lock);
   free(waits);
   free(wait_values);
   free(signals);
   free(signal_values);
   return ret;
}

/* -------------------------------------------------------------------------
 * simple_ioctl: VM_CREATE, QUEUE_CREATE, QUEUE_DESTROY, GET_TIME.
 * ------------------------------------------------------------------------- */
static int
d3dkmt_reserve(struct agx_d3dkmt_winsys *ws, uint64_t base, uint64_t size)
{
   struct agx_kmt_reservegpuva reserve = {
      .hPagingQueue = ws->paging_queue,
      .BaseAddress = base,
      .MinimumAddress = base,
      .MaximumAddress = base + size,
      .Size = size,
      .ReservationType = AGX_KMT_GPUVA_RESERVE_NO_ACCESS,
   };

   agx_kmt_status st = ws->fns.ReserveGpuVirtualAddress(&reserve);
   if (!AGX_KMT_SUCCESS(st)) {
      fprintf(stderr, "D3DKMTReserveGpuVirtualAddress(0x%llx, 0x%llx) failed\n",
              (unsigned long long)base, (unsigned long long)size);
      return agx_d3dkmt_fail(st);
   }

   /*
    * CONTRACT.md SS4.3 item 2 decides that a non-zero BaseAddress is
    * MANDATORY, not advisory -- but flags it as the one place it wants a
    * hardware datum, because whether VidMm passes BaseAddress through rather
    * than overriding it is unproven. This check is the seam: if VidMm
    * relocates, exactly one call site fails loudly here instead of every GPU
    * address in the driver being silently wrong. Honeykrisp's VA layout is
    * not relocatable -- shader_base, the zero page at 2^32 and the printf
    * buffer at 2^36 are baked into precompiled shaders
    * (agx_device.c:580-600, LIBAGX_PRINTF_BUFFER_ADDRESS) -- so a driver that
    * cannot pin its own addresses cannot run at all, and saying so on the
    * first reservation is the cheapest possible discovery.
    */
   if (reserve.VirtualAddress != base) {
      fprintf(stderr,
              "D3DKMTReserveGpuVirtualAddress ignored the base hint "
              "(wanted 0x%llx, got 0x%llx); Honeykrisp's VA layout is fixed\n",
              (unsigned long long)base,
              (unsigned long long)reserve.VirtualAddress);
      errno = ENOTSUP;
      return -1;
   }

   return agx_d3dkmt_wait_paging(ws, reserve.PagingFenceValue);
}

static int
d3dkmt_vm_create(struct agx_device *dev, struct drm_asahi_vm_create *vm)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);

   /*
    * There is no VM object to create: CONTRACT.md SS5.1 makes the AGX VM the
    * D3DKMT device, which agx_d3dkmt_open_device() already made. What DOES
    * have to happen here, and can only happen here, is reserving the user VA
    * window: D3DKMTMapGpuVirtualAddress can only map inside a reservation,
    * and vm_create is the moment Honeykrisp has decided where its heaps go.
    *
    * vm->kernel_start/kernel_end are INPUTS on DRM -- userspace picks where
    * the kernel's slice sits. Under WDDM the miniport owns its own kernel VA
    * (it lives in TTBR1 and is not part of the WDDM VA space at all,
    * CONTRACT.md SS4.2), so they are advisory here and only the window below
    * kernel_start is claimed.
    */
   uint64_t base = dev->params.vm_start;
   uint64_t end = vm->kernel_start;

   if (end <= base) {
      errno = EINVAL;
      return -1;
   }

   /*
    * CONTRACT.md SS4.2: a single 16 KiB kernel/firmware page sits in the
    * MIDDLE of the user window. Reserving across it would ask VidMm for a
    * range that includes an address it has declared internal, so the window
    * is reserved as the one or two sub-ranges that avoid it.
    */
   struct agx_d3dkmt_va_range ranges[2];
   uint32_t n = agx_d3dkmt_va_split_carveout(base, end - base, ranges);
   if (n == 0) {
      errno = EINVAL;
      return -1;
   }

   for (uint32_t i = 0; i < n; ++i) {
      if (d3dkmt_reserve(ws, ranges[i].base, ranges[i].size)) {
         /* Roll back anything already taken so a retry is clean. */
         for (uint32_t j = 0; j < i; ++j) {
            struct agx_kmt_freegpuva f = {
               .hAdapter = ws->adapter,
               .BaseAddress = ranges[j].base,
               .Size = ranges[j].size,
            };
            ws->fns.FreeGpuVirtualAddress(&f);
         }
         return -1;
      }
   }

   ws->va_count = n;
   for (uint32_t i = 0; i < n; ++i)
      ws->va[i] = ranges[i];

   /*
    * CONTRACT.md SS3.3: AGX_BO_EXEC is a MAPPING protection, not an
    * allocation flag, and a bind op carries no execute bit -- so the winsys
    * derives it from the address. agx_open_device() has already chosen
    * shader_base and the 4 GiB USC heap above it (agx_device.c:596-612)
    * before it calls VM_CREATE, so this is the first and only place the
    * window is knowable.
    */
   ws->policy.usc_base = dev->shader_base;
   ws->policy.usc_size = 0x100000000ull;

   /* One VM per device; the id is never interpreted by the kernel here. */
   vm->vm_id = 1;
   return 0;
}

static int
d3dkmt_queue_create(struct agx_device *dev, struct drm_asahi_queue_create *q)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);
   int ret = -1;

   simple_mtx_lock(&ws->lock);

   uint32_t slot = 0;
   while (slot < ws->ctx_capacity && ws->ctxs[slot].hContext != 0)
      slot++;

   if (slot == ws->ctx_capacity) {
      /* A realloc would invalidate a ctx pointer held by submit(). */
      if (ws->active_submits != 0) {
         errno = EBUSY;
         goto out;
      }
      uint32_t cap = ws->ctx_capacity ? ws->ctx_capacity * 2 : 4;
      struct agx_d3dkmt_ctx *ctxs =
         realloc(ws->ctxs, (size_t)cap * sizeof(*ctxs));
      if (ctxs == NULL) {
         errno = ENOMEM;
         goto out;
      }
      memset(ctxs + ws->ctx_capacity, 0,
             (size_t)(cap - ws->ctx_capacity) * sizeof(*ctxs));
      ws->ctxs = ctxs;
      ws->ctx_capacity = cap;
   }

   struct agx_d3dkmt_ctx *ctx = &ws->ctxs[slot];
   memset(ctx, 0, sizeof(*ctx));

   /* Per-context completion fence, used only for staging-ring recycling. */
   struct agx_kmt_createsyncobj2 fence = {
      .hDevice = ws->device,
      .Info = {.Type = AGX_KMT_SYNC_MONITORED_FENCE},
   };
   agx_kmt_status st = ws->fns.CreateSynchronizationObject2(&fence);
   if (!AGX_KMT_SUCCESS(st)) {
      ret = agx_d3dkmt_fail(st);
      goto out;
   }

   struct agxw_context_private priv = {
      .version = AGXW_CONTEXT_PRIVATE_VERSION,
      .struct_size = sizeof(priv),
      .priority = q->priority,
      .usc_exec_base = q->usc_exec_base,
   };

   /* Pre-validate with the miniport's own validator rather than a private
    * copy of the rules, so a rejection here means the same thing it would
    * mean in the kernel (CONTRACT.md SS5.2). */
   if (agxw_context_private_validate(&priv, sizeof(priv), ws->policy.usc_base,
                                     ws->policy.usc_size) != 0) {
      struct agx_kmt_destroysyncobj d = {.hSyncObject = fence.hSyncObject};
      ws->fns.DestroySynchronizationObject(&d);
      errno = EINVAL;
      goto out;
   }

   struct agx_kmt_createcontextvirtual create = {
      .hDevice = ws->device,
      .NodeOrdinal = ws->policy.node_ordinal,
      .EngineAffinity = 1,
      .pPrivateDriverData = &priv,
      .PrivateDriverDataSize = sizeof(priv),
   };

   st = ws->fns.CreateContextVirtual(&create);
   if (!AGX_KMT_SUCCESS(st)) {
      struct agx_kmt_destroysyncobj d = {.hSyncObject = fence.hSyncObject};
      ws->fns.DestroySynchronizationObject(&d);
      ret = agx_d3dkmt_fail(st);
      goto out;
   }

   ctx->hContext = create.hContext;
   ctx->fence = fence.hSyncObject;
   ctx->fence_cpu =
      (const volatile uint64_t *)fence.Info.MonitoredFence
         .FenceValueCPUVirtualAddress;

   /* queue_id is a dense slot index, not the D3DKMT context handle: it is
    * stored in drm_asahi_submit::queue_id and looked up on every submit. */
   q->queue_id = slot + 1;
   ret = 0;

out:
   simple_mtx_unlock(&ws->lock);
   return ret;
}

static int
d3dkmt_queue_destroy(struct agx_device *dev, struct drm_asahi_queue_destroy *q)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);

   simple_mtx_lock(&ws->lock);
   struct agx_d3dkmt_ctx *ctx = ctx_get(ws, q->queue_id);
   if (ctx == NULL) {
      simple_mtx_unlock(&ws->lock);
      errno = EINVAL;
      return -1;
   }
   if (ctx->submit_refs != 0 || ctx->completion_unknown) {
      simple_mtx_unlock(&ws->lock);
      errno = EBUSY;
      return -1;
   }

   struct agx_d3dkmt_ctx local = *ctx;
   memset(ctx, 0, sizeof(*ctx));
   simple_mtx_unlock(&ws->lock);

   struct agx_kmt_destroycontext destroy = {.hContext = local.hContext};
   ws->fns.DestroyContext(&destroy);

   for (unsigned i = 0; i < AGX_D3DKMT_STAGING_RING; ++i) {
      if (local.staging[i] != NULL)
         agx_bo_unreference(dev, local.staging[i]);
   }

   struct agx_kmt_destroysyncobj d = {.hSyncObject = local.fence};
   ws->fns.DestroySynchronizationObject(&d);
   return 0;
}

/*
 * GET_TIME. The second and last escape.
 *
 * D3DKMTQueryClockCalibration exists and is the obvious WDDM-native candidate,
 * but it reports an engine tick whose relationship to AGX's
 * command_timestamp_frequency_hz is unspecified, and Honeykrisp converts this
 * value with exactly that frequency (agx_gpu_timestamp_to_ns, agx_device.h).
 * The contract routes it through the escape so the KMD answers from the same
 * timebase source that serves the ioctl today, which is the property that
 * actually matters (CONTRACT.md SS8).
 */
static int
d3dkmt_get_time(struct agx_device *dev, struct drm_asahi_get_time *t)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);

   return d3dkmt_escape(ws, AGXW_ESC_GET_TIME, t, AGXW_GET_TIME_SIZE);
}

static int
d3dkmt_simple_ioctl(struct agx_device *dev, unsigned cmd, void *req)
{
   switch (cmd) {
   case DRM_IOCTL_ASAHI_VM_CREATE:
      return d3dkmt_vm_create(dev, req);
   case DRM_IOCTL_ASAHI_QUEUE_CREATE:
      return d3dkmt_queue_create(dev, req);
   case DRM_IOCTL_ASAHI_QUEUE_DESTROY:
      return d3dkmt_queue_destroy(dev, req);
   case DRM_IOCTL_ASAHI_GET_TIME:
      return d3dkmt_get_time(dev, req);
   default:
      errno = EINVAL;
      return -1;
   }
}

const agx_device_ops_t agx_device_d3dkmt_ops = {
   .bo_alloc = d3dkmt_bo_alloc,
   .bo_bind = d3dkmt_bo_bind,
   .bo_mmap = d3dkmt_bo_mmap,
   .get_params = d3dkmt_get_params,
   .submit = d3dkmt_submit,
   .bo_bind_object = d3dkmt_bo_bind_object,
   .bo_unbind_object = d3dkmt_bo_unbind_object,
   .simple_ioctl = d3dkmt_simple_ioctl,
   .bo_munmap = d3dkmt_bo_munmap,
   .bo_free = d3dkmt_bo_free,
};

/* -------------------------------------------------------------------------
 * Adapter enumeration and device open.
 * ------------------------------------------------------------------------- */
/*
 * Is this D3DKMT adapter ours?
 *
 * CONTRACT.md SS2.3 is explicit that VendorId/DeviceId are NOT reliable here:
 * the adapter is ACPI-enumerated (ACPI\NTAS0023), there is no PCI config
 * space, and whether dxgkrnl even calls DxgkDdiGetVendorAndDevice for a
 * non-PCI adapter is unproven. The identity blob is the only supported
 * discriminator, and getting it wrong means claiming another vendor's GPU.
 */
static bool
query_identity(const struct agx_d3dkmt_fns *fns, agx_kmt_handle adapter,
               struct agx_d3dkmt_policy *policy,
               struct agxw_adapter_identity *out)
{
   struct agxw_adapter_identity blob;

   memset(&blob, 0, sizeof(blob));

   struct agx_kmt_queryadapterinfo query = {
      .hAdapter = adapter,
      .Type = AGX_KMT_QAITYPE_UMDRIVERPRIVATE,
      .pPrivateDriverData = &blob,
      .PrivateDriverDataSize = sizeof(blob),
   };

   if (!AGX_KMT_SUCCESS(fns->QueryAdapterInfo(&query)))
      return false;

   return agx_d3dkmt_parse_identity(&blob, sizeof(blob), policy, out) ==
          AGX_D3DKMT_OK;
}

bool
agx_d3dkmt_enumerate_adapters(struct agx_kmt_luid *out, uint32_t capacity,
                              uint32_t *count)
{
   struct agx_d3dkmt_fns fns;
   struct agx_kmt_adapterinfo *infos = NULL;
   uint32_t found = 0;

   *count = 0;

   if (!agx_d3dkmt_load(&fns))
      return false;

   struct agx_kmt_enumadapters2 e = {0};
   if (!AGX_KMT_SUCCESS(fns.EnumAdapters2(&e)))
      return false;
   if (e.NumAdapters == 0)
      return true;

   infos = calloc(e.NumAdapters, sizeof(*infos));
   if (infos == NULL)
      return false;

   e.pAdapters = infos;
   if (!AGX_KMT_SUCCESS(fns.EnumAdapters2(&e))) {
      free(infos);
      return false;
   }

   for (uint32_t i = 0; i < e.NumAdapters; ++i) {
      struct agx_d3dkmt_policy policy;
      struct agxw_adapter_identity id;

      /* A foreign adapter's UMDRIVERPRIVATE blob fails the magic/version/
       * contract-version check and is skipped. */
      if (!query_identity(&fns, infos[i].hAdapter, &policy, &id))
         continue;

      if (found < capacity)
         out[found] = infos[i].AdapterLuid;
      found++;
   }

   free(infos);
   *count = found;
   return true;
}

bool
agx_d3dkmt_open_device(struct agx_device *dev, const struct agx_kmt_luid *luid)
{
   struct agx_d3dkmt_winsys *ws = calloc(1, sizeof(*ws));
   if (ws == NULL)
      return false;

   if (!agx_d3dkmt_load(&ws->fns))
      goto fail_alloc;

   ws->policy = agx_d3dkmt_policy_default();
   ws->luid = *luid;

   struct agx_kmt_openadapterfromluid open = {.AdapterLuid = *luid};
   if (!AGX_KMT_SUCCESS(ws->fns.OpenAdapterFromLuid(&open)))
      goto fail_alloc;
   ws->adapter = open.hAdapter;

   struct agxw_adapter_identity identity;
   if (!query_identity(&ws->fns, ws->adapter, &ws->policy, &identity))
      goto fail_adapter;

   /* GET_PARAMS is an escape (CONTRACT.md SS8) and its payload never
    * changes, so it is fetched once here and agx_open_device()'s
    * ops.get_params() copies out of this. */
   ws->params_size = identity.params_blob_size;
   ws->params = malloc(ws->params_size);
   if (ws->params == NULL)
      goto fail_adapter;
   if (d3dkmt_escape(ws, AGXW_ESC_GET_PARAMS, ws->params, ws->params_size))
      goto fail_params;

   struct agx_kmt_createdevice create = {.hAdapter = ws->adapter};
   if (!AGX_KMT_SUCCESS(ws->fns.CreateDevice(&create)))
      goto fail_params;
   ws->device = create.hDevice;

   struct agx_kmt_createpagingqueue pq = {.hDevice = ws->device};
   if (!AGX_KMT_SUCCESS(ws->fns.CreatePagingQueue(&pq)))
      goto fail_device;
   ws->paging_queue = pq.hPagingQueue;
   ws->paging_fence = pq.hSyncObject;
   ws->paging_fence_cpu =
      (const volatile uint64_t *)pq.FenceValueCPUVirtualAddress;

   ws->wait_event = CreateEventW(NULL, TRUE /* manual reset */, FALSE, NULL);
   if (ws->wait_event == NULL)
      goto fail_paging;

   simple_mtx_init(&ws->lock, mtx_plain);
   agx_d3dkmt_pool_init(&ws->pool);
   agx_d3dkmt_sync_table_init(&ws->syncs);

   dev->hdev = ws;
   dev->fd = -1;
   dev->is_virtio = false;
   dev->ops = agx_device_d3dkmt_ops;
   return true; /* caller then runs agx_open_device() */

fail_paging:;
   struct agx_kmt_destroypagingqueue dpq = {.hPagingQueue = ws->paging_queue};
   ws->fns.DestroyPagingQueue(&dpq);
fail_device:;
   struct agx_kmt_destroydevice dd = {.hDevice = ws->device};
   ws->fns.DestroyDevice(&dd);
fail_params:
   free(ws->params);
fail_adapter:;
   struct agx_kmt_closeadapter ca = {.hAdapter = ws->adapter};
   ws->fns.CloseAdapter(&ca);
fail_alloc:
   free(ws);
   return false;
}

void
agx_d3dkmt_close_device(struct agx_device *dev)
{
   struct agx_d3dkmt_winsys *ws = agx_d3dkmt_ws(dev);
   if (ws == NULL)
      return;

   /* Adapter teardown is the forced-retirement boundary for a transfer
    * context or render context whose completion became unknowable. */
   if (ws->xfer_ctx != 0) {
      struct agx_kmt_destroycontext dc = {.hContext = ws->xfer_ctx};
      ws->fns.DestroyContext(&dc);
      ws->xfer_ctx = 0;
   }

   for (uint32_t i = 0; i < ws->ctx_capacity; ++i) {
      if (ws->ctxs[i].hContext == 0)
         continue;

      struct agx_kmt_destroycontext dc = {.hContext = ws->ctxs[i].hContext};
      ws->fns.DestroyContext(&dc);

      for (unsigned slot = 0; slot < AGX_D3DKMT_STAGING_RING; ++slot) {
         if (ws->ctxs[i].staging[slot] != NULL)
            agx_bo_unreference(dev, ws->ctxs[i].staging[slot]);
      }

      struct agx_kmt_destroysyncobj ds = {.hSyncObject = ws->ctxs[i].fence};
      ws->fns.DestroySynchronizationObject(&ds);
   }
   free(ws->ctxs);

   for (uint32_t i = 0; i < ws->va_count; ++i) {
      struct agx_kmt_freegpuva fva = {
         .hAdapter = ws->adapter,
         .BaseAddress = ws->va[i].base,
         .Size = ws->va[i].size,
      };
      ws->fns.FreeGpuVirtualAddress(&fva);
   }

   struct agx_kmt_destroypagingqueue dpq = {.hPagingQueue = ws->paging_queue};
   ws->fns.DestroyPagingQueue(&dpq);

   struct agx_kmt_destroydevice dd = {.hDevice = ws->device};
   ws->fns.DestroyDevice(&dd);

   struct agx_kmt_closeadapter ca = {.hAdapter = ws->adapter};
   ws->fns.CloseAdapter(&ca);

   if (ws->wait_event != NULL)
      CloseHandle(ws->wait_event);

   agx_d3dkmt_pool_finish(&ws->pool);
   agx_d3dkmt_sync_table_finish(&ws->syncs);
   simple_mtx_destroy(&ws->lock);
   free(ws->params);
   free(ws);

   dev->hdev = NULL;
}

struct agx_kmt_luid
agx_d3dkmt_device_luid(const struct agx_device *dev)
{
   const struct agx_d3dkmt_winsys *ws =
      (const struct agx_d3dkmt_winsys *)dev->hdev;

   return ws ? ws->luid : (struct agx_kmt_luid){0, 0};
}
