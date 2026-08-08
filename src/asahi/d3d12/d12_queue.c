/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12CommandQueue on an asahi hardware queue.  DRM timeline syncobjs do
 * not support wait-before-signal: Honeykrisp therefore routes Vulkan submits
 * through Mesa's threaded-submit layer until every dependency is pending.
 * D3D12 needs the same property, plus non-monotonic fence values, so each
 * queue has a small ordered worker.  Wait, submit and Signal calls enqueue
 * immediately; the worker resolves waits, completes AGX work, and then
 * publishes fence signals in API order.
 */

#include "d12_private.h"

#include "decode.h"
#include "util/u_dynarray.h"

#include <xf86drm.h>

#include "agx_helpers.h"
#include "util/u_math.h"

#include <time.h>

enum d12_queue_op_type {
   D12_QUEUE_OP_WAIT,
   D12_QUEUE_OP_SUBMIT,
   D12_QUEUE_OP_SIGNAL,
};

struct d12_queue_op {
   struct d12_queue_op *next;
   enum d12_queue_op_type type;

   struct d12_fence *fence;
   uint64_t value;
   struct d12_fence_ticket ticket;

   struct d12_cmd_list *list;
   void *payload;
   size_t payload_size;
   unsigned nr_vdm;
   unsigned nr_cdm;
};

static void d12_queue_stop_worker(struct d12_queue *queue);

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

      d12_queue_stop_worker(queue);
      drmSyncobjDestroy(dev->fd, queue->syncobj);
      agx_destroy_command_queue(dev, queue->agx_queue_id);
      cnd_destroy(&queue->work_available);
      mtx_destroy(&queue->lock);
      d12_private_data_store_destroy(&queue->private_data);
      d12_device_release(queue->device);
      free(queue);
   }

   return refs;
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_queue, ID3D12CommandQueue,
                                d12_queue_from_iface)

static HRESULT STDMETHODCALLTYPE
d12_queue_GetDevice(ID3D12CommandQueue *iface, REFIID riid, void **out)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   return ID3D12Device_QueryInterface(&queue->device->iface, riid, out);
}

/*
 * Fill a drm_asahi_cmd_render for a clear-only render segment. Mirrors
 * hk_queue.c's asahi_fill_vdm_command for the color-only, single-layer case.
 */
static uint32_t
d12_default_sample_positions(unsigned nr_samples)
{
   switch (nr_samples) {
   case 1: return 0x88;
   case 2: return 0x44cc;
   case 4: return 0xeaa26e26;
   default: UNREACHABLE("invalid D3D12 sample count");
   }
}

static void
d12_fill_render_cmd(struct d12_device *device, struct d12_cmd_list *list,
                    const struct d12_segment *seg,
                    struct drm_asahi_cmd_render *c)
{
   memset(c, 0, sizeof(*c));

   c->vdm_ctrl_stream_base = list->stream_bo->va->addr + seg->stream_start;

   agx_pack(&c->ppp_ctrl, CR_PPP_CONTROL, cfg) {
      cfg.enable_w_clamp = true;
      cfg.fixed_point_format = 1;
   }

   c->width_px = seg->width_px;
   c->height_px = seg->height_px;

   /* Background object values: hk's base constant, with the stencil clear
    * value in the low byte.
    */
   c->isp_bgobjdepth = seg->isp_bgobjdepth;
   c->isp_bgobjvals = 0x300 | seg->stencil_clear;

