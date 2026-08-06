/*
 * Copyright 2026 Aurora Silicon
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

#include "decode.h"
#include "util/u_dynarray.h"

#include <xf86drm.h>

#include "agx_helpers.h"
#include "util/u_math.h"

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

/*
 * Fill a drm_asahi_cmd_render for a clear-only render segment. Mirrors
 * hk_queue.c's asahi_fill_vdm_command for the color-only, single-sampled,
 * single-layer case.
 */
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
         .z_store = true,
      };

      c->depth.base = seg->ds->bo->va->addr;

      /* Stride is in pages, biased, with the low bit set — the encoding
       * hk uses (hk_cmd_draw.c).
       */
      unsigned z_pages = z->layer_stride_B / AIL_PAGESIZE;
      c->depth.stride = ((z_pages - 1) << 14) | 1;

      const struct ail_layout *s = NULL;
      if (seg->ds->stencil_bo) {
         s = &seg->ds->stencil_layout;
         zls.s_load = !seg->stencil_cleared;
         zls.s_store = true;

         c->stencil.base = seg->ds->stencil_bo->va->addr;
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

   /* Sample positions. Irrelevant to a clear-only pass, but rasterizing
    * geometry with this left at zero hangs the GPU. 0x88 is the standard
    * single-sample centre.
    */
   c->ppp_multisamplectl = 0x88;

   agx_tilebuffer_set_drm_cmd(c, &seg->tib);
   c->samples = 1;
   c->sample_size_B = seg->tib.sample_size_B;
   c->layers = 1;

   const float tan_60 = 1.732051f;
   c->isp_merge_upper_x = fui(tan_60 / seg->width_px);
   c->isp_merge_upper_y = fui(tan_60 / seg->height_px);

   c->bg.usc = seg->bg_usc | 4;
   c->eot.usc = seg->eot_usc | 4;

   /* Partial-render programs can only run when the tiler spills mid-pass,
    * which requires geometry; a clear-only pass has none. Point the partial
    * slots at the main programs as placeholders. Real partial programs
    * (LOAD-based) arrive with draws in G4.
    */
   c->partial_bg.usc = seg->bg_usc | 4;
   c->partial_eot.usc = seg->eot_usc | 4;

   static_assert(sizeof(c->bg.rsrc_spec) == sizeof(seg->bg_counts),
                 "counts word must match rsrc_spec");
   memcpy(&c->bg.rsrc_spec, &seg->bg_counts, sizeof(seg->bg_counts));
   memcpy(&c->eot.rsrc_spec, &seg->eot_counts, sizeof(seg->eot_counts));
   memcpy(&c->partial_bg.rsrc_spec, &seg->bg_counts,
          sizeof(seg->bg_counts));
   memcpy(&c->partial_eot.rsrc_spec, &seg->eot_counts,
          sizeof(seg->eot_counts));

   /* The scissor array carries both the XY bounds and the depth clip range
    * that draws index into. Occlusion scratch must be valid memory whether
    * or not queries run (mirrors hk pointing at the device query heap).
    */
   c->isp_scissor_base = seg->scissor_base;
   c->isp_dbias_base = 0;
   c->isp_oclqry_base = device->oclqry_bo->va->addr;

   /* The whole point: with no geometry, every tile is "empty", and this
    * flag is what makes the hardware still run background (clear) +
    * end-of-tile (store) on each of them.
    */
   c->flags |= DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES;

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
      mesa_logi("  bg.usc=0x%llx eot.usc=0x%llx  %ux%u samples=%u",
                (unsigned long long)c->bg.usc, (unsigned long long)c->eot.usc,
                c->width_px, c->height_px, c->samples);
   }
}