   /*
    * Depth/stencil. The ZLS unit is fixed-function: unlike colour, there is
    * no background or end-of-tile program involved. Pointing it at the
    * buffers and setting the load/store bits is the entire configuration.
    *
    * z_load is the interesting bit. If the pass cleared depth, every tile
    * starts from isp_bgobjdepth and loading would overwrite that with stale
    * memory. If it did not, the application is continuing from depth it
    * rendered earlier, and NOT loading would silently discard it.
    */
   if (seg->ds) {
      const struct ail_layout *z = &seg->ds->layout;
      struct agx_zls zls = {
         .z_load = !seg->depth_cleared,
         /* READ_ONLY constrains draw writes, not an explicit clear. The draw
          * state masks those writes separately; a cleared aspect must still
          * be committed at the end of this pass. */
         .z_store = seg->depth_cleared ||
                    !(seg->dsv_flags & D3D12_DSV_FLAG_READ_ONLY_DEPTH),
      };

      c->depth.base = seg->ds_address;

      /* Stride is in pages, biased, with the low bit set — the encoding
       * hk uses (hk_cmd_draw.c).
       */
      unsigned z_pages = z->layer_stride_B / AIL_PAGESIZE;
      c->depth.stride = ((z_pages - 1) << 14) | 1;

      const struct ail_layout *s = NULL;
      if (seg->ds->stencil_bo) {
         s = &seg->ds->stencil_layout;
         zls.s_load = !seg->stencil_cleared;
         zls.s_store = seg->stencil_cleared ||
                       !(seg->dsv_flags &
                         D3D12_DSV_FLAG_READ_ONLY_STENCIL);

         c->stencil.base = seg->stencil_address;
         unsigned s_pages = s->layer_stride_B / AIL_PAGESIZE;
         c->stencil.stride = ((s_pages - 1) << 14) | 1;
      }

      struct agx_zls_control_packed zls_ctrl;
      agx_pack_zls_control(&zls_ctrl, z, s, &zls);
      static_assert(sizeof(c->zls_ctrl) == sizeof(zls_ctrl),
                    "zls_ctrl width must match the packed struct");
      memcpy(&c->zls_ctrl, &zls_ctrl, sizeof(zls_ctrl));

      /* The depth buffer may be larger than the colour target. */
      agx_pack(&c->isp_zls_pixels, CR_ISP_ZLS_PIXELS, cfg) {
         cfg.x = z->width_px;
         cfg.y = z->height_px;
      }

      /* Integer depth bias applies to unorm depth buffers, float bias to
       * floating-point ones.
       */
      if (z->format == PIPE_FORMAT_Z16_UNORM)
         c->flags |= DRM_ASAHI_RENDER_DBIAS_IS_INT;
   }

   /* Standard D3D sample positions, in the same packed spelling Honeykrisp
    * programs. Leaving this zero for a draw is a GPU-hang class bug.
    */
   unsigned samples = MAX2(seg->tib.nr_samples, 1);
   c->ppp_multisamplectl = d12_default_sample_positions(samples);

   agx_tilebuffer_set_drm_cmd(c, &seg->tib);
   c->samples = samples;
   c->sample_size_B = seg->tib.sample_size_B;
   c->layers = seg->layers;

   const float tan_60 = 1.732051f;
   c->isp_merge_upper_x = fui(tan_60 / seg->width_px);
   c->isp_merge_upper_y = fui(tan_60 / seg->height_px);

   c->bg.usc = seg->bg_usc | 4;
   c->eot.usc = seg->eot_usc | 4;

   c->partial_bg.usc = seg->partial_bg_usc | 4;
   c->partial_eot.usc = seg->partial_eot_usc | 4;

   c->sampler_heap = seg->sampler_heap;
   c->sampler_count = seg->sampler_count;

   static_assert(sizeof(c->bg.rsrc_spec) == sizeof(seg->bg_counts),
                 "counts word must match rsrc_spec");
   memcpy(&c->bg.rsrc_spec, &seg->bg_counts, sizeof(seg->bg_counts));
   memcpy(&c->eot.rsrc_spec, &seg->eot_counts, sizeof(seg->eot_counts));
   memcpy(&c->partial_bg.rsrc_spec, &seg->partial_bg_counts,
          sizeof(seg->partial_bg_counts));
   memcpy(&c->partial_eot.rsrc_spec, &seg->partial_eot_counts,
          sizeof(seg->partial_eot_counts));

   /* The scissor array carries both the XY bounds and the depth clip range
    * that draws index into. Occlusion scratch must be valid memory whether
    * or not queries run (mirrors hk pointing at the device query heap).
    */
   c->isp_scissor_base = seg->scissor_base;
   c->isp_dbias_base = 0;
   c->isp_oclqry_base = device->oclqry_bo->va->addr;

   if (seg->process_empty_tiles)
      c->flags |= DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES;

   if (seg->scratch_vs_bo) {
      c->flags |= DRM_ASAHI_RENDER_VERTEX_SCRATCH;
      c->vertex_helper.data = seg->scratch_vs_bo->va->addr;
      c->vertex_helper.cfg = seg->scratch_vs_preamble ? (1 << 16) : 0;
      c->vertex_helper.binary = agx_helper_program(&device->bg_eot);
   }

   if (seg->scratch_fs_bo) {
      c->fragment_helper.data = seg->scratch_fs_bo->va->addr;
      c->fragment_helper.cfg = seg->scratch_fs_preamble ? (1 << 16) : 0;
      c->fragment_helper.binary = agx_helper_program(&device->bg_eot);
   }

   /*
    * Every address this command hands the hardware, so a VDM fault can be
    * matched to the field that carried it. The fault we are chasing reads
    * 0x4000000000 -- exactly 1 << 38, a round number rather than a leaked
    * pointer, and not AGX_ZERO_PAGE_ADDRESS (1 << 32) -- from dVDM_SSD,
    * the unit that walks this control stream. Print the candidates in the
    * same run as the fault and match, exactly as the tessellation VA map
    * did for the last one.
    */
   if (getenv("D3D12AGX_DUMP_RENDER_CMD")) {
      mesa_logi("render cmd: vdm_base=0x%llx (stream_bo 0x%llx..0x%llx, "
                "seg %zu..%zu, dispatches=%u)",
                (unsigned long long)c->vdm_ctrl_stream_base,
                (unsigned long long)list->stream_bo->va->addr,
                (unsigned long long)(list->stream_bo->va->addr +
                                     list->stream_bo->size),
                seg->stream_start, seg->stream_end, seg->dispatches);
      mesa_logi("  depth=0x%llx stencil=0x%llx scissor=0x%llx oclqry=0x%llx",
                (unsigned long long)c->depth.base,
                (unsigned long long)c->stencil.base,
                (unsigned long long)c->isp_scissor_base,
                (unsigned long long)c->isp_oclqry_base);
      mesa_logi("  bg.usc=0x%llx eot.usc=0x%llx  %ux%ux%u samples=%u",
                (unsigned long long)c->bg.usc, (unsigned long long)c->eot.usc,
                c->width_px, c->height_px, c->layers, c->samples);
   }
}

static void
d12_queue_op_destroy(struct d12_queue_op *op)
{
   if (op->type == D12_QUEUE_OP_WAIT)
      d12_fence_ticket_fini(&op->ticket);
   if (op->fence)
      d12_fence_internal_unref(op->fence);
   if (op->list)
      ID3D12GraphicsCommandList_Release(&op->list->iface);
   free(op->payload);
   free(op);
}

static bool
d12_queue_enqueue_locked(struct d12_queue *queue, struct d12_queue_op *op)
{
   if (p_atomic_read(&queue->stop))
      return false;

   if (queue->op_tail)
      queue->op_tail->next = op;
   else
      queue->op_head = op;
   queue->op_tail = op;
   cnd_signal(&queue->work_available);
   return true;
}