static void STDMETHODCALLTYPE
d12_queue_ExecuteCommandLists(ID3D12CommandQueue *iface, UINT count,
                              ID3D12CommandList *const *lists)
{
   struct d12_queue *queue = d12_queue_from_iface(iface);
   struct d12_device *device = queue->device;
   struct agx_device *dev = &device->dev;

   simple_mtx_lock(&queue->lock);

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
         mesa_loge("ExecuteCommandLists: list %u not closed; dropped", i);
         continue;
      }

      if (list->segment_count == 0)
         continue;

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
            };
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

      /* In-sync on the queue's progress timeline (honoring earlier Wait()
       * transfers), out-sync advances it — the hk_queue scheme, so fence
       * Signal() transfers represent real GPU completion.
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

      struct drm_asahi_submit submit = {
         .queue_id = queue->agx_queue_id,
         .in_sync_count = 1,
         .out_sync_count = 1,
         .cmdbuf_size = payload_size,
         .syncs = (uint64_t)(uintptr_t)syncs,
         .cmdbuf = (uint64_t)(uintptr_t)payload,
      };

      /*
       * ASAHI_MESA_DEBUG=trace. Mesa's own decoder parsing our command
       * stream is the highest-information tool available for runtime
       * bugs -- it reads the USC words, descriptors and launch packets
       * the way the hardware does, rather than the way we believe we
       * wrote them. It was never wired up, which is why `trace` printed
       * nothing useful.
       */
      if (dev->debug & AGX_DBG_TRACE) {
         struct util_dynarray wrapped;
         util_dynarray_init(&wrapped, NULL);
         void *copy = util_dynarray_grow_bytes(&wrapped, 1, payload_size);
         memcpy(copy, payload, payload_size);
         agxdecode_drm_cmdbuf(dev->agxdecode, &dev->params, &wrapped, true);
         util_dynarray_fini(&wrapped);
         agxdecode_next_frame();
      }

      struct agx_submit_virt virt = {.ring_idx = 0};
      int ret = dev->ops.submit(dev, &submit, &virt);

      /*
       * EXPERIMENT (D3D12AGX_SYNC_SUBMIT), not a behaviour change.
       *
       * Nothing in this driver tracks whether a submitted list is still
       * executing: Reset rewinds the stream and re-records over the same
       * memory. test_render_pass_suspend_resume_opts_disabled submits 32
       * render commands that all reuse the identical stream addresses
       * (0x2ff7ec0000 / 0x2ff7ec002c, sixteen times each) and every one of
       * them times out, so "the CPU overwrites a control stream the VDM is
       * still walking" is a live explanation for the dVDM fault.
       *
       * Waiting here makes that impossible. If the hang survives a
       * synchronous submit it is not a reuse race and this can go.
       */
      if (!ret && getenv("D3D12AGX_SYNC_SUBMIT")) {
         uint64_t point = queue->timeline_value + 1;
         drmSyncobjTimelineWait(dev->fd, &queue->syncobj, &point, 1,
                                INT64_MAX,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
      }

      /* Read the geometry vertex output buffer back while the command
       * list -- and therefore its stream BO -- is still alive. Dumping it
       * at device teardown reads memory the list already freed, which
       * reports zeroes whatever the GPU did.
       */
      if (!ret && device->dbg_gs_vb) {
         /* The out-sync is timeline_value + 1; waiting on timeline_value
          * returns immediately on an already-signalled point and reads
          * the buffer before the GPU has touched it. */
         uint64_t point = queue->timeline_value + 1;
         drmSyncobjTimelineWait(dev->fd, &queue->syncobj, &point, 1,
                                INT64_MAX,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);

         const float *f = (const float *)device->dbg_gs_vb;
         unsigned n = MIN2(device->dbg_gs_vb_size / 4, 32);
         mesa_logi("geometry vertex buffer (%zu bytes):",
                   device->dbg_gs_vb_size);
         for (unsigned i = 0; i < n; i += 4) {
            mesa_logi("  slot %u: %8.4f %8.4f %8.4f %8.4f", i / 4, f[i],
                      f[i + 1], f[i + 2], f[i + 3]);
         }
         device->dbg_gs_vb = NULL;
      }

      if (!ret && device->dbg_tess_draw) {
         uint64_t point = queue->timeline_value + 1;
         drmSyncobjTimelineWait(dev->fd, &queue->syncobj, &point, 1,
                                INT64_MAX,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);

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
            mesa_logi("  tcs[%2u] %8.3f %8.3f %8.3f %8.3f  (0x%08x %08x "
                      "%08x %08x)",
                      i, t[i], t[i + 1], t[i + 2], t[i + 3], ti[i],
                      ti[i + 1], ti[i + 2], ti[i + 3]);
         }

         if (device->dbg_mesh_grid) {
            const uint32_t *g = (const uint32_t *)device->dbg_mesh_grid;
            const uint32_t *d = (const uint32_t *)device->dbg_mesh_disp;
            mesa_logi("  AS DispatchMesh grid: %u %u %u", d[0], d[1], d[2]);
            mesa_logi("  mesh indirect grid:   threads %u %u %u  local %u %u %u",
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
      if (ret) {
         mesa_loge("DRM_IOCTL_ASAHI_SUBMIT failed for list %u: %d -- device "
                   "marked removed", i, ret);
         /* Do NOT advance the timeline, and latch removal so a later
          * Signal cannot transfer the last good point into a fence and
          * report progress for work that never executed.
          */
         queue->device->removed = true;
      } else {
         queue->timeline_value++;

         /* The GPU now owns this recording's storage until the allocator
          * is Reset. Reset on the list must take fresh storage rather than
          * rewind it -- see d12_list_take_fresh_storage.
          */
         list->submitted = true;
         if (getenv("D3D12AGX_DUMP_SYNC")) {
            mesa_logi("SYNC submit: queue %p agxq %u  in=%llu out=%llu  "
                      "list %p stream_bo 0x%llx",
                      (void *)queue, queue->agx_queue_id,
                      (unsigned long long)(queue->timeline_value - 1),
                      (unsigned long long)queue->timeline_value, (void *)list,
                      (unsigned long long)list->stream_bo->va->addr);
         }
      }

      free(payload);
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

   /* Once a submission has failed, signalling would claim progress that
    * never happened. Report removal instead.
    */
   if (queue->device->removed)
      return DXGI_ERROR_DEVICE_REMOVED;

   simple_mtx_lock(&queue->lock);

   /* Forward current queue progress into the fence's timeline: the fence
    * point signals when everything currently on the queue completes.
    */
   if (getenv("D3D12AGX_DUMP_SYNC")) {
      mesa_logi("SYNC Signal: queue %p agxq %u  fence %p value %llu "
                "<- queue point %llu",
                (void *)queue, queue->agx_queue_id, (void *)fence,
                (unsigned long long)value,
                (unsigned long long)queue->timeline_value);
   }

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
   if (getenv("D3D12AGX_DUMP_SYNC")) {
      mesa_logi("SYNC Wait:   queue %p agxq %u  fence %p value %llu "
                "-> queue point %llu",
                (void *)queue, queue->agx_queue_id, (void *)fence,
                (unsigned long long)value, (unsigned long long)next);
   }

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