static bool
d12_queue_process_submit(struct d12_queue *queue, struct d12_queue_op *op)
{
   struct d12_device *device = queue->device;
   struct agx_device *dev = &device->dev;

   /* ASAHI_MESA_DEBUG=trace: decode the exact command stream which will be
    * submitted, not a reconstructed view of it. */
   if (dev->debug & AGX_DBG_TRACE) {
      struct util_dynarray wrapped;
      util_dynarray_init(&wrapped, NULL);
      void *copy =
         util_dynarray_grow_bytes(&wrapped, 1, op->payload_size);
      memcpy(copy, op->payload, op->payload_size);
      agxdecode_drm_cmdbuf(dev->agxdecode, &dev->params, &wrapped, true);
      util_dynarray_fini(&wrapped);
      agxdecode_next_frame();
   }

   uint64_t in = queue->timeline_value;
   uint64_t out = in + 1;
   struct drm_asahi_sync syncs[2] = {
      {
         .sync_type = DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ,
         .handle = queue->syncobj,
         .timeline_value = in,
      },
      {
         .sync_type = DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ,
         .handle = queue->syncobj,
         .timeline_value = out,
      },
   };
   struct drm_asahi_submit submit = {
      .queue_id = queue->agx_queue_id,
      .in_sync_count = 1,
      .out_sync_count = 1,
      .cmdbuf_size = op->payload_size,
      .syncs = (uint64_t)(uintptr_t)syncs,
      .cmdbuf = (uint64_t)(uintptr_t)op->payload,
   };
   struct agx_submit_virt virt = {.ring_idx = 0};
   int ret = dev->ops.submit(dev, &submit, &virt);
   if (ret) {
      mesa_loge("DRM_IOCTL_ASAHI_SUBMIT failed: %d -- device marked removed",
                ret);
      device->removed = true;
      device->removed_reason = DXGI_ERROR_DEVICE_REMOVED;
      return false;
   }

   queue->timeline_value = out;
   if (getenv("D3D12AGX_DUMP_SYNC")) {
      mesa_logi("SYNC submit: queue %p agxq %u  in=%llu out=%llu  "
                "list %p stream_bo 0x%llx",
                (void *)queue, queue->agx_queue_id,
                (unsigned long long)in, (unsigned long long)out,
                (void *)op->list,
                (unsigned long long)op->list->stream_bo->va->addr);
   }

   /* Serial completion is intentional.  It is the correctness-first D3D12
    * analogue of Mesa's threaded-submit dependency resolution: later Wait,
    * submit and Signal operations are not processed until this work really
    * completed, so cross-queue fence visibility and resource ordering are
    * exact even though Linux syncobjs cannot model D3D12 fence rewinds. */
   ret = drmSyncobjTimelineWait(dev->fd, &queue->syncobj, &out, 1,
                                INT64_MAX,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
   if (ret) {
      mesa_loge("queue GPU completion wait failed: %d", ret);
      device->removed = true;
      device->removed_reason = DXGI_ERROR_DEVICE_REMOVED;
      return false;
   }

   /* Preserve the existing opt-in geometry/tessellation diagnostics.  The
    * worker has just waited for this exact submission, so these pointers are
    * read while the recording and its stream BO are still retained. */
   if (device->dbg_gs_vb) {
      const float *f = (const float *)device->dbg_gs_vb;
      const uint32_t *u = (const uint32_t *)device->dbg_gs_vb;
      unsigned n = MIN2(device->dbg_gs_vb_size / 4, 32);
      mesa_logi("geometry vertex buffer (%zu bytes):", device->dbg_gs_vb_size);
      for (unsigned i = 0; i < n; i += 4) {
         mesa_logi("  slot %u: %8.4f %8.4f %8.4f %8.4f  "
                   "[%08x %08x %08x %08x]",
                   i / 4, f[i], f[i + 1], f[i + 2], f[i + 3], u[i],
                   u[i + 1], u[i + 2], u[i + 3]);
      }
      device->dbg_gs_vb = NULL;
   }

   if (device->dbg_tess_draw) {
      const uint32_t *d = (const uint32_t *)device->dbg_tess_draw;
      mesa_logi("tess out_draws: count=%u inst=%u first=%u bias=%u "
                "start_inst=%u",
                d[0], d[1], d[2], d[3], d[4]);

      const uint32_t *c = (const uint32_t *)device->dbg_tess_counts;
      for (unsigned i = 0; i < MIN2(device->dbg_tess_counts_n, 8); i++)
         mesa_logi("  counts[%u] = %u", i, c[i]);

      const float *t = (const float *)device->dbg_tcs_buf;
      const uint32_t *ti = (const uint32_t *)device->dbg_tcs_buf;
      for (unsigned i = 0; i < MIN2(device->dbg_tcs_buf_n, 140); i += 4) {
         mesa_logi("  tcs[%2u] %8.3f %8.3f %8.3f %8.3f  "
                   "(0x%08x %08x %08x %08x)",
                   i, t[i], t[i + 1], t[i + 2], t[i + 3], ti[i],
                   ti[i + 1], ti[i + 2], ti[i + 3]);
      }

      if (device->dbg_mesh_grid) {
         const uint32_t *g = (const uint32_t *)device->dbg_mesh_grid;
         const uint32_t *disp = (const uint32_t *)device->dbg_mesh_disp;
         mesa_logi("  AS DispatchMesh grid: %u %u %u", disp[0], disp[1],
                   disp[2]);
         mesa_logi("  mesh indirect grid: threads %u %u %u local %u %u %u",
                   g[0], g[1], g[2], g[3], g[4], g[5]);
         device->dbg_mesh_grid = NULL;
      }

      if (device->dbg_mesh_verts) {
         const float *v = (const float *)device->dbg_mesh_verts;
         unsigned stride_f = (32 * 16) / 4;
         for (unsigned i = 0; i < 4; i++) {
            const float *p = v + i * stride_f;
            mesa_logi("  mesh vtx %u: %8.4f %8.4f %8.4f %8.4f", i, p[0],
                      p[1], p[2], p[3]);
         }
         const uint32_t *ix = (const uint32_t *)device->dbg_mesh_idx;
         mesa_logi("  mesh indices: %u %u %u / %u %u %u", ix[0], ix[1],
                   ix[2], ix[3], ix[4], ix[5]);
         device->dbg_mesh_verts = NULL;
      }
      device->dbg_tess_draw = NULL;
   }

   return true;
}

static int
d12_queue_worker(void *data)
{
   struct d12_queue *queue = data;

   for (;;) {
      mtx_lock(&queue->lock);
      while (!queue->op_head && !p_atomic_read(&queue->stop))
         cnd_wait(&queue->work_available, &queue->lock);

      if (p_atomic_read(&queue->stop)) {
         mtx_unlock(&queue->lock);
         break;
      }

      struct d12_queue_op *op = queue->op_head;
      queue->op_head = op->next;
      if (!queue->op_head)
         queue->op_tail = NULL;
      mtx_unlock(&queue->lock);

      switch (op->type) {
      case D12_QUEUE_OP_WAIT:
         if (getenv("D3D12AGX_DUMP_SYNC"))
            mesa_logi("SYNC Wait worker: queue %p fence %p value %llu",
                      (void *)queue, (void *)op->fence,
                      (unsigned long long)op->value);
         d12_fence_ticket_wait(&op->ticket, &queue->stop);
         break;
      case D12_QUEUE_OP_SUBMIT:
         d12_queue_process_submit(queue, op);
         break;
      case D12_QUEUE_OP_SIGNAL:
         if (getenv("D3D12AGX_DUMP_SYNC"))
            mesa_logi("SYNC Signal worker: queue %p fence %p value %llu",
                      (void *)queue, (void *)op->fence,
                      (unsigned long long)op->value);
         d12_fence_signal_value(op->fence, op->value);
         break;
      }

      d12_queue_op_destroy(op);
   }

   return 0;
}

static void
d12_queue_stop_worker(struct d12_queue *queue)
{
   if (!queue->worker_started)
      return;

   p_atomic_set(&queue->stop, 1);
   mtx_lock(&queue->lock);
   cnd_broadcast(&queue->work_available);
   mtx_unlock(&queue->lock);
   thrd_join(queue->worker, NULL);
   queue->worker_started = false;

   struct d12_queue_op *op = queue->op_head;
   while (op) {
      struct d12_queue_op *next = op->next;
      d12_queue_op_destroy(op);
      op = next;
   }
   queue->op_head = queue->op_tail = NULL;
}

static void STDMETHODCALLTYPE
d12_queue_ExecuteCommandLists(ID3D12CommandQueue *iface, UINT count,
                              ID3D12CommandList *const *lists)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   struct d12_device *device = queue->device;

   mtx_lock(&queue->lock);

   for (UINT i = 0; i < count; i++) {
      /* Our lists are always ID3D12GraphicsCommandList objects. */
      struct d12_cmd_list *list = (struct d12_cmd_list *)lists[i];

      if (list && list->had_error) {
         /* Closed, but its stream is incomplete: recording failed part
          * way. Submitting it is not a wrong picture, it is undefined
          * work on the GPU -- which is how this bench wedged and cost a
          * reboot. */
         mesa_loge("ExecuteCommandLists: list %u errored while recording; "
                   "dropped", i);
         continue;
      }
      if (!list || list->state != D12_LIST_CLOSED) {
         /*
          * Submitting a list still being recorded is an APPLICATION error,
          * and D3D12 answers it by removing the device with
          * DXGI_ERROR_INVALID_CALL -- not by ignoring the list. Dropping it
          * silently let the application carry on believing the work was
          * queued.
          */
         mesa_loge("ExecuteCommandLists: list %u not closed; device removed",
                   i);
         queue->device->removed = true;
         queue->device->removed_reason = DXGI_ERROR_INVALID_CALL;
         continue;
      }

      if (list->segment_count == 0)
         continue;

      if (!d12_cmd_list_refresh_descriptors(list)) {
         mesa_loge("ExecuteCommandLists: descriptor refresh for list %u "
                   "references invalid command-list storage; dropped",
                   i);
         continue;
      }

      /* All of a list's segments go in one submission, ordered by the
       * per-command barrier fields exactly as hk_queue does: each command
       * barriers on all previously-submitted commands of both types.
       */
      size_t max_payload =
         list->segment_count *
         (sizeof(struct drm_asahi_cmd_header) +
          MAX2(sizeof(struct drm_asahi_cmd_compute),
               sizeof(struct drm_asahi_cmd_render)));
      uint8_t *payload = malloc(max_payload);
      if (!payload) {
         mesa_loge("ExecuteCommandLists: out of memory");
         break;
      }

      size_t payload_size = 0;
      unsigned nr_vdm = 0, nr_cdm = 0;

      for (unsigned s = 0; s < list->segment_count; s++) {
         const struct d12_segment *seg = &list->segments[s];
         bool compute = seg->type == D12_SEG_CDM;

         if (getenv("D3D12AGX_DUMP_SEGS"))
            mesa_logi("seg %u: %s start=%zu end=%zu dispatches=%u", s,
                      compute ? "CDM" : "RENDER", seg->stream_start,
                      seg->stream_end, seg->dispatches);

         if (compute && seg->dispatches == 0)
            continue;

         struct drm_asahi_cmd_header header =
            agx_cmd_header(compute, nr_vdm, nr_cdm);
         memcpy(payload + payload_size, &header, sizeof(header));
         payload_size += sizeof(header);

         if (compute) {
            struct drm_asahi_cmd_compute cmd = {
               .cdm_ctrl_stream_base =
                  list->stream_bo->va->addr + seg->stream_start,
               .cdm_ctrl_stream_end =
                  list->stream_bo->va->addr + seg->stream_end,
               .sampler_heap = seg->sampler_heap,
               .sampler_count = seg->sampler_count,
            };
            if (seg->scratch_cs_bo) {
               cmd.helper.data = seg->scratch_cs_bo->va->addr;
               cmd.helper.cfg =
                  seg->scratch_cs_preamble ? (1 << 16) : 0;
               cmd.helper.binary = agx_helper_program(&device->bg_eot);
            }
            memcpy(payload + payload_size, &cmd, sizeof(cmd));
            payload_size += sizeof(cmd);
            nr_cdm++;
         } else {
            struct drm_asahi_cmd_render cmd;
            d12_fill_render_cmd(device, list, seg, &cmd);
            memcpy(payload + payload_size, &cmd, sizeof(cmd));
            payload_size += sizeof(cmd);
            nr_vdm++;
         }
      }

      if (!payload_size) {
         free(payload);
         continue;
      }

      struct d12_queue_op *op = calloc(1, sizeof(*op));
      if (!op) {
         mesa_loge("ExecuteCommandLists: out of memory queuing list %u", i);
         free(payload);
         break;
      }

      op->type = D12_QUEUE_OP_SUBMIT;
      op->list = list;
      op->payload = payload;
      op->payload_size = payload_size;
      op->nr_vdm = nr_vdm;
      op->nr_cdm = nr_cdm;

      /* Reset is legal as soon as ExecuteCommandLists returns.  Mark this
       * recording submitted before handing it back to the application, and
       * keep the list alive until the worker observes GPU completion. */
      ID3D12GraphicsCommandList_AddRef(&list->iface);
      list->submitted = true;
      if (!d12_queue_enqueue_locked(queue, op)) {
         d12_queue_op_destroy(op);
         break;
      }
   }

   mtx_unlock(&queue->lock);
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
   if (!fence_iface)
      return E_INVALIDARG;
   struct d12_fence *fence = d12_fence_from_iface(fence_iface);

   if (queue->device->removed)
      return DXGI_ERROR_DEVICE_REMOVED;
   if (fence->device != queue->device)
      return E_INVALIDARG;

   struct d12_queue_op *op = calloc(1, sizeof(*op));
   if (!op)
      return E_OUTOFMEMORY;
   op->type = D12_QUEUE_OP_SIGNAL;
   op->fence = fence;
   op->value = value;
   d12_fence_internal_ref(fence);

   if (getenv("D3D12AGX_DUMP_SYNC")) {
      mesa_logi("SYNC Signal enqueue: queue %p agxq %u fence %p value %llu",
                (void *)queue, queue->agx_queue_id, (void *)fence,
                (unsigned long long)value);
   }

   mtx_lock(&queue->lock);
   bool queued = d12_queue_enqueue_locked(queue, op);
   mtx_unlock(&queue->lock);
   if (!queued) {
      d12_queue_op_destroy(op);
      return DXGI_ERROR_DEVICE_REMOVED;
   }
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_queue_Wait(ID3D12CommandQueue *iface, ID3D12Fence *fence_iface,
               UINT64 value)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   if (!fence_iface)
      return E_INVALIDARG;
   struct d12_fence *fence = d12_fence_from_iface(fence_iface);
   if (fence->device != queue->device)
      return E_INVALIDARG;

   struct d12_queue_op *op = calloc(1, sizeof(*op));
   if (!op)
      return E_OUTOFMEMORY;
   op->type = D12_QUEUE_OP_WAIT;
   op->fence = fence;
   op->value = value;
   d12_fence_internal_ref(fence);
   d12_fence_ticket_init(fence, &op->ticket, value);

   if (getenv("D3D12AGX_DUMP_SYNC")) {
      mesa_logi("SYNC Wait enqueue: queue %p agxq %u fence %p value %llu",
                (void *)queue, queue->agx_queue_id, (void *)fence,
                (unsigned long long)value);
   }

   mtx_lock(&queue->lock);
   bool queued = d12_queue_enqueue_locked(queue, op);
   mtx_unlock(&queue->lock);
   if (!queued) {
      d12_queue_op_destroy(op);
      return DXGI_ERROR_DEVICE_REMOVED;
   }
   return S_OK;
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
   struct d12_queue *queue = d12_queue_from_iface(iface);
   struct timespec now;

   if (!gpu_timestamp || !cpu_timestamp)
      return E_POINTER;

   if (clock_gettime(CLOCK_MONOTONIC, &now))
      return E_FAIL;

   /* The GPU value is in the same calibrated domain used by EndQuery and
    * GetTimestampFrequency.  Linux has no Windows QPC ABI, so expose the
    * monotonic nanosecond clock as the paired CPU counter; callers use its
    * own frequency on this platform. */
   *cpu_timestamp = (uint64_t)now.tv_sec * 1000000000ull + now.tv_nsec;
   *gpu_timestamp = agx_get_gpu_timestamp(&queue->device->dev);
   return S_OK;
}

static D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE
d12_queue_GetDesc(ID3D12CommandQueue *iface)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   return queue->desc;
}

static const ID3D12CommandQueueVtbl d12_queue_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12CommandQueue_DEFAULTS,
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

   /* Materialize point 0 for the first real AGX submission. */
   uint64_t zero = 0;
   drmSyncobjTimelineSignal(dev->fd, &queue->syncobj, &zero, 1);

   queue->iface.lpVtbl = (ID3D12CommandQueueVtbl *)&d12_queue_vtbl;
   queue->refcount = 1;
   queue->device = device;
   queue->desc = *desc;

   /*
    * NodeMask 0 means "the one adapter" and D3D12 REPORTS it as 1 -- GetDesc
    * returns the normalised description, not the request. Echoing the
    * caller's zero is the same mistake GetDesc on a resource made with
    * MipLevels and Alignment: a field the runtime is expected to resolve,
    * handed back unresolved.
    */
   if (!queue->desc.NodeMask)
      queue->desc.NodeMask = 1;

   queue->timeline_value = 0;
   if (mtx_init(&queue->lock, mtx_plain) != thrd_success) {
      drmSyncobjDestroy(dev->fd, queue->syncobj);
      agx_destroy_command_queue(dev, queue->agx_queue_id);
      free(queue);
      return E_FAIL;
   }
   if (cnd_init(&queue->work_available) != thrd_success) {
      mtx_destroy(&queue->lock);
      drmSyncobjDestroy(dev->fd, queue->syncobj);
      agx_destroy_command_queue(dev, queue->agx_queue_id);
      free(queue);
      return E_FAIL;
   }
   d12_private_data_store_init(&queue->private_data);
   d12_device_add_ref(device);

   if (thrd_create(&queue->worker, d12_queue_worker, queue) != thrd_success) {
      d12_device_release(device);
      d12_private_data_store_destroy(&queue->private_data);
      cnd_destroy(&queue->work_available);
      mtx_destroy(&queue->lock);
      drmSyncobjDestroy(dev->fd, queue->syncobj);
      agx_destroy_command_queue(dev, queue->agx_queue_id);
      free(queue);
      return E_FAIL;
   }
   queue->worker_started = true;

   HRESULT hr = d12_queue_QueryInterface(&queue->iface, riid, out);
   d12_queue_Release(&queue->iface);
   return hr;
}
