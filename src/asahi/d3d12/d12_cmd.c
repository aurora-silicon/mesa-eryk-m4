/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12CommandAllocator and ID3D12GraphicsCommandList.
 *
 * A command list is an ordered sequence of hardware segments (see
 * d12_private.h): CDM compute streams for copies/dispatches, and RENDER
 * segments for clears. Encoding happens at record time, mirroring
 * Honeykrisp's internal-dispatch (hk_dispatch_precomp) and bg/eot
 * (hk_build_bg_eot) recipes. No CPU-side execution lies anywhere: every
 * recorded operation becomes real GPU work at ExecuteCommandLists.
 */

#include "d12_private.h"

#include "agx_helpers.h"
#include "agx_nir_texture.h"
#include "agx_usc.h"
#include "libagx_dgc.h"
#include "libagx_shaders.h"
#include "util/format/format_utils.h"
#include "util/format/u_format.h"
#include "util/u_math.h"

#define D12_STREAM_BO_SIZE (1024 * 1024)
#define D12_USC_BO_SIZE    (2 * 1024 * 1024)
/* Worst-case control words per dispatch or render prologue, with slack. */
#define D12_DISPATCH_STREAM_MARGIN 0x300

/*
 * ID3D12CommandAllocator. The real allocations live on the list for now;
 * the allocator is lifetime bookkeeping to honor the API shape.
 */

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

/*
 * Release recording storage parked by lists that were Reset while their
 * previous submission could still have been executing. Safe only where
 * D3D12 guarantees GPU completion: allocator Reset, and allocator destroy.
 */
static void
d12_alloc_free_retired(struct d12_cmd_allocator *alloc)
{
   util_dynarray_foreach(&alloc->retired_bos, struct agx_bo *, bo)
      agx_bo_unreference(&alloc->device->dev, *bo);

   util_dynarray_clear(&alloc->retired_bos);
}

static ULONG STDMETHODCALLTYPE
d12_alloc_Release(ID3D12CommandAllocator *iface)
{
   struct d12_cmd_allocator *alloc = d12_cmd_allocator_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&alloc->refcount);

   if (refs == 0) {
      d12_alloc_free_retired(alloc);
      util_dynarray_fini(&alloc->retired_bos);
      d12_private_data_store_destroy(&alloc->private_data);
      d12_device_release(alloc->device);
      free(alloc);
   }

   return refs;
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_alloc, ID3D12CommandAllocator,
                                d12_cmd_allocator_from_iface)

static HRESULT STDMETHODCALLTYPE
d12_alloc_GetDevice(ID3D12CommandAllocator *iface, REFIID riid, void **out)
{
   struct d12_cmd_allocator *alloc = d12_cmd_allocator_from_iface(iface);
   return ID3D12Device_QueryInterface(&alloc->device->iface, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_Reset(ID3D12CommandAllocator *iface)
{
   struct d12_cmd_allocator *alloc = d12_cmd_allocator_from_iface(iface);

   /* D3D12 requires this to FAIL while any list built from the allocator
    * is still recording. Returning S_OK regardless hides a real
    * application bug and corrupts whatever that list goes on to emit.
    */
   if (alloc->recording_lists) {
      mesa_loge("CommandAllocator::Reset with %u list(s) still recording",
                alloc->recording_lists);
      return E_FAIL;
   }

   /*
    * THIS is the point D3D12 guarantees the GPU is finished with everything
    * recorded from this allocator -- the application is required to have
    * waited before calling it. Release the recording storage parked here by
    * lists that were Reset while still in flight.
    */
   d12_alloc_free_retired(alloc);

   return S_OK;
}

static const ID3D12CommandAllocatorVtbl d12_alloc_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12CommandAllocator_DEFAULTS,
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
   *out = NULL;

   switch (type) {
   case D3D12_COMMAND_LIST_TYPE_DIRECT:
   case D3D12_COMMAND_LIST_TYPE_BUNDLE:
   case D3D12_COMMAND_LIST_TYPE_COMPUTE:
   case D3D12_COMMAND_LIST_TYPE_COPY:
      break;
   default:
      mesa_loge("CreateCommandAllocator: invalid type %d", (int)type);
      return E_INVALIDARG;
   }

   struct d12_cmd_allocator *alloc = calloc(1, sizeof(*alloc));
   if (!alloc)
      return E_OUTOFMEMORY;

   alloc->iface.lpVtbl = (ID3D12CommandAllocatorVtbl *)&d12_alloc_vtbl;
   alloc->refcount = 1;
   alloc->device = device;
   alloc->type = type;
   d12_private_data_store_init(&alloc->private_data);
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

enum d12_scratch_stage {
   D12_SCRATCH_CS,
   D12_SCRATCH_VS,
   D12_SCRATCH_FS,
};

/*
 * Reserve the device-wide scratch pool and retain the exact resulting BO in
 * the segment. agx_scratch_alloc may grow the pool by replacing its BO; a
 * segment reference keeps the old mapping alive if another command list grows
 * the pool after this one has been recorded.
 */
static bool
d12_segment_reserve_scratch(struct d12_cmd_list *list,
                            struct d12_segment *seg,
                            enum d12_scratch_stage stage,
                            const struct agx_shader_info *info)
{
   unsigned size = MAX2(info->scratch_size, info->preamble_scratch_size);
   if (!size)
      return true;

   struct d12_device *device = list->device;
   struct agx_scratch *scratch;
   struct agx_bo **segment_bo;
   bool *segment_preamble;

   switch (stage) {
   case D12_SCRATCH_CS:
      scratch = &device->scratch_cs;
      segment_bo = &seg->scratch_cs_bo;
      segment_preamble = &seg->scratch_cs_preamble;
      break;
   case D12_SCRATCH_VS:
      scratch = &device->scratch_vs;
      segment_bo = &seg->scratch_vs_bo;
      segment_preamble = &seg->scratch_vs_preamble;
      break;
   case D12_SCRATCH_FS:
      scratch = &device->scratch_fs;
      segment_bo = &seg->scratch_fs_bo;
      segment_preamble = &seg->scratch_fs_preamble;
      break;
   default:
      UNREACHABLE("invalid d3d12 scratch stage");
   }

   simple_mtx_lock(&device->scratch_lock);
   agx_scratch_alloc(scratch, size, 0);

   if (!scratch->buf) {
      simple_mtx_unlock(&device->scratch_lock);
      mesa_loge("failed to allocate %u dwords of shader scratch", size);
      list->state = D12_LIST_ERROR;
      return false;
   }

   if (*segment_bo != scratch->buf) {
      agx_bo_reference(scratch->buf);
      if (*segment_bo)
         agx_bo_unreference(&device->dev, *segment_bo);
      *segment_bo = scratch->buf;
   }
   *segment_preamble |= info->preamble_scratch_size > 0;
   simple_mtx_unlock(&device->scratch_lock);

   if (getenv("D3D12AGX_DUMP_SCRATCH"))
      mesa_logi("scratch stage=%u size=%u preamble=%u bo=0x%llx (%zu B)",
                stage, info->scratch_size, info->preamble_scratch_size,
                (unsigned long long)(*segment_bo)->va->addr,
                (*segment_bo)->size);

   return true;
}

static void
d12_list_release_segment_scratch(struct d12_cmd_list *list,
                                 struct d12_cmd_allocator *retire_to)
{
   for (unsigned i = 0; i < list->segment_count; i++) {
      struct d12_segment *seg = &list->segments[i];
      struct agx_bo **bos[] = {
         &seg->scratch_cs_bo,
         &seg->scratch_vs_bo,
         &seg->scratch_fs_bo,
      };

      for (unsigned j = 0; j < ARRAY_SIZE(bos); j++) {
         if (!*bos[j])
            continue;

         if (retire_to) {
            util_dynarray_append_typed(&retire_to->retired_bos,
                                       struct agx_bo *, *bos[j]);
         } else {
            agx_bo_unreference(&list->device->dev, *bos[j]);
         }
         *bos[j] = NULL;
      }
   }
}

static void
d12_list_begin_recording(struct d12_cmd_list *list)
{
   d12_list_release_segment_scratch(list, NULL);
   list->stream_cur = d12_list_stream_map(list);
   list->data_floor = D12_STREAM_BO_SIZE;
   list->usc_offset = 0;
   list->segment_count = 0;
   list->cdm_open = false;
   list->render_open = false;
   list->pregfx_open = false;
   list->pregfx_cur = NULL;
   /* D3D12 has no default topology, but a list that draws without setting
    * one used to get triangles implicitly; keep that rather than making
    * every existing draw path set it.
    */
   list->topology = MESA_PRIM_TRIANGLES;
   list->patch_control_points = 0;
   memset(list->so, 0, sizeof(list->so));

   list->uses_heap = false;
   list->active_occlusion_queries = 0;
   list->active_pipeline_stats_heap = NULL;
   memset(&list->pipeline_stats, 0, sizeof(list->pipeline_stats));
   memset(list->active_so_stats_heap, 0,
          sizeof(list->active_so_stats_heap));

   /* The real free is the GPU write Close appends (see there): a closed
    * list can be executed many times without ever being Reset, so a reset
    * hook cannot be the only thing that frees the heap. This CPU zero
    * stays as a floor for the first recording, when nothing has been
    * submitted yet and no GPU write is in flight to race with.
    */
   if (list->device->heap_desc_cpu)
      list->device->heap_desc_cpu->bottom = 0;

   list->state = D12_LIST_RECORDING;

   /* Bindings do not survive a reset: a stale render target or depth buffer
    * here would let the next recording draw into last frame's texture.
    */
   memset(list->bound_rtv, 0, sizeof(list->bound_rtv));
   memset(&list->bound_dsv, 0, sizeof(list->bound_dsv));
   list->bound_rtv_count = 0;
   list->has_bound_dsv = false;
   list->stencil_ref = 0;
   for (unsigned i = 0; i < 4; i++)
      list->blend_factor[i] = 1.0f;
   list->compute_rs = NULL;
   list->graphics_rs = NULL;
   memset(list->compute_root_table, 0, sizeof(list->compute_root_table));
   memset(list->graphics_root_table, 0, sizeof(list->graphics_root_table));
   memset(list->compute_root_source, 0, sizeof(list->compute_root_source));
   memset(list->graphics_root_source, 0,
          sizeof(list->graphics_root_source));
   memset(list->compute_sampler_table, 0,
          sizeof(list->compute_sampler_table));
   memset(list->graphics_sampler_table, 0,
          sizeof(list->graphics_sampler_table));
   util_dynarray_clear(&list->descriptor_fixups);
   util_dynarray_clear(&list->indirect_constant_fixups);
   util_dynarray_clear(&list->indirect_argument_fixups);

   /* Points into the stream BO, whose data floor has just been rewound, so
    * the old address is about to be handed out to somebody else.
    */
   list->null_buffer = 0;
   list->discard_buffer = 0;
   list->null_counters = 0;
}

/* Upload a data blob into the top of the stream BO; returns its GPU VA. */
static uint64_t
d12_list_upload(struct d12_cmd_list *list, const void *data, size_t size,
                size_t align)
{
   /*
    * Check BEFORE subtracting. data_floor and size are size_t, so an
    * oversized upload makes `data_floor - size` wrap to a value near
    * SIZE_MAX -- which then sails past the `floor <= used + MARGIN` guard
    * (a huge floor looks like plenty of room) and memcpys far outside the
    * BO. The guard was load-bearing and could be skipped by the very case
    * it existed to catch.
    */
   if (size > list->data_floor) {
      mesa_loge("command list upload of %zu bytes exceeds data space",
                size);
      list->state = D12_LIST_ERROR;
      return 0;
   }

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

/* Reserve zeroed data space; returns its GPU address. */
static uint64_t
d12_list_upload_zero(struct d12_cmd_list *list, size_t size, size_t align)
{
   /* Same underflow as d12_list_upload -- check before subtracting. */
   if (size > list->data_floor) {
      mesa_loge("command list zero-reserve of %zu bytes exceeds data space",
                size);
      list->state = D12_LIST_ERROR;
      return 0;
   }

   size_t floor = (list->data_floor - size) & ~(align - 1);
   size_t used = list->stream_cur - d12_list_stream_map(list);

   if (floor <= used + D12_DISPATCH_STREAM_MARGIN) {
      mesa_loge("command list out of stream/data space");
      list->state = D12_LIST_ERROR;
      return 0;
   }

   memset(d12_list_stream_map(list) + floor, 0, size);
   list->data_floor = floor;
   return list->stream_bo->va->addr + floor;
}

/*
 * Zeroed memory that an unbound root slot can point at instead of NULL,
 * allocated once per recording and shared by every slot that needs it.
 *
 * 64 KiB is D3D12's own maximum constant-buffer size, so for a null CBV the
 * emulation is exact: every offset the API can legally address is covered.
 * A structured SRV/UAV indexed far enough past the end can still walk off
 * it -- covering that needs per-access bounds checking, not a bigger buffer
 * -- but it turns the common case from a guaranteed queue timeout into a
 * spec-correct read of zero.
 */
static uint64_t
d12_list_null_buffer(struct d12_cmd_list *list)
{
   if (!list->null_buffer)
      list->null_buffer = d12_list_upload_zero(list, 64 * 1024, 16);

   return list->null_buffer;
}

static uint64_t
d12_list_null_counters(struct d12_cmd_list *list)
{
   if (!list->null_counters) {
      list->null_counters = d12_list_upload_zero(
         list, D12_MAX_RESOURCE_BINDINGS * sizeof(uint32_t), 16);
   }

   return list->null_counters;
}

static uint64_t
d12_list_discard_buffer(struct d12_cmd_list *list)
{
   if (!list->discard_buffer)
      list->discard_buffer = d12_list_upload_zero(list, 16, 16);

   return list->discard_buffer;
}

/* CPU pointer for a GPU address previously returned by an upload. */
static uint8_t *
d12_list_data_cpu(struct d12_cmd_list *list, uint64_t gpu)
{
   if (!gpu)
      return NULL;
   return d12_list_stream_map(list) + (gpu - list->stream_bo->va->addr);
}

static bool
d12_list_stream_contains(const struct d12_cmd_list *list, uint64_t gpu,
                         size_t size)
{
   uint64_t base = list->stream_bo->va->addr;
   return gpu >= base && size <= list->stream_bo->size &&
          gpu - base <= list->stream_bo->size - size;
}

/* Allocate USC words; returns CPU pointer and the usc-space address. */
static uint8_t *
d12_list_alloc_usc(struct d12_cmd_list *list, size_t size, uint32_t *out_usc)
{
   size_t off = ALIGN_POT(list->usc_offset, 64);
   if (off + size > D12_USC_BO_SIZE) {
      mesa_loge("command list out of USC space");
      list->state = D12_LIST_ERROR;
      return NULL;
   }
   list->usc_offset = off + size;

   *out_usc =
      agx_usc_addr(&list->device->dev, list->usc_bo->va->addr + off);
   return (uint8_t *)agx_bo_map(list->usc_bo) + off;
}

static bool
d12_list_check_stream_space(struct d12_cmd_list *list)
{
   size_t used = list->stream_cur - d12_list_stream_map(list);
   if (used + D12_DISPATCH_STREAM_MARGIN > list->data_floor) {
      mesa_loge("command list out of control-stream space");
      list->state = D12_LIST_ERROR;
      return false;
   }
   return true;
}

static void d12_list_close_pregfx(struct d12_cmd_list *list);

/* Close whichever segment is open, terminating its control stream. */
static void
d12_list_close_cdm(struct d12_cmd_list *list)
{
   if (list->cdm_open) {
      struct d12_segment *seg = &list->segments[list->segment_count - 1];
      list->stream_cur =
         (uint8_t *)agx_cdm_terminate((uint32_t *)list->stream_cur);
      seg->stream_end = list->stream_cur - d12_list_stream_map(list);
      list->cdm_open = false;
   }

   if (list->render_open) {
      /* The pre-graphics stream belongs to this pass; it is meaningless
       * once the pass it precedes has ended.
       */
      d12_list_close_pregfx(list);

      struct d12_segment *seg = &list->segments[list->segment_count - 1];
      list->stream_cur =
         (uint8_t *)agx_vdm_terminate((uint32_t *)list->stream_cur);
      seg->stream_end = list->stream_cur - d12_list_stream_map(list);
      list->render_open = false;
   }
}

/* Size of one render pass's pre-graphics compute stream. Dispatches are
 * ~0x40 bytes each plus a barrier, so this holds a few hundred — far more
 * than a pass with geometry shaders in it will record before the segment
 * limit bites.
 */
#define D12_PREGFX_STREAM_SIZE (32 * 1024)

/*
 * Terminate the pre-graphics stream, if one is open. Called when the
 * render pass it belongs to ends, because the pair is what makes the
 * ordering meaningful: the compute segment sits immediately before the
 * render segment and nothing may be appended to it afterwards.
 */
static void
d12_list_close_pregfx(struct d12_cmd_list *list)
{
   if (!list->pregfx_open)
      return;

   struct d12_segment *seg = &list->segments[list->pregfx_seg];
   uint8_t *end = (uint8_t *)agx_cdm_terminate((uint32_t *)list->pregfx_cur);
   seg->stream_end = end - d12_list_stream_map(list);
   list->pregfx_open = false;
   list->pregfx_cur = NULL;
}

/*
 * Get the pre-graphics compute segment for the open render pass, creating
 * it on first use. Unlike d12_list_get_cdm this does NOT end the render
 * pass -- that is the entire point. See the comment on pregfx_cur in
 * d12_private.h.
 *
 * Returns NULL if there is no open render pass to precede, which is a
 * caller bug rather than a resource limit: without a render pass the
 * ordinary CDM path is correct and cheaper.
 */
static struct d12_segment *
d12_list_get_pregfx(struct d12_cmd_list *list)
{
   if (list->pregfx_open)
      return &list->segments[list->pregfx_seg];

   if (!list->render_open) {
      mesa_loge("pre-graphics compute requested with no open render pass");
      list->state = D12_LIST_ERROR;
      return NULL;
   }

   if (list->segment_count >= D12_MAX_SEGMENTS) {
      mesa_loge("command list segment limit reached");
      list->state = D12_LIST_ERROR;
      return NULL;
   }

   /* Carve the stream out of the data area. It is a control stream living
    * in data space, which the submit path does not care about: it reads
    * stream_start/stream_end and nothing else.
    */
   uint64_t gpu = d12_list_upload_zero(list, D12_PREGFX_STREAM_SIZE, 64);
   if (!gpu)
      return NULL;

   size_t offset = gpu - list->stream_bo->va->addr;

   /* Insert BEFORE the open render segment, which is by definition the
    * last one. Shifting it up by one keeps `render_open` meaning "the
    * last segment is an unterminated RENDER", which the close path and
    * every draw rely on.
    */
   unsigned at = list->segment_count - 1;
   list->segments[at + 1] = list->segments[at];
   list->segment_count++;

   struct d12_segment *seg = &list->segments[at];
   memset(seg, 0, sizeof(*seg));
   seg->type = D12_SEG_CDM;
   seg->stream_start = offset;

   list->pregfx_cur = d12_list_stream_map(list) + offset;
   list->pregfx_end = list->pregfx_cur + D12_PREGFX_STREAM_SIZE;
   list->pregfx_seg = at;
   list->pregfx_open = true;
   return seg;
}

/* Get an open CDM segment to append dispatches to, starting one if needed. */
static struct d12_segment *
d12_list_get_cdm(struct d12_cmd_list *list)
{
   if (list->cdm_open)
      return &list->segments[list->segment_count - 1];

   /* A render pass may still be open; compute work ends it. */
   if (list->render_open)
      d12_list_close_cdm(list);

   if (list->segment_count >= D12_MAX_SEGMENTS) {
      mesa_loge("command list segment limit reached");
      list->state = D12_LIST_ERROR;
      return NULL;
   }

   struct d12_segment *seg = &list->segments[list->segment_count++];
   memset(seg, 0, sizeof(*seg));
   seg->type = D12_SEG_CDM;
   seg->stream_start = list->stream_cur - d12_list_stream_map(list);
   list->cdm_open = true;
   return seg;
}

/*
 * Dispatch a precompiled libagx kernel, exactly as hk_dispatch_precomp does.
 */
static void
d12_list_dispatch_precomp_to(struct d12_cmd_list *list,
                             enum libagx_program idx, const void *args,
                             size_t args_size, struct agx_grid grid,
                             bool pre_gfx)
{
   struct d12_device *device = list->device;

   if (list->state != D12_LIST_RECORDING) {
      mesa_loge("recording into a non-recording command list");
      return;
   }

   /* Tessellation runs its libagx kernels ahead of the draw that consumes
    * them, so they go in the pre-graphics stream rather than ending the
    * render pass.
    */
   struct d12_segment *seg =
      pre_gfx ? d12_list_get_pregfx(list) : d12_list_get_cdm(list);
   if (!seg)
      return;

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

   uint32_t usc;
   uint8_t *usc_cpu = d12_list_alloc_usc(list, agx_usc_size(15), &usc);
   if (!usc_cpu)
      return;

   agx_usc_words_precomp((uint32_t *)usc_cpu, &prog->b, data_gpu, args_size);

   if (getenv("D3D12AGX_DUMP_USC")) {
      fprintf(stderr, "precomp USC @%u:", usc);
      for (unsigned i = 0; i < 32; i++)
         fprintf(stderr, " %02x", usc_cpu[i]);
      fprintf(stderr, "\n  data_gpu=0x%llx size=%zu\n",
              (unsigned long long)data_gpu, args_size);
   }

   uint8_t **cur = pre_gfx ? &list->pregfx_cur : &list->stream_cur;

   if (pre_gfx) {
      if (list->pregfx_cur + 0x100 > list->pregfx_end) {
         mesa_loge("pre-graphics stream full");
         list->state = D12_LIST_ERROR;
         return;
      }
   } else if (!d12_list_check_stream_space(list)) {
      return;
   }

   *cur = (uint8_t *)agx_cdm_launch((uint32_t *)*cur, device->dev.chip, grid,
                                    prog->b.workgroup, prog->b.launch, usc);
   *cur = (uint8_t *)agx_cdm_barrier((uint32_t *)*cur, device->dev.chip);
   seg->dispatches++;
}

static void
d12_list_dispatch_precomp(struct d12_cmd_list *list, enum libagx_program idx,
                          const void *args, size_t args_size,
                          struct agx_grid grid)
{
   d12_list_dispatch_precomp_to(list, idx, args, args_size, grid, false);
}

/*
 * Build the USC words for a background (clear) or end-of-tile (store)
 * program targeting one RGBA8 linear render target. Mirrors
 * hk_build_bg_eot for the single-RT, non-layered, single-sampled case.
 */
static uint32_t
d12_build_bg_eot_usc(struct d12_cmd_list *list, const struct d12_segment *seg,
                     const float color[4],
                     const enum agx_bg_eot_op ops[AGX_MAX_RENDER_TARGETS],
                     const struct agx_tilebuffer_layout *tib,
                     struct agx_counts_packed *out_counts)
{
   struct d12_device *device = list->device;

   uint32_t usc;
   size_t usc_size = agx_usc_size(3 + 8);
   uint8_t *cpu = d12_list_alloc_usc(list, usc_size, &usc);
   if (!cpu)
      return 0;

   struct agx_usc_builder b = agx_usc_builder(cpu, usc_size);
   struct agx_bg_eot_key key = {.tib = *tib};
   key.tib.layered = seg->layers > 1;
   unsigned uniforms = 0, nr_tex = 0;
   bool uses_txf = false;

   for (unsigned rt_index = 0; rt_index < AGX_MAX_RENDER_TARGETS; rt_index++) {
      enum agx_bg_eot_op op = ops[rt_index];
      struct d12_resource *rt = seg->rt[rt_index];
      uint64_t rt_address = seg->rt_address[rt_index];
      enum pipe_format type_format = seg->rt_view_format[rt_index];
      key.op[rt_index] = op;

      if (op == AGX_BG_EOT_NONE)
         continue;

      if (!rt) {
         mesa_loge("background/EOT operation requested for unbound RT%u",
                   rt_index);
         list->state = D12_LIST_ERROR;
         return 0;
      }

      if (op == AGX_BG_CLEAR) {

         /*
          * ClearRenderTargetView always hands over FLOAT[4], whatever the
          * target's type. For a PURE INTEGER target those floats are integer
          * VALUES, not normalized intensities, so they must be converted
          * rather than forwarded: clearing a UINT target to 1 has to write
          * the integer 1, where passing the float straight through wrote 255
          * ("Got 0xff00ff00, expected 0x01000100" in
          * test_clear_render_target_view).
          *
          * The format consulted here is the RESOURCE's, which is safe because
          * it is only being asked "is this integer-typed?". An earlier attempt
          * also drove the tilebuffer and PBE from a view format; those describe
          * MEMORY, so the store wrote the wrong size and produced 75 GPU
          * timeouts. Type interpretation and memory layout are different
          * questions -- do not merge them again.
          */
         /* The VIEW's format answers "what type is this", which the resource
          * format cannot for a TYPELESS target -- typeless maps to a float
          * sibling purely to fix the block size, so a UINT clear otherwise
          * looked like a float one.
          *
          * Measured, not assumed: view format 8 failures vs resource format 15
          * in test_clear_render_target_view.
          *
          * Used for THAT QUESTION ONLY. Memory layout still comes from the
          * resource; merging the two wrote the wrong size and cost 75 GPU
          * timeouts.
          */
         enum pipe_format cf =
            type_format != PIPE_FORMAT_NONE ? type_format : rt->layout.format;
         union {
            float f[4];
            uint32_t u[4];
            int32_t i[4];
         } cv;

         if (util_format_is_pure_uint(cf)) {
            for (unsigned c = 0; c < 4; c++)
               cv.u[c] = (uint32_t)color[c];
         } else if (util_format_is_pure_sint(cf)) {
            for (unsigned c = 0; c < 4; c++)
               cv.i[c] = (int32_t)color[c];
         } else {
            memcpy(cv.f, color, sizeof(cv.f));
         }

         /* 16 bytes of clear color, register base 4 — hk's fixed ABI */
         uint64_t colour = d12_list_upload(list, cv.f, 16, 16);
         if (!colour)
            return 0;

         agx_usc_uniform(&b, 4 + (8 * rt_index), 8, colour);
         uniforms = MAX2(uniforms, 4 + (8 * rt_index) + 8);
      } else if (op == AGX_BG_LOAD) {

         enum pipe_format pf =
            type_format != PIPE_FORMAT_NONE ? type_format : rt->layout.format;
         const struct util_format_description *fdesc =
            util_format_description(pf);
         struct agx_texture_packed desc;

         agx_pack(&desc, TEXTURE, cfg) {
            const unsigned samples = MAX2(rt->layout.sample_count_sa, 1);
            cfg.dimension =
               seg->layers > 1
                  ? (samples > 1
                        ? AGX_TEXTURE_DIMENSION_2D_ARRAY_MULTISAMPLED
                        : AGX_TEXTURE_DIMENSION_2D_ARRAY)
                  : (samples > 1 ? AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED
                                 : AGX_TEXTURE_DIMENSION_2D);
            cfg.layout = agx_translate_layout(rt->layout.tiling);
            cfg.channels = ail_pixel_format[pf].channels;
            cfg.type = ail_pixel_format[pf].type;
            cfg.srgb = util_format_is_srgb(pf);
            cfg.swizzle_r = agx_channel_from_pipe(fdesc->swizzle[0]);
            cfg.swizzle_g = agx_channel_from_pipe(fdesc->swizzle[1]);
            cfg.swizzle_b = agx_channel_from_pipe(fdesc->swizzle[2]);
            cfg.swizzle_a = agx_channel_from_pipe(fdesc->swizzle[3]);
            cfg.address = rt_address;
            cfg.width = rt->layout.width_px;
            cfg.height = rt->layout.height_px;
            cfg.first_level = 0;
            cfg.last_level = 0;
            cfg.unk_mipmapped = rt->layout.levels > 1;

            if (samples > 1)
               cfg.samples = agx_translate_sample_count(samples);

            if (rt->layout.tiling == AIL_TILING_LINEAR) {
               cfg.stride = ail_get_linear_stride_B(&rt->layout, 0) - 16;
               if (seg->layers > 1) {
                  cfg.depth_linear = seg->layers;
                  cfg.layer_stride_linear = rt->layout.layer_stride_B - 0x80;
                  cfg.extended = true;
               }
            } else {
               cfg.depth = seg->layers;
               cfg.page_aligned_layers = rt->layout.page_aligned_layers;
            }
         }

         uint64_t desc_ptr = d12_list_upload(list, &desc, sizeof(desc), 8);
         if (!desc_ptr)
            return 0;

         if (getenv("D3D12AGX_VALIDATE_MRT"))
            mesa_logi("MRT validate: BG_LOAD RT%u texture_slot=%u "
                      "desc=0x%llx",
                      rt_index, rt_index * 2,
                      (unsigned long long)desc_ptr);

         agx_usc_pack(&b, TEXTURE, cfg) {
            /* Background texture handles use the even half of eMRT's
             * two-slots-per-target ABI, matching agx_bg_eot.c.
             */
            cfg.start = rt_index * 2;
            cfg.count = 1;
            cfg.buffer = desc_ptr;
         }
         nr_tex = MAX2(nr_tex, (rt_index * 2) + 1);
         uses_txf = true;
      } else if (op == AGX_EOT_STORE) {

         /* EOT uses the view's channel interpretation but the resource's actual
          * memory layout. This is the single-layer, level-0 subset of
          * hk_image_view.c::pack_pbe.
          */
         struct agx_pbe_packed desc;
         agx_pack(&desc, PBE, cfg) {
            const unsigned samples = MAX2(rt->layout.sample_count_sa, 1);
            cfg.dimension =
               seg->layers > 1
                  ? (samples > 1
                        ? AGX_TEXTURE_DIMENSION_2D_ARRAY_MULTISAMPLED
                        : AGX_TEXTURE_DIMENSION_2D_ARRAY)
                  : (samples > 1 ? AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED
                                 : AGX_TEXTURE_DIMENSION_2D);
            cfg.layout = agx_translate_layout(rt->layout.tiling);
            enum pipe_format pf = type_format != PIPE_FORMAT_NONE
                                     ? util_format_linear(type_format)
                                     : rt->layout.format;
            const struct util_format_description *fdesc =
               util_format_description(pf);

            cfg.channels = ail_pixel_format[pf].channels;
            cfg.type = ail_pixel_format[pf].type;
            cfg.srgb = util_format_is_srgb(pf);

            /* Honour the format's channel order, so BGRA does not come out
             * with red and blue swapped.
             */
            cfg.swizzle_r = 0;
            cfg.swizzle_g = 1;
            cfg.swizzle_b = 2;
            cfg.swizzle_a = 3;
            for (unsigned c = 0; c < fdesc->nr_channels && c < 4; c++) {
               switch (fdesc->swizzle[c]) {
               case PIPE_SWIZZLE_X:
                  cfg.swizzle_r = c;
                  break;
               case PIPE_SWIZZLE_Y:
                  cfg.swizzle_g = c;
                  break;
               case PIPE_SWIZZLE_Z:
                  cfg.swizzle_b = c;
                  break;
               case PIPE_SWIZZLE_W:
                  cfg.swizzle_a = c;
                  break;
               default:
                  break;
               }
            }
            cfg.buffer = rt_address;
            cfg.width = rt->layout.width_px;
            cfg.height = rt->layout.height_px;
            cfg.level = 0;
            cfg.levels = rt->layout.levels;
            cfg.unk_mipmapped = rt->layout.levels > 1;
            if (samples > 1)
               cfg.samples = agx_translate_sample_count(samples);
            if (rt->layout.tiling == AIL_TILING_LINEAR) {
               cfg.stride = ail_get_linear_stride_B(&rt->layout, 0) - 4;
               if (seg->layers > 1) {
                  cfg.depth_linear = seg->layers;
                  cfg.layer_stride_linear = rt->layout.layer_stride_B - 0x80;
                  cfg.extended = true;
               } else {
                  cfg.layers = 1;
               }
            } else {
               cfg.layers = seg->layers;
               cfg.page_aligned_layers = rt->layout.page_aligned_layers;
            }
         }

         uint64_t desc_ptr = d12_list_upload(list, &desc, sizeof(desc), 8);
         if (!desc_ptr)
            return 0;

         if (getenv("D3D12AGX_VALIDATE_MRT"))
            mesa_logi("MRT validate: EOT_STORE RT%u pbe_slot=%u desc=0x%llx",
                      rt_index, rt_index,
                      (unsigned long long)desc_ptr);

         agx_usc_pack(&b, TEXTURE, cfg) {
            cfg.start = rt_index;
            cfg.count = 1;
            cfg.buffer = desc_ptr;
         }
         nr_tex = MAX2(nr_tex, rt_index + 1);
      }
   }

   if (uses_txf)
      agx_usc_push_packed(&b, SAMPLER, device->dev.txf_sampler);

   agx_usc_push_packed(&b, SHARED, &key.tib.usc);

   key.reserved_preamble = uniforms;
   struct agx_bg_eot_shader *shader =
      agx_get_bg_eot_shader(&device->bg_eot, &key);
   if (!shader) {
      mesa_loge("failed to build bg/eot shader");
      list->state = D12_LIST_ERROR;
      return 0;
   }

   agx_usc_pack(&b, SHADER, cfg) {
      cfg.code = agx_usc_addr(&device->dev, shader->ptr);
      cfg.unk_2 = 0;
   }

   agx_usc_pack(&b, REGISTERS, cfg)
      cfg.register_count = shader->info.nr_gprs;

   if (shader->info.has_preamble) {
      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code = agx_usc_addr(&device->dev,
                                 shader->ptr + shader->info.preamble_offset);
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
   }

   agx_pack(out_counts, COUNTS, cfg) {
      cfg.uniform_register_count = shader->info.push_count;
      cfg.preshader_register_count = shader->info.nr_preamble_gprs;
      cfg.texture_state_register_count = nr_tex;
      cfg.sampler_state_register_count =
         agx_translate_sampler_state_count(uses_txf ? 1 : 0, false);
   }

   if (getenv("D3D12AGX_VALIDATE_MRT"))
      mesa_logi("MRT validate: bg/eot ops ready nr_tex=%u uniforms=%u "
                "uses_txf=%u",
                nr_tex, uniforms, uses_txf);

   return usc;
}

static bool
d12_segment_matches_bindings(const struct d12_cmd_list *list,
                             const struct d12_segment *seg, bool use_rtv)
{
   const unsigned rtv_count = use_rtv ? list->bound_rtv_count : 0;
   const struct d12_descriptor *dsv =
      list->has_bound_dsv ? &list->bound_dsv : NULL;

   if (seg->rt_count != rtv_count ||
       seg->ds != (dsv ? dsv->resource : NULL))
      return false;

   for (unsigned i = 0; i < rtv_count; i++) {
      const struct d12_descriptor *rtv = &list->bound_rtv[i];
      if (seg->rt[i] != rtv->resource)
         return false;

      if (rtv->resource &&
          (seg->rt_address[i] != rtv->address ||
           seg->rt_view_format[i] != d12_format_to_pipe(rtv->format) ||
           seg->layers != MAX2(rtv->size_B, 1)))
         return false;
   }

   if (dsv &&
       (seg->dsv_flags != dsv->dsv_flags ||
        seg->ds_address != dsv->address ||
        seg->stencil_address != dsv->counter_address ||
        seg->layers != MAX2(dsv->size_B, 1)))
      return false;

   return true;
}

/*
 * Open a render segment from authoritative OM descriptors. D3D12 does not
 * require a clear before drawing, so AGX's main background program must LOAD
 * the target in that case. Partial renders always use their own LOAD/STORE
 * pair: reusing a CLEAR program after a tiler spill destroys earlier draws.
 */
static struct d12_segment *
d12_list_begin_render(struct d12_cmd_list *list,
                      const struct d12_descriptor *rtvs, unsigned rtv_count,
                      const struct d12_descriptor *dsv,
                      struct d12_pipeline_state *pso,
                      enum agx_bg_eot_op bg_op, const float color[4],
                      bool process_empty_tiles)
{
   d12_list_close_cdm(list);

   if (list->segment_count >= D12_MAX_SEGMENTS) {
      mesa_loge("command list segment limit reached");
      list->state = D12_LIST_ERROR;
      return NULL;
   }

   if (rtv_count > AGX_MAX_RENDER_TARGETS || (rtv_count && !rtvs)) {
      mesa_loge("render pass received %u RTV descriptors", rtv_count);
      list->state = D12_LIST_ERROR;
      return NULL;
   }

   if (dsv && (!dsv->resource || dsv->kind != D12_DESC_DSV)) {
      mesa_loge("render pass received an invalid DSV descriptor");
      list->state = D12_LIST_ERROR;
      return NULL;
   }

   struct d12_segment *seg = &list->segments[list->segment_count];
   memset(seg, 0, sizeof(*seg));
   seg->type = D12_SEG_RENDER;
   seg->process_empty_tiles = process_empty_tiles;
   seg->layers = 1;

   seg->rt_count = rtv_count;
   unsigned bound_rts = 0;
   unsigned rt_samples = 1;
   for (unsigned i = 0; i < rtv_count; i++) {
      const struct d12_descriptor *rtv = &rtvs[i];
      if (!rtv->resource)
         continue;

      if (rtv->kind != D12_DESC_RTV) {
         mesa_loge("render pass received an invalid RTV%u descriptor", i);
         list->state = D12_LIST_ERROR;
         return NULL;
      }

      enum pipe_format view_pf = d12_format_to_pipe(rtv->format);
      if (view_pf == PIPE_FORMAT_NONE || !ail_is_valid_pixel_format(view_pf) ||
          util_format_get_blocksize(view_pf) !=
             util_format_get_blocksize(rtv->resource->layout.format)) {
         mesa_loge("RTV%u format %u is incompatible with resource format %u",
                   i, (unsigned)rtv->format,
                   (unsigned)rtv->resource->layout.format);
         list->state = D12_LIST_ERROR;
         return NULL;
      }

      unsigned samples = MAX2(rtv->resource->layout.sample_count_sa, 1);
      if (bound_rts && samples != rt_samples) {
         mesa_loge("render targets have mismatched sample counts %u and %u",
                   rt_samples, samples);
         list->state = D12_LIST_ERROR;
         return NULL;
      }

      const unsigned layers = MAX2(rtv->size_B, 1);
      if (bound_rts && layers != seg->layers) {
         mesa_loge("render targets have mismatched layer counts %u and %u",
                   seg->layers, layers);
         list->state = D12_LIST_ERROR;
         return NULL;
      }

      seg->rt[i] = rtv->resource;
      seg->rt_address[i] = rtv->address ? rtv->address : rtv->resource->addr;
      seg->rt_view_format[i] = view_pf;

      if (getenv("D3D12AGX_VALIDATE_MRT")) {
         const uint64_t bo_start = rtv->resource->bo->va->addr;
         const uint64_t bo_end = bo_start + rtv->resource->bo->size;
         const uint64_t span =
            (uint64_t)rtv->resource->layout.layer_stride_B * layers;
         if (seg->rt_address[i] < bo_start || seg->rt_address[i] >= bo_end ||
             span > bo_end - seg->rt_address[i]) {
            mesa_loge("MRT validate: RT%u address 0x%llx span 0x%llx is "
                      "outside BO [0x%llx, 0x%llx)",
                      i, (unsigned long long)seg->rt_address[i],
                      (unsigned long long)span, (unsigned long long)bo_start,
                      (unsigned long long)bo_end);
            list->state = D12_LIST_ERROR;
            return NULL;
         }

         mesa_logi("MRT validate: RT%u addr=0x%llx span=0x%llx "
                   "bo=[0x%llx,0x%llx) format=%u samples=%u",
                   i, (unsigned long long)seg->rt_address[i],
                   (unsigned long long)span, (unsigned long long)bo_start,
                   (unsigned long long)bo_end, view_pf, samples);
      }
      rt_samples = samples;
      seg->layers = layers;
      if (!bound_rts) {
         seg->width_px = rtv->resource->layout.width_px;
         seg->height_px = rtv->resource->layout.height_px;
      } else {
         seg->width_px = MIN2(seg->width_px, rtv->resource->layout.width_px);
         seg->height_px = MIN2(seg->height_px,
                               rtv->resource->layout.height_px);
      }
      bound_rts++;
   }

   if (dsv) {
      seg->ds = dsv->resource;
      seg->ds_address = dsv->address;
      seg->stencil_address = dsv->counter_address;
      seg->dsv_flags = dsv->dsv_flags;
      const unsigned ds_layers = MAX2(dsv->size_B, 1);
      const unsigned ds_samples =
         MAX2(dsv->resource->layout.sample_count_sa, 1);
      if (bound_rts && ds_samples != rt_samples) {
         mesa_loge("render targets and depth/stencil have mismatched sample "
                   "counts %u and %u", rt_samples, ds_samples);
         list->state = D12_LIST_ERROR;
         return NULL;
      }
      if (bound_rts && ds_layers != seg->layers) {
         mesa_loge("render targets and depth/stencil have mismatched layer "
                   "counts %u and %u", seg->layers, ds_layers);
         list->state = D12_LIST_ERROR;
         return NULL;
      }
      if (!bound_rts)
         seg->layers = ds_layers;

      const uint64_t z_start = dsv->resource->bo->va->addr;
      const uint64_t z_end = z_start + dsv->resource->bo->size;
      const uint64_t z_span =
         (uint64_t)dsv->resource->layout.layer_stride_B * ds_layers;
      if (seg->ds_address < z_start || seg->ds_address >= z_end ||
          z_span > z_end - seg->ds_address) {
         mesa_loge("DSV depth address 0x%llx span 0x%llx is outside BO "
                   "[0x%llx, 0x%llx)",
                   (unsigned long long)seg->ds_address,
                   (unsigned long long)z_span,
                   (unsigned long long)z_start,
                   (unsigned long long)z_end);
         list->state = D12_LIST_ERROR;
         return NULL;
      }
      if (dsv->resource->stencil_bo) {
         const uint64_t s_start = dsv->resource->stencil_bo->va->addr;
         const uint64_t s_end = s_start + dsv->resource->stencil_bo->size;
         const uint64_t s_span =
            (uint64_t)dsv->resource->stencil_layout.layer_stride_B *
            ds_layers;
         if (seg->stencil_address < s_start ||
             seg->stencil_address >= s_end ||
             s_span > s_end - seg->stencil_address) {
            mesa_loge("DSV stencil address 0x%llx span 0x%llx is outside BO "
                      "[0x%llx, 0x%llx)",
                      (unsigned long long)seg->stencil_address,
                      (unsigned long long)s_span,
                      (unsigned long long)s_start,
                      (unsigned long long)s_end);
            list->state = D12_LIST_ERROR;
            return NULL;
         }
      }
      rt_samples = ds_samples;
      if (!seg->width_px) {
         seg->width_px = dsv->resource->layout.width_px;
         seg->height_px = dsv->resource->layout.height_px;
      } else if (dsv->resource->layout.width_px < seg->width_px ||
                 dsv->resource->layout.height_px < seg->height_px) {
         mesa_loge("depth/stencil target %ux%u is smaller than the render "
                   "target %ux%u", dsv->resource->layout.width_px,
                   dsv->resource->layout.height_px, seg->width_px,
                   seg->height_px);
         list->state = D12_LIST_ERROR;
         return NULL;
      }
   }

   if (!seg->width_px) {
      seg->width_px = list->has_viewport
                         ? MAX2((unsigned)ceilf(list->viewport.Width), 1)
                         : 1;
      seg->height_px = list->has_viewport
                          ? MAX2((unsigned)ceilf(list->viewport.Height), 1)
                          : 1;
   }

   if (pso) {
      if ((bound_rts || dsv) && pso->tib.nr_samples != rt_samples) {
         mesa_loge("render pass sample mismatch: PSO %u, attachments %u",
                   pso->tib.nr_samples, rt_samples);
         list->state = D12_LIST_ERROR;
         return NULL;
      }
      seg->tib = pso->tib;

      if (getenv("D3D12AGX_VALIDATE_MRT")) {
         for (unsigned i = 0; i < rtv_count; i++) {
            if (seg->rt[i] &&
                seg->tib.logical_format[i] != seg->rt_view_format[i]) {
               mesa_loge("MRT validate: RT%u PSO format %u != view format "
                         "%u", i, seg->tib.logical_format[i],
                         seg->rt_view_format[i]);
               list->state = D12_LIST_ERROR;
               return NULL;
            }
         }
      }
   } else {
      enum pipe_format formats[AGX_MAX_RENDER_TARGETS] = {0};
      for (unsigned i = 0; i < rtv_count; i++)
         formats[i] = seg->rt_view_format[i];
      seg->tib = agx_build_tilebuffer_layout(formats, rtv_count, rt_samples,
                                             seg->layers > 1);
   }

   seg->scissor_base = d12_list_upload_zero(
      list, D12_MAX_SCISSORS * AGX_SCISSOR_LENGTH, 64);
   if (!seg->scissor_base)
      return NULL;

   enum agx_bg_eot_op main_bg[AGX_MAX_RENDER_TARGETS] = {0};
   enum agx_bg_eot_op main_eot[AGX_MAX_RENDER_TARGETS] = {0};
   enum agx_bg_eot_op partial_bg[AGX_MAX_RENDER_TARGETS] = {0};
   enum agx_bg_eot_op partial_eot[AGX_MAX_RENDER_TARGETS] = {0};
   for (unsigned i = 0; i < rtv_count; i++) {
      if (!seg->rt[i])
         continue;
      main_bg[i] = bg_op;
      main_eot[i] = AGX_EOT_STORE;
      partial_bg[i] = AGX_BG_LOAD;
      partial_eot[i] = AGX_EOT_STORE;
   }

   seg->bg_usc = d12_build_bg_eot_usc(
      list, seg, color, main_bg, &seg->tib, &seg->bg_counts);
   seg->eot_usc = d12_build_bg_eot_usc(
      list, seg, NULL, main_eot, &seg->tib, &seg->eot_counts);
   seg->partial_bg_usc = d12_build_bg_eot_usc(
      list, seg, NULL, partial_bg, &seg->tib, &seg->partial_bg_counts);
   seg->partial_eot_usc = d12_build_bg_eot_usc(
      list, seg, NULL, partial_eot, &seg->tib, &seg->partial_eot_counts);
   if (!seg->bg_usc || !seg->eot_usc || !seg->partial_bg_usc ||
       !seg->partial_eot_usc)
      return NULL;

   if (!d12_list_check_stream_space(list))
      return NULL;

   seg->stream_start = list->stream_cur - d12_list_stream_map(list);
   agx_pack(list->stream_cur, VDM_BARRIER, cfg) {
      cfg.usc_cache_inval = true;
   }
   list->stream_cur += AGX_VDM_BARRIER_LENGTH;

   struct AGX_PPP_HEADER present = {
      .w_clamp = true,
      .occlusion_query_2 = true,
      .output_unknown = true,
      .varying_word_2 = true,
      .viewport_count = 1,
   };
   size_t size = agx_ppp_update_size(&present);
   uint64_t gpu = d12_list_upload_zero(list, size, AGX_PPP_HEADER_ALIGN);
   uint8_t *cpu = d12_list_data_cpu(list, gpu);
   if (!cpu)
      return NULL;

   struct agx_ppp_update ppp = agx_new_ppp_update(
      (struct agx_ptr){.cpu = cpu, .gpu = gpu}, size, &present);
   agx_ppp_push(&ppp, W_CLAMP, cfg)
      cfg.w_clamp = 1e-10;
   agx_ppp_push(&ppp, FRAGMENT_OCCLUSION_QUERY_2, cfg);
   agx_ppp_push(&ppp, OUTPUT_UNKNOWN, cfg);
   agx_ppp_push(&ppp, VARYING_2, cfg);
   agx_ppp_fini(&list->stream_cur, &ppp);

   list->segment_count++;
   list->render_open = true;
   return seg;
}


void
d12_refresh_pbe_descriptor(struct agx_pbe_packed *out,
                           const struct d12_descriptor *d)
{
   agx_set_null_pbe(out);

   if (d->kind != D12_DESC_UAV || !d->resource)
      return;

   struct d12_resource *res = d->resource;
   enum pipe_format pf = d12_format_to_pipe(d->format);
   if (pf == PIPE_FORMAT_NONE || !ail_is_valid_pixel_format(pf) ||
       !ail_pixel_format[pf].texturable ||
       util_format_is_depth_or_stencil(pf))
      return;

   const struct util_format_description *fdesc =
      util_format_description(pf);
   const unsigned blocksize = util_format_get_blocksize(pf);

   if (res->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      if (d->buffer_kind != D12_BUFFER_VIEW_TYPED || !blocksize ||
          !d->size_B)
         return;

      uint64_t base = d->address & ~0xfull;
      unsigned tail_B = d->address & 0xf;
      if (tail_B % blocksize)
         return;

      unsigned elements = d->size_B / blocksize;
      agx_pack(out, PBE, cfg) {
         cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
         cfg.layout = AGX_LAYOUT_LINEAR;
         cfg.channels = ail_pixel_format[pf].channels;
         cfg.type = ail_pixel_format[pf].type;
         cfg.srgb = util_format_is_srgb(pf);

         for (unsigned i = 0; i < fdesc->nr_channels && i < 4; i++) {
            if (fdesc->swizzle[i] == PIPE_SWIZZLE_X)
               cfg.swizzle_r = i;
            else if (fdesc->swizzle[i] == PIPE_SWIZZLE_Y)
               cfg.swizzle_g = i;
            else if (fdesc->swizzle[i] == PIPE_SWIZZLE_Z)
               cfg.swizzle_b = i;
            else if (fdesc->swizzle[i] == PIPE_SWIZZLE_W)
               cfg.swizzle_a = i;
         }

         cfg.buffer = base;
         cfg.buffer_offset_sw = tail_B / blocksize;
         cfg.width = AGX_TEXTURE_BUFFER_WIDTH;
         cfg.height = DIV_ROUND_UP(elements, cfg.width);
         cfg.level = 0;
         cfg.levels = 1;
         cfg.stride = (cfg.width * blocksize) - 4;
         cfg.layers = 1;
      }
      return;
   }

   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       !res->layout.size_B || !res->layout.writeable_image ||
       res->layout.tiling == AIL_TILING_LINEAR)
      return;

   const struct ail_layout *layout = &res->layout;
   const unsigned level = d->texture_level;
   const unsigned layers = MAX2(d->texture_layers, 1);
   const bool array = d->texture_array;

   agx_pack(out, PBE, cfg) {
      cfg.dimension = array ? AGX_TEXTURE_DIMENSION_2D_ARRAY
                            : AGX_TEXTURE_DIMENSION_2D;
      cfg.layout = agx_translate_layout(layout->tiling);
      cfg.channels = ail_pixel_format[pf].channels;
      cfg.type = ail_pixel_format[pf].type;
      cfg.srgb = util_format_is_srgb(pf);

      for (unsigned i = 0; i < fdesc->nr_channels && i < 4; i++) {
         if (fdesc->swizzle[i] == PIPE_SWIZZLE_X)
            cfg.swizzle_r = i;
         else if (fdesc->swizzle[i] == PIPE_SWIZZLE_Y)
            cfg.swizzle_g = i;
         else if (fdesc->swizzle[i] == PIPE_SWIZZLE_Z)
            cfg.swizzle_b = i;
         else if (fdesc->swizzle[i] == PIPE_SWIZZLE_W)
            cfg.swizzle_a = i;
      }

      cfg.buffer = d->address;
      cfg.width = layout->width_px;
      cfg.height = layout->height_px;
      cfg.level = level;
      cfg.levels = layout->levels;
      cfg.layers = layers;
      cfg.page_aligned_layers = layout->page_aligned_layers;

      /* PBE words 4/5 are software sideband when the architectural
       * descriptor is not extended. libagx_image_texel_address consumes
       * these fields for tiled image atomics. */
      if (!cfg.extended) {
         cfg.level_offset_sw = ail_get_level_offset_B(layout, level);
         cfg.tile_width_sw = layout->tilesize_el[level].width_el;
         cfg.tile_height_sw = layout->tilesize_el[level].height_el;
         cfg.layer_stride_sw = layout->layer_stride_B;
      }
   }
}

/*
 * Build the two tables a shader reaches its resources through, and return
 * the addresses to publish in uniform registers.
 *
 * - The ROOT TABLE is 64-bit addresses indexed by binding slot: buffers,
 *   read with one indirection.
 * - The TEXTURE HEAP is hardware texture descriptors at the same slot
 *   index scaled by the descriptor size. AGX does not reach a texture by
 *   address; it reads a descriptor, so one must exist in memory even
 *   though D3D12's own descriptor never becomes memory.
 *
 * BOTH out-parameters are addresses of an 8-byte cell CONTAINING the
 * table's address, not the table itself. agx_usc_uniform fills uniform
 * registers FROM memory at the address it is given, so handing it a table
 * directly loads the table's first eight bytes into the register pair.
 * For the texture heap that is descriptor word 0, which the hardware then
 * uses as the heap base -- a fault at an address that looks like garbage
 * but is exactly the descriptor's own first dword.
 */
static bool
d12_list_upload_bindings(struct d12_cmd_list *list, bool compute,
                         uint64_t *out_root, uint64_t *out_textures,
                         uint64_t *out_pbes, uint64_t *out_heap,
                         uint64_t *out_heap_textures,
                         uint64_t *out_heap_pbes,
                         uint64_t *out_heap_samplers)
{
   bool validate = getenv("D3D12AGX_VALIDATE_BINDINGS") != NULL;
   const struct d12_descriptor *table =
      compute ? list->compute_root_table : list->graphics_root_table;
   const struct d12_descriptor *const *sources =
      compute ? list->compute_root_source : list->graphics_root_source;
   const struct d12_descriptor *sampler_table =
      compute ? list->compute_sampler_table : list->graphics_sampler_table;
   const struct d12_root_signature *rs =
      compute ? list->compute_rs : list->graphics_rs;

   *out_heap = 0;
   *out_heap_textures = 0;
   *out_heap_pbes = 0;
   *out_heap_samplers = 0;

   /* Three arrays back to back: resource base addresses, UAV counter
    * addresses for bufferUpdateCounter, then buffer-view counts for
    * GetDimensions/BUFINFO. A small header below points to each plane so
    * shaders never need a global-load immediate beyond the AGX encoding.
    */
   /* An UNBOUND slot is address 0, and a shader that reads one dereferences
    * it -- which the GPU reports as
    *
    *     address: 0x0  reason: Unmapped  read: true
    *
    * and which then times out the queue. Because the compositor shares this
    * GPU, that stalls the desktop too: 75 such timeouts in one suite run on
    * 2026-08-05 froze the GUI and cost a physical reboot.
    *
    * Substituting zeroed memory is not merely crash avoidance, it is the
    * specified behaviour: D3D12 defines reads through a null descriptor as
    * returning zero, and writes as discarded. Faulting was always the wrong
    * answer.
    */
   uint64_t null_buf = 0, null_counters = 0;
   uint64_t addrs[D12_ROOT_TABLE_ENTRIES];
   for (unsigned i = 0; i < D12_MAX_RESOURCE_BINDINGS; i++) {
      addrs[i] = table[i].address;
      addrs[D12_ROOT_COUNTER_BASE + i] = table[i].counter_address;
      addrs[D12_ROOT_BUFINFO_BASE + i] = 0;

      if (table[i].resource &&
          table[i].resource->desc.Dimension ==
             D3D12_RESOURCE_DIMENSION_BUFFER &&
          (table[i].kind == D12_DESC_SRV ||
           table[i].kind == D12_DESC_UAV)) {
         switch (table[i].buffer_kind) {
         case D12_BUFFER_VIEW_RAW:
            addrs[D12_ROOT_BUFINFO_BASE + i] = table[i].size_B;
            break;
         case D12_BUFFER_VIEW_TYPED:
         case D12_BUFFER_VIEW_STRUCTURED:
            if (table[i].stride_B)
               addrs[D12_ROOT_BUFINFO_BASE + i] =
                  table[i].size_B / table[i].stride_B;
            break;
         default:
            break;
         }
         if (table[i].buffer_kind == D12_BUFFER_VIEW_STRUCTURED ||
             table[i].buffer_kind == D12_BUFFER_VIEW_TYPED)
            addrs[D12_ROOT_BUFINFO_BASE + i] |=
               (uint64_t)table[i].stride_B << 32;
      } else if (table[i].resource && table[i].kind == D12_DESC_SRV &&
                 table[i].resource->desc.Dimension ==
                    D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
         /* Texture SRVs use the low BUFINFO word for the runtime per-view
          * minimum LOD clamp. Buffer users interpret the same plane as a
          * count, and the resource dimension keeps the meanings disjoint. */
         uint32_t info = table[i].texture_txf_min_level |
                         (table[i].texture_min_lod_oob ? 0x80000000u : 0);
         addrs[D12_ROOT_BUFINFO_BASE + i] =
            fui(table[i].texture_min_lod) | (uint64_t)info << 32;
      } else if (table[i].address && !table[i].resource &&
                 (table[i].kind == D12_DESC_SRV ||
                  table[i].kind == D12_DESC_UAV)) {
         /* Root SRV/UAV descriptors carry only a GPU address, so D3D12
          * provides no view extent to publish.  Treat their element count
          * as unbounded instead of zero: structured-store robustness uses
          * this plane for descriptor-table views, and must not accidentally
          * discard every valid access through a bare root descriptor.  Null
          * root descriptors retain count zero because their address is zero
          * until the safe null-buffer substitution below.
          */
         addrs[D12_ROOT_BUFINFO_BASE + i] = UINT32_MAX;
      }

      if (!addrs[i]) {
         if (!null_buf && !(null_buf = d12_list_null_buffer(list)))
            return false;
         addrs[i] = null_buf;
      }

      if (!addrs[D12_ROOT_COUNTER_BASE + i]) {
         if (!null_counters &&
             !(null_counters = d12_list_null_counters(list)))
            return false;
         addrs[D12_ROOT_COUNTER_BASE + i] =
            null_counters + i * sizeof(uint32_t);
      }
   }

   uint64_t tables_gpu = d12_list_upload(list, addrs, sizeof(addrs), 8);
   if (!tables_gpu)
      return false;

   if (!null_buf && !(null_buf = d12_list_null_buffer(list)))
      return false;
   uint64_t discard_buf = d12_list_discard_buffer(list);
   if (!discard_buf)
      return false;

   uint64_t metadata_header[] = {
      tables_gpu + D12_ROOT_COUNTER_BASE * sizeof(uint64_t),
      tables_gpu + D12_ROOT_BUFINFO_BASE * sizeof(uint64_t),
      null_buf,
      discard_buf,
   };
   static_assert(sizeof(metadata_header) == D12_ROOT_METADATA_HEADER_SIZE,
                 "root metadata header ABI mismatch");

   uint64_t metadata_gpu =
      d12_list_upload(list, metadata_header, sizeof(metadata_header), 8);
   if (!metadata_gpu)
      return false;

   /* Resource and sampler slots are independent namespaces. Oversized
    * sampler ranges store their first global heap index in this fourth
    * plane; finite/static samplers continue through the compact USC file. */
   bool rooted_samplers = false;
   if (rs) {
      for (unsigned i = 0; i < rs->map.count; i++) {
         rooted_samplers |= rs->map.entries[i].klass == D12_RES_SAMPLER &&
                            rs->map.entries[i].unbounded;
      }
   }

   uint64_t sampler_tables_gpu = 0;
   if (rooted_samplers) {
      uint64_t sampler_bases[D12_MAX_RESOURCE_BINDINGS];
      for (unsigned i = 0; i < D12_MAX_RESOURCE_BINDINGS; i++)
         sampler_bases[i] = sampler_table[i].address;

      sampler_tables_gpu = d12_list_upload(
         list, sampler_bases, sizeof(sampler_bases), 8);
      if (!sampler_tables_gpu)
         return false;
   }

   uint64_t header[] = {
      tables_gpu + D12_ROOT_RESOURCE_BASE * sizeof(uint64_t),
      metadata_gpu,
      sampler_tables_gpu,
   };
   static_assert(sizeof(header) == D12_ROOT_HEADER_SIZE,
                 "root header ABI mismatch");

   uint64_t header_gpu = d12_list_upload(list, header, sizeof(header), 8);
   if (!header_gpu)
      return false;

   *out_root = d12_list_upload(list, &header_gpu, sizeof(header_gpu), 8);
   if (!*out_root)
      return false;

   /* A bindless texture handle is a byte offset from this heap. Allocate a
    * null descriptor for every slot declared by the active root signature,
    * even when no texture is currently bound. This keeps stale/uninitialised
    * root parameters undefined-but-safe instead of making an omitted uniform
    * read as VA 0 and wedging the GPU.
    */
   unsigned last_texture = rs ? MAX2(rs->map.resource_count, 1) : 1;
   for (unsigned i = 0; i < D12_MAX_RESOURCE_BINDINGS; i++) {
      if ((table[i].kind == D12_DESC_SRV ||
           table[i].kind == D12_DESC_UAV) &&
          table[i].resource &&
          (table[i].resource->desc.Dimension ==
              D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
           (table[i].resource->desc.Dimension ==
               D3D12_RESOURCE_DIMENSION_BUFFER &&
            table[i].kind == D12_DESC_SRV &&
            table[i].format != DXGI_FORMAT_UNKNOWN)))
         last_texture = MAX2(last_texture, i + 1);
   }

   static_assert(AGX_TEXTURE_LENGTH == D12_TEXTURE_DESC_SIZE,
                 "the front end's descriptor stride must match hardware");
   static_assert(AGX_PBE_LENGTH == D12_PBE_DESC_SIZE,
                 "the PBE descriptor stride must match hardware");

   uint64_t heap_gpu = d12_list_upload_zero(
      list, (size_t)last_texture * AGX_TEXTURE_LENGTH, 64);
   if (!heap_gpu)
      return false;

   size_t heap_size = (size_t)last_texture * AGX_TEXTURE_LENGTH;
   if (validate && (!d12_list_stream_contains(list, heap_gpu, heap_size) ||
                    (heap_gpu & 63))) {
      mesa_loge("binding validation: texture heap 0x%llx+%zu is outside "
                "the 64-byte-aligned command-list stream BO",
                (unsigned long long)heap_gpu, heap_size);
      list->state = D12_LIST_ERROR;
      return false;
   }

   struct agx_texture_packed *descs =
      (struct agx_texture_packed *)d12_list_data_cpu(list, heap_gpu);

   for (unsigned i = 0; i < last_texture; i++)
      agx_set_null_texture(&descs[i]);

   for (unsigned i = 0; i < last_texture; i++) {
      if (table[i].kind != D12_DESC_SRV && table[i].kind != D12_DESC_UAV)
         continue;

      struct d12_resource *res = table[i].resource;
      if (!res)
         continue;

      if (res->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
         if (table[i].kind != D12_DESC_SRV ||
             table[i].format == DXGI_FORMAT_UNKNOWN)
            continue;

         enum pipe_format pf = d12_format_to_pipe(table[i].format);
         if (pf == PIPE_FORMAT_NONE || !ail_is_valid_pixel_format(pf) ||
             !ail_pixel_format[pf].texturable ||
             util_format_is_depth_or_stencil(pf))
            continue;

         const struct util_format_description *fdesc =
            util_format_description(pf);
         unsigned blocksize = util_format_get_blocksize(pf);
         unsigned elements = table[i].size_B / blocksize;
         if (!elements)
            continue;

         agx_pack(&descs[i], TEXTURE, cfg) {
            cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
            cfg.layout = AGX_LAYOUT_LINEAR;
            cfg.channels = ail_pixel_format[pf].channels;
            cfg.type = ail_pixel_format[pf].type;
            cfg.srgb = util_format_is_srgb(pf);
            cfg.srgb_2_channel =
               cfg.srgb && util_format_colormask(fdesc) == 0x3;

            cfg.swizzle_r = agx_channel_from_pipe(fdesc->swizzle[0]);
            cfg.swizzle_g = agx_channel_from_pipe(fdesc->swizzle[1]);
            cfg.swizzle_b = agx_channel_from_pipe(fdesc->swizzle[2]);
            cfg.swizzle_a = agx_channel_from_pipe(fdesc->swizzle[3]);

            cfg.address = table[i].address;
            cfg.width = AGX_TEXTURE_BUFFER_WIDTH;
            cfg.height = DIV_ROUND_UP(elements, cfg.width);
            cfg.first_level = 0;
            cfg.last_level = 0;
            cfg.buffer_size_sw = elements;
            cfg.buffer_offset_sw = 0;
            cfg.depth = 1;
            cfg.stride = (cfg.width * blocksize) - 16;
         }
         continue;
      }

      if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
         continue;

      const bool stencil = table[i].texture_plane == 1;
      const struct ail_layout *layout =
         stencil ? &res->stencil_layout : &res->layout;
      struct agx_bo *texture_bo = stencil ? res->stencil_bo : res->bo;
      if (!texture_bo || !layout->size_B)
         continue;

      if (validate) {
         uint64_t bo_base = texture_bo->va->addr;
         if (table[i].address < bo_base ||
             table[i].address - bo_base >= texture_bo->size) {
            mesa_loge("binding validation: texture slot %u address 0x%llx "
                      "is outside resource BO 0x%llx..0x%llx", i,
                      (unsigned long long)table[i].address,
                      (unsigned long long)bo_base,
                      (unsigned long long)(bo_base + texture_bo->size));
            list->state = D12_LIST_ERROR;
            return false;
         }
      }

      /* S8 is a storage/aspect format, not the colour interpretation used by
       * a Texture2D SRV. D3D12 exposes the stencil plane as an R8_UINT value;
       * keep the S8 layout/address but pack the descriptor's channels and
       * type from that view interpretation.
       */
      enum pipe_format pf = stencil ? PIPE_FORMAT_R8_UINT
                                    : table[i].kind == D12_DESC_UAV
                                         ? d12_format_to_pipe(table[i].format)
                                         : layout->format;
      if (pf == PIPE_FORMAT_NONE || !ail_is_valid_pixel_format(pf) ||
          !ail_pixel_format[pf].texturable ||
          util_format_is_depth_or_stencil(pf))
         continue;

      const struct util_format_description *fdesc =
         util_format_description(pf);
      const unsigned samples = MAX2(layout->sample_count_sa, 1);
      const bool array = table[i].kind == D12_DESC_UAV &&
                         table[i].texture_array;
      const unsigned level = table[i].kind == D12_DESC_UAV
                                ? table[i].texture_level
                                : table[i].texture_first_level;
      const unsigned last_level = table[i].kind == D12_DESC_UAV
                                     ? level
                                     : level + MAX2(
                                          table[i].texture_level_count, 1) - 1;
      const unsigned layers = table[i].kind == D12_DESC_UAV
                                 ? MAX2(table[i].texture_layers, 1)
                                 : 1;

      agx_pack(&descs[i], TEXTURE, cfg) {
         cfg.dimension = array ? AGX_TEXTURE_DIMENSION_2D_ARRAY
                         : samples > 1
                            ? AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED
                            : AGX_TEXTURE_DIMENSION_2D;
         cfg.layout = agx_translate_layout(layout->tiling);
         cfg.channels = ail_pixel_format[pf].channels;
         cfg.type = ail_pixel_format[pf].type;
         cfg.srgb = util_format_is_srgb(pf);

         cfg.swizzle_r = agx_channel_from_pipe(fdesc->swizzle[0]);
         cfg.swizzle_g = agx_channel_from_pipe(fdesc->swizzle[1]);
         cfg.swizzle_b = agx_channel_from_pipe(fdesc->swizzle[2]);
         cfg.swizzle_a = agx_channel_from_pipe(fdesc->swizzle[3]);

         cfg.address = table[i].address;
         cfg.width = layout->width_px;
         cfg.height = layout->height_px;
         cfg.first_level = level;
         cfg.last_level = last_level;
         cfg.unk_mipmapped = layout->levels > 1;
         if (samples > 1)
            cfg.samples = agx_translate_sample_count(samples);

         /* Depth and Stride share bit 110; the hardware reads one or the
          * other by layout. Depth carries minus(1), so a single-layer
          * texture packs zero either way -- but write only the one that
          * applies, because an array does not.
          */
         if (layout->tiling == AIL_TILING_LINEAR) {
            /* TEXTURE.Stride is (bytes - 16) >> 4. Note PBE.Stride is a
             * different convention: bytes - 4, unshifted.
             */
            cfg.stride = ail_get_linear_stride_B(layout, 0) - 16;
            if (array) {
               cfg.depth_linear = layers;
               cfg.layer_stride_linear = layout->layer_stride_B - 0x80;
               cfg.extended = true;
            }
         } else {
            cfg.depth = layers;
            cfg.page_aligned_layers = layout->page_aligned_layers;
         }
      }
   }

   uint64_t pbe_heap_gpu = d12_list_upload_zero(
      list, (size_t)last_texture * AGX_PBE_LENGTH, 64);
   if (!pbe_heap_gpu)
      return false;

   size_t pbe_heap_size = (size_t)last_texture * AGX_PBE_LENGTH;
   if (validate &&
       (!d12_list_stream_contains(list, pbe_heap_gpu, pbe_heap_size) ||
        (pbe_heap_gpu & 63))) {
      mesa_loge("binding validation: PBE heap 0x%llx+%zu is outside "
                "the 64-byte-aligned command-list stream BO",
                (unsigned long long)pbe_heap_gpu, pbe_heap_size);
      list->state = D12_LIST_ERROR;
      return false;
   }

   struct agx_pbe_packed *pbes =
      (struct agx_pbe_packed *)d12_list_data_cpu(list, pbe_heap_gpu);
   for (unsigned i = 0; i < last_texture; i++) {
      if (validate && table[i].kind == D12_DESC_UAV && table[i].resource &&
          table[i].resource->desc.Dimension ==
             D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
          (!table[i].resource->layout.writeable_image ||
           table[i].resource->layout.tiling == AIL_TILING_LINEAR)) {
         mesa_loge("binding validation: writable texture slot %u has no "
                   "atomic-capable tiled layout", i);
         list->state = D12_LIST_ERROR;
         return false;
      }
      d12_refresh_pbe_descriptor(&pbes[i], &table[i]);

      if (getenv("D3D12AGX_DUMP_TEX")) {
         const uint8_t *raw = (const uint8_t *)&pbes[i];
         fprintf(stderr, "pbe[%u]:", i);
         for (unsigned b = 0; b < AGX_PBE_LENGTH; b++)
            fprintf(stderr, " %02x", raw[b]);
         fprintf(stderr, "\n");
      }
   }

   if (getenv("D3D12AGX_DUMP_TEX")) {
      for (unsigned i = 0; i < last_texture; i++) {
         struct d12_resource *res =
            (table[i].kind == D12_DESC_SRV ||
             table[i].kind == D12_DESC_UAV) ? table[i].resource : NULL;
         fprintf(stderr,
                 "tex[%u] kind=%u dimension=%u va=0x%llx size=%u "
                 "stride=%u format=%u\n", i, table[i].kind,
                 res ? (unsigned)res->desc.Dimension : 0,
                 (unsigned long long)table[i].address, table[i].size_B,
                 table[i].stride_B, (unsigned)table[i].format);
         const uint8_t *d = (const uint8_t *)&descs[i];
         fprintf(stderr, "  desc:");
         for (unsigned b = 0; b < AGX_TEXTURE_LENGTH; b++)
            fprintf(stderr, " %02x", d[b]);
         fprintf(stderr, "\n");
      }
   }

   /* The heap needs a home for the same reason the root table does. */
   *out_textures = d12_list_upload(list, &heap_gpu, sizeof(heap_gpu), 8);
   *out_pbes =
      d12_list_upload(list, &pbe_heap_gpu, sizeof(pbe_heap_gpu), 8);

   /* Heap-rooted and SM 6.6 handles address the application's coherent
    * 40-byte descriptor array, while AGX texture/image instructions require
    * parallel 24-byte native arrays. Publish one home cell for each base;
    * shader lowering derives the same global heap index for all three. */
   struct d12_descriptor_heap *bound_heap = list->resource_descriptor_heap;
   if (bound_heap && bound_heap->bo && bound_heap->texture_bo &&
       bound_heap->pbe_bo) {
      uint64_t base = bound_heap->bo->va->addr;
      uint64_t textures = bound_heap->texture_bo->va->addr;
      uint64_t pbes = bound_heap->pbe_bo->va->addr;
      *out_heap = d12_list_upload(list, &base, sizeof(base), 8);
      *out_heap_textures =
         d12_list_upload(list, &textures, sizeof(textures), 8);
      *out_heap_pbes = d12_list_upload(list, &pbes, sizeof(pbes), 8);
      if (!*out_heap || !*out_heap_textures || !*out_heap_pbes)
         return false;

      if (validate) {
         mesa_logi("binding validation: bindless heap slots=0x%llx "
                   "textures=0x%llx pbes=0x%llx count=%u valid=1",
                   (unsigned long long)base,
                   (unsigned long long)textures,
                   (unsigned long long)pbes,
                   bound_heap->desc.NumDescriptors);
      }
   }

   struct d12_descriptor_heap *bound_samplers =
      list->sampler_descriptor_heap;
   if (bound_samplers && bound_samplers->sampler_bo) {
      uint64_t samplers = bound_samplers->sampler_bo->va->addr;
      *out_heap_samplers =
         d12_list_upload(list, &samplers, sizeof(samplers), 8);
      if (!*out_heap_samplers)
         return false;

      if (validate) {
         mesa_logi("binding validation: bindless sampler heap=0x%llx "
                   "count=%u home=0x%llx valid=1",
                   (unsigned long long)samplers,
                   bound_samplers->desc.NumDescriptors,
                   (unsigned long long)*out_heap_samplers);
      }
   }

   if (validate) {
      if (!*out_textures || !*out_pbes ||
          !d12_list_stream_contains(list, *out_textures, sizeof(heap_gpu))) {
         mesa_loge("binding validation: texture-home cell 0x%llx is outside "
                   "the command-list stream BO",
                   (unsigned long long)*out_textures);
         list->state = D12_LIST_ERROR;
         return false;
      }

      if (!d12_list_stream_contains(list, *out_pbes,
                                    sizeof(pbe_heap_gpu)) ||
          *(uint64_t *)d12_list_data_cpu(list, *out_pbes) != pbe_heap_gpu) {
         mesa_loge("binding validation: PBE-home cell 0x%llx is invalid",
                   (unsigned long long)*out_pbes);
         list->state = D12_LIST_ERROR;
         return false;
      }

      uint64_t home = *(uint64_t *)d12_list_data_cpu(list, *out_textures);
      if (home != heap_gpu) {
         mesa_loge("binding validation: texture-home cell holds 0x%llx, "
                   "expected heap 0x%llx", (unsigned long long)home,
                   (unsigned long long)heap_gpu);
         list->state = D12_LIST_ERROR;
         return false;
      }

      mesa_logi("binding validation: texture heap=0x%llx+%zu home=0x%llx "
                "PBE=0x%llx+%zu home=0x%llx resource-slots=%u valid=1",
                (unsigned long long)heap_gpu, heap_size,
                (unsigned long long)*out_textures,
                (unsigned long long)pbe_heap_gpu, pbe_heap_size,
                (unsigned long long)*out_pbes, last_texture);
   }

   if (getenv("D3D12AGX_DUMP_TEX") && *out_textures) {
      uint64_t home = *(uint64_t *)d12_list_data_cpu(list, *out_textures);
      fprintf(stderr,
              "chain: textures_gpu=0x%llx -> home holds 0x%llx "
              "(heap_gpu=0x%llx) %s\n",
              (unsigned long long)*out_textures, (unsigned long long)home,
              (unsigned long long)heap_gpu,
              home == heap_gpu ? "MATCH" : "MISMATCH");
   }

   /* A shader-visible finite table remains mutable until execution.  Record
    * where this draw/dispatch flattened each source slot so the queue can
    * refresh it after Close and immediately before submission.
    */
   for (unsigned i = 0; i < D12_MAX_RESOURCE_BINDINGS; i++) {
      if (!sources[i])
         continue;

      struct d12_descriptor_fixup fixup = {
         .source = sources[i],
         .tables_gpu = tables_gpu,
         .textures_gpu = i < last_texture ? heap_gpu : 0,
         .pbes_gpu = i < last_texture ? pbe_heap_gpu : 0,
         .slot = i,
      };
      util_dynarray_append_typed(&list->descriptor_fixups,
                                 struct d12_descriptor_fixup, fixup);
   }

   return *out_textures != 0 && *out_pbes != 0;
}

static uint64_t
d12_descriptor_bufinfo(const struct d12_descriptor *d)
{
   if (d->resource &&
       d->resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
       (d->kind == D12_DESC_SRV || d->kind == D12_DESC_UAV)) {
      uint64_t count = 0;
      switch (d->buffer_kind) {
      case D12_BUFFER_VIEW_RAW:
         count = d->size_B;
         break;
      case D12_BUFFER_VIEW_TYPED:
      case D12_BUFFER_VIEW_STRUCTURED:
         if (d->stride_B)
            count = d->size_B / d->stride_B;
         break;
      default:
         break;
      }

      if (d->buffer_kind == D12_BUFFER_VIEW_STRUCTURED ||
          d->buffer_kind == D12_BUFFER_VIEW_TYPED)
         count |= (uint64_t)d->stride_B << 32;
      return count;
   }

   if (d->resource && d->kind == D12_DESC_SRV &&
       d->resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
      uint32_t info = d->texture_txf_min_level |
                      (d->texture_min_lod_oob ? 0x80000000u : 0);
      return fui(d->texture_min_lod) | (uint64_t)info << 32;
   }

   if (d->address && !d->resource &&
       (d->kind == D12_DESC_SRV || d->kind == D12_DESC_UAV))
      return UINT32_MAX;

   return 0;
}

/* Recreate one API SRV/UAV's AGX descriptor in already-allocated command-list
 * storage.  This is the same packing performed while recording; Execute uses
 * it only for finite shader-visible table slots whose CPU descriptor changed
 * after Close.
 */
void
d12_refresh_texture_descriptor(struct agx_texture_packed *out,
                               const struct d12_descriptor *d)
{
   agx_set_null_texture(out);

   if ((d->kind != D12_DESC_SRV && d->kind != D12_DESC_UAV) || !d->resource)
      return;

   struct d12_resource *res = d->resource;
   if (res->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      if (d->kind != D12_DESC_SRV || d->format == DXGI_FORMAT_UNKNOWN)
         return;

      enum pipe_format pf = d12_format_to_pipe(d->format);
      if (pf == PIPE_FORMAT_NONE || !ail_is_valid_pixel_format(pf) ||
          !ail_pixel_format[pf].texturable ||
          util_format_is_depth_or_stencil(pf))
         return;

      const struct util_format_description *fdesc =
         util_format_description(pf);
      unsigned blocksize = util_format_get_blocksize(pf);
      unsigned elements = d->size_B / blocksize;
      if (!elements)
         return;

      agx_pack(out, TEXTURE, cfg) {
         cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
         cfg.layout = AGX_LAYOUT_LINEAR;
         cfg.channels = ail_pixel_format[pf].channels;
         cfg.type = ail_pixel_format[pf].type;
         cfg.srgb = util_format_is_srgb(pf);
         cfg.srgb_2_channel =
            cfg.srgb && util_format_colormask(fdesc) == 0x3;
         cfg.swizzle_r = agx_channel_from_pipe(fdesc->swizzle[0]);
         cfg.swizzle_g = agx_channel_from_pipe(fdesc->swizzle[1]);
         cfg.swizzle_b = agx_channel_from_pipe(fdesc->swizzle[2]);
         cfg.swizzle_a = agx_channel_from_pipe(fdesc->swizzle[3]);
         cfg.address = d->address;
         cfg.width = AGX_TEXTURE_BUFFER_WIDTH;
         cfg.height = DIV_ROUND_UP(elements, cfg.width);
         cfg.first_level = 0;
         cfg.last_level = 0;
         cfg.buffer_size_sw = elements;
         cfg.buffer_offset_sw = 0;
         cfg.depth = 1;
         cfg.stride = (cfg.width * blocksize) - 16;
      }
      return;
   }

   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
      return;

   const bool stencil = d->texture_plane == 1;
   const struct ail_layout *layout =
      stencil ? &res->stencil_layout : &res->layout;
   struct agx_bo *texture_bo = stencil ? res->stencil_bo : res->bo;
   if (!texture_bo || !layout->size_B)
      return;

   enum pipe_format pf = stencil ? PIPE_FORMAT_R8_UINT
                                 : d->kind == D12_DESC_UAV
                                      ? d12_format_to_pipe(d->format)
                                      : layout->format;
   if (pf == PIPE_FORMAT_NONE || !ail_is_valid_pixel_format(pf) ||
       !ail_pixel_format[pf].texturable ||
       util_format_is_depth_or_stencil(pf))
      return;

   const struct util_format_description *fdesc = util_format_description(pf);
   uint8_t format_swizzle[4] = {
      fdesc->swizzle[0], fdesc->swizzle[1],
      fdesc->swizzle[2], fdesc->swizzle[3],
   };

   /* A8 shares R8 storage on AGX, but its D3D view exposes that physical
    * byte as alpha and forces RGB to zero.  Preserve that logical format
    * swizzle before composing Shader4ComponentMapping. */
   if (d->format == DXGI_FORMAT_A8_UNORM) {
      format_swizzle[0] = PIPE_SWIZZLE_0;
      format_swizzle[1] = PIPE_SWIZZLE_0;
      format_swizzle[2] = PIPE_SWIZZLE_0;
      format_swizzle[3] = PIPE_SWIZZLE_X;
   }

   uint8_t swizzle[4];
   uint8_t view_swizzle[4];
   for (unsigned c = 0; c < 4; c++) {
      view_swizzle[c] = D3D12_DECODE_SHADER_4_COMPONENT_MAPPING(
         c, d->texture_component_mapping);
   }
   util_format_compose_swizzles(format_swizzle, view_swizzle, swizzle);
   const unsigned samples = MAX2(layout->sample_count_sa, 1);
   const bool array = d->kind == D12_DESC_UAV && d->texture_array;
   const unsigned level = d->kind == D12_DESC_UAV
                             ? d->texture_level
                             : d->texture_first_level;
   const unsigned last_level = d->kind == D12_DESC_UAV
                                  ? level
                                  : level + MAX2(d->texture_level_count, 1) - 1;
   const unsigned layers = d->kind == D12_DESC_UAV
                              ? MAX2(d->texture_layers, 1)
                              : 1;

   agx_pack(out, TEXTURE, cfg) {
      cfg.dimension = array ? AGX_TEXTURE_DIMENSION_2D_ARRAY
                      : samples > 1 ? AGX_TEXTURE_DIMENSION_2D_MULTISAMPLED
                                    : AGX_TEXTURE_DIMENSION_2D;
      cfg.layout = agx_translate_layout(layout->tiling);
      cfg.channels = ail_pixel_format[pf].channels;
      cfg.type = ail_pixel_format[pf].type;
      cfg.srgb = util_format_is_srgb(pf);
      cfg.swizzle_r = agx_channel_from_pipe(swizzle[0]);
      cfg.swizzle_g = agx_channel_from_pipe(swizzle[1]);
      cfg.swizzle_b = agx_channel_from_pipe(swizzle[2]);
      cfg.swizzle_a = agx_channel_from_pipe(swizzle[3]);
      cfg.address = d->address;
      cfg.width = layout->width_px;
      cfg.height = layout->height_px;
      cfg.first_level = level;
      cfg.last_level = last_level;
      cfg.unk_mipmapped = layout->levels > 1;
      if (samples > 1)
         cfg.samples = agx_translate_sample_count(samples);

      if (layout->tiling == AIL_TILING_LINEAR) {
         cfg.stride = ail_get_linear_stride_B(layout, 0) - 16;
         if (array) {
            cfg.depth_linear = layers;
            cfg.layer_stride_linear = layout->layer_stride_B - 0x80;
            cfg.extended = true;
         }
      } else {
         cfg.depth = layers;
         cfg.page_aligned_layers = layout->page_aligned_layers;
      }
   }
}

bool
d12_cmd_list_refresh_descriptors(struct d12_cmd_list *list)
{
   bool validate = getenv("D3D12AGX_VALIDATE_BINDINGS") != NULL;

   util_dynarray_foreach (&list->descriptor_fixups,
                          struct d12_descriptor_fixup, fixup) {
      if (!fixup->source || fixup->slot >= D12_MAX_RESOURCE_BINDINGS)
         return false;

      uint64_t *tables =
         (uint64_t *)d12_list_data_cpu(list, fixup->tables_gpu);
      if (!tables)
         return false;

      const struct d12_descriptor *d = fixup->source;
      unsigned slot = fixup->slot;

      if (validate && d->resource) {
         struct agx_bo *bo = d->texture_plane == 1 && d->resource->stencil_bo
                                ? d->resource->stencil_bo
                                : d->resource->bo;
         if (!bo || (d->address &&
                     (d->address < bo->va->addr ||
                      d->address - bo->va->addr >= bo->size))) {
            mesa_loge("descriptor refresh: slot %u source address 0x%llx "
                      "is outside its resource BO",
                      slot, (unsigned long long)d->address);
            return false;
         }
      }

      tables[D12_ROOT_RESOURCE_BASE + slot] =
         d->address ? d->address : list->null_buffer;
      tables[D12_ROOT_COUNTER_BASE + slot] =
         d->counter_address
            ? d->counter_address
            : list->null_counters + slot * sizeof(uint32_t);
      tables[D12_ROOT_BUFINFO_BASE + slot] = d12_descriptor_bufinfo(d);

      if (fixup->textures_gpu) {
         struct agx_texture_packed *texture =
            (struct agx_texture_packed *)d12_list_data_cpu(
               list, fixup->textures_gpu + slot * AGX_TEXTURE_LENGTH);
         if (!texture)
            return false;
         d12_refresh_texture_descriptor(texture, d);
      }

      if (fixup->pbes_gpu) {
         struct agx_pbe_packed *pbe =
            (struct agx_pbe_packed *)d12_list_data_cpu(
               list, fixup->pbes_gpu + slot * AGX_PBE_LENGTH);
         if (!pbe)
            return false;
         d12_refresh_pbe_descriptor(pbe, d);
      }

      if (getenv("D3D12AGX_DUMP_DESCRIPTOR_FIXUPS"))
         mesa_logi("descriptor refresh: slot=%u source=%p kind=%u "
                   "address=0x%llx tables=0x%llx textures=0x%llx "
                   "pbes=0x%llx valid=1",
                   slot, (const void *)d, d->kind,
                   (unsigned long long)d->address,
                   (unsigned long long)fixup->tables_gpu,
                   (unsigned long long)fixup->textures_gpu,
                   (unsigned long long)fixup->pbes_gpu);
   }

   util_dynarray_foreach (&list->indirect_constant_fixups,
                          struct d12_indirect_constant_fixup, fixup) {
      const struct d12_resource *source = fixup->source;
      if (!source || !source->bo ||
          source->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
          fixup->source_offset_B > source->desc.Width ||
          fixup->size_B > source->desc.Width - fixup->source_offset_B ||
          source->offset_B > source->bo->size ||
          fixup->source_offset_B > source->bo->size - source->offset_B ||
          fixup->size_B > source->bo->size - source->offset_B -
                              fixup->source_offset_B ||
          !d12_list_stream_contains(list, fixup->destination_gpu,
                                    fixup->size_B)) {
         mesa_loge("indirect constant refresh references invalid storage");
         return false;
      }

      const uint8_t *src = agx_bo_map(source->bo) + source->offset_B +
                           fixup->source_offset_B;
      uint8_t *dst = d12_list_data_cpu(list, fixup->destination_gpu);
      memcpy(dst, src, fixup->size_B);

      if (getenv("D3D12AGX_DUMP_INDIRECT"))
         mesa_logi("indirect constants: source=%p+0x%llx destination=0x%llx "
                   "size=%u valid=1", (void *)source,
                   (unsigned long long)fixup->source_offset_B,
                   (unsigned long long)fixup->destination_gpu,
                   fixup->size_B);
   }

   util_dynarray_foreach (&list->indirect_argument_fixups,
                          struct d12_indirect_argument_fixup, fixup) {
      const struct d12_resource *source = fixup->source;
      const struct d12_resource *count = fixup->count_source;
      if (!source || !source->bo || !count || !count->bo ||
          source->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
          count->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
          fixup->source_offset_B > source->desc.Width ||
          fixup->size_B > source->desc.Width - fixup->source_offset_B ||
          fixup->count_offset_B > count->desc.Width ||
          sizeof(uint32_t) > count->desc.Width - fixup->count_offset_B ||
          source->offset_B > source->bo->size ||
          fixup->source_offset_B > source->bo->size - source->offset_B ||
          fixup->size_B > source->bo->size - source->offset_B -
                              fixup->source_offset_B ||
          count->offset_B > count->bo->size ||
          fixup->count_offset_B > count->bo->size - count->offset_B ||
          sizeof(uint32_t) > count->bo->size - count->offset_B -
                                fixup->count_offset_B ||
          !d12_list_stream_contains(list, fixup->destination_gpu,
                                    fixup->size_B)) {
         mesa_loge("indirect argument refresh references invalid storage");
         return false;
      }

      const uint8_t *count_cpu = agx_bo_map(count->bo) + count->offset_B +
                                 fixup->count_offset_B;
      uint32_t command_count;
      memcpy(&command_count, count_cpu, sizeof(command_count));

      uint8_t *dst = d12_list_data_cpu(list, fixup->destination_gpu);
      if (fixup->command_index < command_count) {
         const uint8_t *src = agx_bo_map(source->bo) + source->offset_B +
                              fixup->source_offset_B;
         memcpy(dst, src, fixup->size_B);
      } else {
         memset(dst, 0, fixup->size_B);
      }

      if (getenv("D3D12AGX_DUMP_INDIRECT"))
         mesa_logi("indirect args: source=%p+0x%llx count=%u index=%u "
                   "destination=0x%llx size=%u valid=1", (void *)source,
                   (unsigned long long)fixup->source_offset_B, command_count,
                   fixup->command_index,
                   (unsigned long long)fixup->destination_gpu,
                   fixup->size_B);
   }

   /* The stream BO is coherent, but order the CPU descriptor writes before
    * the queue ioctl makes the command stream visible to the GPU.
    */
   __sync_synchronize();
   return true;
}

/*
 * Dispatch a pipeline compiled from the application's DXIL. Mirrors
 * hk_dispatch_with_usc: build USC words binding the shader, its registers
 * and our resource-address uniforms, then emit a CDM launch.
 */
/*
 * The uniform registers a compute launch publishes. Split out because the
 * geometry programs need the same USC construction as an ordinary
 * dispatch plus the two poly parameter blocks, and duplicating ~60 lines
 * of USC packing to add two uniforms is how the two paths drift apart.
 *
 * A zero address means "not published", which is not the same as
 * publishing zero: the geometry programs read these as pointers.
 */
struct d12_dispatch_uniforms {
   uint64_t root;
   uint64_t textures;
   uint64_t pbes;
   uint64_t heap;
   uint64_t heap_textures;
   uint64_t heap_pbes;
   uint64_t heap_samplers;
   uint64_t draw;
   uint64_t vertex_params;
   uint64_t geom_params;
   uint64_t tess_params;
   uint64_t mesh_params;
   /* Address of the VS output MASK, not the mask itself: agx_usc_uniform
    * fills registers FROM memory, and load_vs_outputs_poly reads a 64-bit
    * value. The geometry body computes every vertex address in the output
    * buffer from this mask, so a wrong one is not a wrong value, it is a
    * wrong POINTER.
    */
   uint64_t vs_outputs;

   /* Address of a POINTER to the vertex-attribute block, not the block:
    * agx_usc_uniform fills registers from memory, and the shader reads a
    * 64-bit base it then adds offsets to. Same indirection as the mesh
    * and geometry parameter blocks.
    */
   uint64_t vb_params;
};

static bool
d12_sampler_visible(D3D12_SHADER_VISIBILITY declared,
                    D3D12_SHADER_VISIBILITY stage)
{
   return declared == D3D12_SHADER_VISIBILITY_ALL || declared == stage;
}

/* Upload one contiguous sampler state file. AGX reserves hardware state 0
 * for texture Load; application sampler S therefore lives at S + 1. Using
 * one USC record for the complete file matches the proven Gallium path and
 * avoids overlapping state records with unrelated backing addresses. */
static bool
d12_list_push_samplers(struct d12_cmd_list *list,
                       struct agx_usc_builder *b,
                       const struct agx_shader_info *info,
                       bool compute_bindings,
                       D3D12_SHADER_VISIBILITY visibility)
{
   bool validate = getenv("D3D12AGX_VALIDATE_BINDINGS") != NULL;
   unsigned count = info->uses_txf ? MAX2(info->sampler_state_count, 1)
                                   : info->sampler_state_count;

   if (getenv("D3D12AGX_DUMP_SAMPLERS"))
      mesa_logi("sampler requirement: stage=%u compute=%u count=%u txf=%u",
                (unsigned)visibility, compute_bindings, count,
                info->uses_txf);

   if (!count)
      return true;
   if (count > 17) {
      mesa_loge("shader requires %u sampler states; AGX supports 17", count);
      list->state = D12_LIST_ERROR;
      return false;
   }

   struct agx_sampler_packed states[17];
   for (unsigned i = 0; i < count; i++)
      agx_pack_txf_sampler(&states[i]);

   const struct d12_descriptor *dynamic =
      compute_bindings ? list->compute_sampler_table
                       : list->graphics_sampler_table;
   const struct d12_root_signature *rs =
      compute_bindings ? list->compute_rs : list->graphics_rs;

   for (unsigned hw = 1; hw < count; hw++) {
      unsigned logical = hw - 1;
      const struct d12_descriptor *chosen = NULL;
      bool visible_slot = false;

      if (rs) {
         for (unsigned i = 0; i < rs->map.count; i++) {
            if (rs->map.entries[i].klass == D12_RES_SAMPLER &&
                rs->map.entries[i].slot == logical &&
                (rs->map.entries[i].visibility ==
                    D3D12_SHADER_VISIBILITY_ALL ||
                 rs->map.entries[i].visibility == visibility)) {
               visible_slot = true;
               break;
            }
         }

         for (uint32_t i = 0; i < rs->layout.num_static_samplers; i++) {
            const D3D12_STATIC_SAMPLER_DESC1 *s =
               &rs->layout.static_samplers[i];
            if (!d12_sampler_visible(s->ShaderVisibility, visibility))
               continue;
            if (d12_binding_map_find_visible(
                   &rs->map, D12_RES_SAMPLER, s->RegisterSpace,
                   s->ShaderRegister, visibility) == logical) {
               chosen = &rs->static_samplers[i];
               break;
            }
         }
      }

      if (!chosen && dynamic[logical].sampler_kind == D12_DESC_SAMPLER)
         chosen = &dynamic[logical];

      if (chosen) {
         states[hw] = chosen->sampler;
      } else if (validate && visible_slot) {
         mesa_loge("binding validation: sampler s%u has no visible static "
                   "or dynamic descriptor for stage %u", logical,
                   (unsigned)visibility);
         list->state = D12_LIST_ERROR;
         return false;
      }
   }

   size_t state_size = count * sizeof(states[0]);
   uint64_t gpu = d12_list_upload(list, states, state_size, 8);
   if (!gpu)
      return false;

   if (validate && ((gpu & 7) ||
                    !d12_list_stream_contains(list, gpu, state_size))) {
      mesa_loge("binding validation: sampler file 0x%llx+%zu is outside "
                "the aligned command-list stream BO",
                (unsigned long long)gpu, state_size);
      list->state = D12_LIST_ERROR;
      return false;
   }

   if (getenv("D3D12AGX_DUMP_SAMPLERS")) {
      mesa_logi("samplers: stage=%u compute=%u states=%u logical=%u "
                "gpu=0x%llx stream=0x%llx..0x%llx valid=%u",
                (unsigned)visibility, compute_bindings, count,
                rs ? rs->map.sampler_count : 0,
                (unsigned long long)gpu,
                (unsigned long long)list->stream_bo->va->addr,
                (unsigned long long)(list->stream_bo->va->addr +
                                     list->stream_bo->size),
                d12_list_stream_contains(list, gpu, state_size));
      for (unsigned i = 0; i < count; i++) {
         const uint8_t *raw = (const uint8_t *)&states[i];
         mesa_logi("  ss%u: %02x %02x %02x %02x %02x %02x %02x %02x",
                   i, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                   raw[6], raw[7]);
      }
   }

   agx_usc_pack(b, SAMPLER, cfg) {
      cfg.start = 0;
      cfg.count = count;
      cfg.buffer = gpu;
   }
   return true;
}

/* Build the USC words for one compute launch; returns the USC offset, or
 * 0 on failure.
 */
static uint32_t
d12_list_build_compute_usc(struct d12_cmd_list *list, uint64_t code_va,
                           const struct agx_shader_info *info,
                           const struct d12_dispatch_uniforms *u,
                           bool compute_bindings,
                           D3D12_SHADER_VISIBILITY visibility,
                           uint8_t **out_cpu)
{
   struct d12_device *device = list->device;

   uint32_t usc;
   size_t usc_size = agx_usc_size(18);
   uint8_t *usc_cpu = d12_list_alloc_usc(list, usc_size, &usc);
   if (!usc_cpu)
      return 0;

   if (out_cpu)
      *out_cpu = usc_cpu;

   struct agx_usc_builder b = agx_usc_builder(usc_cpu, usc_size);

   /* Four 16-bit uniform registers hold the 64-bit root pointer. */
   agx_usc_uniform(&b, D12_UNIFORM_ROOT, D12_UNIFORM_ROOT_REGS, u->root);
   if (u->textures) {
      agx_usc_uniform(&b, D12_UNIFORM_TEXTURES, D12_UNIFORM_TEXTURE_REGS,
                      u->textures);
   }

   /* The geometry programs read their parameter blocks from these two,
    * which the DXIL translator already lowers poly's parameter
    * intrinsics to. Nothing else publishes them.
    */
   /* Ascending base order: 4, 8, 20, 24, 28, 32, 36, 52. */
   if (u->draw) {
      agx_usc_uniform(&b, D12_UNIFORM_DRAW, D12_UNIFORM_DRAW_REGS, u->draw);
   }
   if (u->tess_params) {
      agx_usc_uniform(&b, D12_UNIFORM_TESS_PARAMS, D12_UNIFORM_PARAM_REGS,
                      u->tess_params);
   }
   if (u->geom_params) {
      agx_usc_uniform(&b, D12_UNIFORM_GEOM_PARAMS, D12_UNIFORM_PARAM_REGS,
                      u->geom_params);
   }
   if (u->vs_outputs) {
      agx_usc_uniform(&b, D12_UNIFORM_VS_OUTPUTS, D12_UNIFORM_PARAM_REGS,
                      u->vs_outputs);
   }
   if (u->vertex_params) {
      agx_usc_uniform(&b, D12_UNIFORM_VERTEX_PARAMS, D12_UNIFORM_PARAM_REGS,
                      u->vertex_params);
   }
   if (u->mesh_params) {
      agx_usc_uniform(&b, D12_UNIFORM_MESH_PARAMS, D12_UNIFORM_MESH_REGS,
                      u->mesh_params);
   }

   /*
    * VERTEX ATTRIBUTE FETCH, and this is a COMPUTE builder on purpose.
    *
    * The software VS that a tessellation, geometry or mesh pipeline
    * substitutes for the hardware vertex stage is a compute shader, and
    * compile_sw_vs_stage runs agx_nir_lower_vbo over it -- so it fetches
    * its attributes through exactly this uniform. The graphics builder
    * published it and this one did not, so every such dispatch read the
    * vertex buffer through uniform registers nobody had written, i.e.
    * through whatever the previous dispatch happened to leave in them.
    *
    * It is the same class of bug as the rodata comment directly below,
    * and it is what wedged the GPU on test_quad_tessellation_dxil: a read
    * of an unmapped address in a region we never map, different every
    * run, belonging to none of our allocations. Our own g7 tessellation
    * probe never caught it because it derives its geometry from
    * SV_VertexID and so fetches no attributes at all.
    */
   if (u->vb_params) {
      agx_usc_uniform(&b, D12_UNIFORM_VB_PARAMS, D12_UNIFORM_VB_REGS,
                      u->vb_params);
   }

   if (u->heap) {
      agx_usc_uniform(&b, D12_UNIFORM_HEAP, D12_UNIFORM_HEAP_REGS,
                      u->heap);
   }
   if (u->pbes) {
      agx_usc_uniform(&b, D12_UNIFORM_PBES, D12_UNIFORM_PBE_REGS,
                      u->pbes);
   }
   if (u->heap_textures) {
      agx_usc_uniform(&b, D12_UNIFORM_HEAP_TEXTURES,
                      D12_UNIFORM_HEAP_TEXTURE_REGS, u->heap_textures);
   }
   if (u->heap_pbes) {
      agx_usc_uniform(&b, D12_UNIFORM_HEAP_PBES,
                      D12_UNIFORM_HEAP_PBE_REGS, u->heap_pbes);
   }
   if (u->heap_samplers) {
      agx_usc_uniform(&b, D12_UNIFORM_HEAP_SAMPLERS,
                      D12_UNIFORM_HEAP_SAMPLER_REGS, u->heap_samplers);
   }

   /*
    * Constants the compiler promoted out of the instruction stream live in
    * the shader BO's rodata, and the shader reads them as uniforms above
    * its push range. Nothing else publishes them: skip this and those
    * uniforms hold whatever the last dispatch left there, so a shader
    * computes with stale numbers instead of its own constants.
    */
   if (info->rodata.size_16)
      agx_usc_immediates(&b, &info->rodata, code_va);

   if (!d12_list_push_samplers(list, &b, info, compute_bindings, visibility))
      return 0;

   /* The compiler publishes static TGSM usage in info->local_size. Mirror
    * Honeykrisp's non-fragment program binding so AGX allocates that shared
    * memory for the workgroup instead of advertising "no shared memory". */
   agx_usc_shared_non_fragment(&b, info, 0);

   agx_usc_pack(&b, SHADER, cfg) {
      cfg.code = agx_usc_addr(&device->dev, code_va + info->main_offset);
      cfg.unk_2 = 3; /* compute */
   }

   agx_usc_pack(&b, REGISTERS, cfg)
      cfg.register_count = info->nr_gprs;

   if (info->has_preamble) {
      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code =
            agx_usc_addr(&device->dev, code_va + info->preamble_offset);
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
   }

   if (getenv("D3D12AGX_DUMP_GS_USC")) {
      mesa_logi("compute USC: code=0x%llx main=%u gprs=%u push=%u "
                "preamble=%d tex=%u txf=%d pregprs=%u "
                "root=0x%llx vp=0x%llx gp=0x%llx vso=0x%llx",
                (unsigned long long)code_va, info->main_offset, info->nr_gprs,
                info->push_count, info->has_preamble,
                info->texture_state_count, info->uses_txf, info->nr_preamble_gprs,
                (unsigned long long)u->root,
                (unsigned long long)u->vertex_params,
                (unsigned long long)u->geom_params,
                (unsigned long long)u->vs_outputs);
      for (unsigned i = 0; i < 48; i += 16) {
         mesa_logi("  %02x: %02x %02x %02x %02x %02x %02x %02x %02x "
                   "%02x %02x %02x %02x %02x %02x %02x %02x", i,
                   usc_cpu[i], usc_cpu[i+1], usc_cpu[i+2], usc_cpu[i+3],
                   usc_cpu[i+4], usc_cpu[i+5], usc_cpu[i+6], usc_cpu[i+7],
                   usc_cpu[i+8], usc_cpu[i+9], usc_cpu[i+10], usc_cpu[i+11],
                   usc_cpu[i+12], usc_cpu[i+13], usc_cpu[i+14], usc_cpu[i+15]);
      }
   }

   return usc;
}

/* Pack the launch word for one compute program. */
static struct agx_cdm_launch_word_0_packed
d12_compute_launch_word(const struct agx_shader_info *info)
{
   struct agx_cdm_launch_word_0_packed launch;
   agx_pack(&launch, CDM_LAUNCH_WORD_0, cfg) {
      cfg.texture_state_register_count = info->texture_state_count;
      /* See the note in d12_pipeline.c: txf needs a sampler counted. */
      cfg.sampler_state_register_count = agx_translate_sampler_state_count(
         info->uses_txf ? MAX2(info->sampler_state_count, 1)
                        : info->sampler_state_count,
         false);
      cfg.uniform_register_count = info->push_count;
      cfg.preshader_register_count = info->nr_preamble_gprs;
   }
   return launch;
}

static bool
d12_segment_bind_sampler_heap(struct d12_cmd_list *list,
                              struct d12_segment *seg, bool required)
{
   if (!required)
      return true;

   struct d12_descriptor_heap *heap = list->sampler_descriptor_heap;
   if (!heap || !heap->sampler_bo ||
       heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
      mesa_loge("shader uses bindless samplers with no sampler heap bound");
      list->state = D12_LIST_ERROR;
      return false;
   }
   if (heap->desc.NumDescriptors > 1024) {
      mesa_loge("sampler heap has %u entries; AGX firmware supports 1024",
                heap->desc.NumDescriptors);
      list->state = D12_LIST_ERROR;
      return false;
   }

   uint64_t address = heap->sampler_bo->va->addr;
   if (seg->sampler_heap &&
       (seg->sampler_heap != address ||
        seg->sampler_count != heap->desc.NumDescriptors)) {
      mesa_loge("one hardware segment references multiple sampler heaps");
      list->state = D12_LIST_ERROR;
      return false;
   }

   seg->sampler_heap = address;
   seg->sampler_count = heap->desc.NumDescriptors;
   return true;
}

static void
d12_list_dispatch_pso(struct d12_cmd_list *list, unsigned x, unsigned y,
                      unsigned z, uint64_t indirect_args)
{
   struct d12_device *device = list->device;
   struct d12_pipeline_state *pso = list->pso;

   if (list->state != D12_LIST_RECORDING)
      return;

   if (!pso || !pso->is_compute) {
      mesa_loge("Dispatch without a compute pipeline state");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_segment *seg = d12_list_get_cdm(list);
   if (!seg)
      return;

   if (!d12_segment_bind_sampler_heap(
          list, seg, pso->info.uses_sampler_heap))
      return;

   if (!d12_segment_reserve_scratch(list, seg, D12_SCRATCH_CS, &pso->info))
      return;

   uint64_t root_gpu = 0, textures_gpu = 0, pbes_gpu = 0;
   uint64_t heap_gpu = 0, heap_textures_gpu = 0, heap_pbes_gpu = 0;
   uint64_t heap_samplers_gpu = 0;
   if (!d12_list_upload_bindings(list, true, &root_gpu, &textures_gpu,
                                 &pbes_gpu, &heap_gpu, &heap_textures_gpu,
                                 &heap_pbes_gpu, &heap_samplers_gpu))
      return;

   uint8_t *usc_cpu = NULL;
   uint32_t usc = d12_list_build_compute_usc(
      list, pso->code_va, &pso->info,
      &(struct d12_dispatch_uniforms){.root = root_gpu,
                                      .textures = textures_gpu,
                                      .pbes = pbes_gpu,
                                      .heap = heap_gpu,
                                      .heap_textures = heap_textures_gpu,
                                      .heap_pbes = heap_pbes_gpu,
                                      .heap_samplers = heap_samplers_gpu},
      true, D3D12_SHADER_VISIBILITY_ALL, &usc_cpu);
   if (!usc)
      return;

   if (getenv("D3D12AGX_DUMP_USC")) {
      fprintf(stderr, "pso USC @%u:", usc);
      for (unsigned i = 0; i < 32; i++)
         fprintf(stderr, " %02x", usc_cpu[i]);
      fprintf(stderr, "\n  root_gpu=0x%llx textures_gpu=0x%llx "
                      "pbes_gpu=0x%llx\n",
              (unsigned long long)root_gpu,
              (unsigned long long)textures_gpu,
              (unsigned long long)pbes_gpu);
   }

   if (getenv("D3D12AGX_DUMP_TEX")) {
      fprintf(stderr,
              "cs info: uses_txf=%d sampler_state_count=%u "
              "texture_state_count=%u push_count=%u nr_preamble_gprs=%u\n",
              pso->info.uses_txf, pso->info.sampler_state_count,
              pso->info.texture_state_count, pso->info.push_count,
              pso->info.nr_preamble_gprs);
      fprintf(stderr, "   rodata: size_16=%u base_uniform=%u offset=%u\n",
              pso->info.rodata.size_16, pso->info.rodata.base_uniform,
              pso->info.rodata.offset);
   }

   struct agx_cdm_launch_word_0_packed launch =
      d12_compute_launch_word(&pso->info);

   struct agx_workgroup wg = agx_workgroup(pso->info.workgroup_size[0],
                                           pso->info.workgroup_size[1],
                                           pso->info.workgroup_size[2]);

   /* D3D12 Dispatch counts thread GROUPS; AGX launches want global threads.
    * A native indirect pointer is therefore directly compatible only for a
    * 1x1x1 workgroup. Larger workgroups need a GPU-side argument rewrite.
    */
   struct agx_grid grid;
   if (indirect_args) {
      if (wg.x != 1 || wg.y != 1 || wg.z != 1) {
         mesa_loge("ExecuteIndirect: dispatch workgroup %ux%ux%u needs an "
                   "argument rewrite", wg.x, wg.y, wg.z);
         list->state = D12_LIST_ERROR;
         return;
      }
      grid = agx_grid_indirect(indirect_args);
   } else {
      grid = agx_3d(x * wg.x, y * wg.y, z * wg.z);
   }

   if (!d12_list_check_stream_space(list))
      return;

   list->stream_cur = (uint8_t *)agx_cdm_launch(
      (uint32_t *)list->stream_cur, device->dev.chip, grid, wg, launch, usc);
   list->stream_cur = (uint8_t *)agx_cdm_barrier(
      (uint32_t *)list->stream_cur, device->dev.chip);
   seg->dispatches++;
}

/*
 * Build the USC words that bind one linked graphics stage: our root
 * uniform, the stage's shared config, then the linker's packed program
 * words. Mirrors hk_upload_usc_words for the no-descriptor case.
 */
static uint32_t
d12_gfx_stage_usc(struct d12_cmd_list *list, const struct d12_gfx_stage *st,
                  bool fragment, const struct agx_tilebuffer_layout *tib,
                  uint64_t root_gpu, uint64_t textures_gpu, uint64_t pbes_gpu,
                  uint64_t heap_gpu, uint64_t heap_textures_gpu,
                  uint64_t heap_pbes_gpu, uint64_t heap_samplers_gpu,
                  uint64_t draw_gpu, uint64_t blend_gpu,
                  D3D12_SHADER_VISIBILITY visibility,
                  const struct d12_dispatch_uniforms *poly)
{
   struct d12_device *device = list->device;

   uint32_t usc;
   uint8_t *cpu = d12_list_alloc_usc(list, agx_usc_size(18), &usc);
   if (!cpu)
      return 0;

   struct agx_usc_builder b = agx_usc_builder(cpu, agx_usc_size(18));

   /* USC uniform records must be emitted in ascending register order.
    * Fragment registers 4..11 carry D3D12's four blend constants, followed
    * by the fragment root at 12 and the texture-heap base at 16. Keeping the
    * texture base at the old slot 8 would alias the upper half of the blend
    * constants whenever a shader sampled and blended in the same draw. */
   if (fragment) {
      agx_usc_uniform(&b, 4, 8, blend_gpu);
      agx_usc_uniform(&b, D12_UNIFORM_FRAGMENT_ROOT, D12_UNIFORM_ROOT_REGS,
                      root_gpu);
   } else {
      agx_usc_uniform(&b, D12_UNIFORM_ROOT, D12_UNIFORM_ROOT_REGS, root_gpu);
   }
   if (textures_gpu)
      agx_usc_uniform(&b, D12_UNIFORM_TEXTURES, D12_UNIFORM_TEXTURE_REGS,
                      textures_gpu);

   if (draw_gpu) {
      agx_usc_uniform(&b, D12_UNIFORM_DRAW, D12_UNIFORM_DRAW_REGS,
                      draw_gpu);
   }

   /*
    * A geometry shader's RASTERIZATION program is a hardware vertex
    * shader that re-runs the geometry body, so it reads the same
    * parameter blocks the compute half does -- and the emitted vertices
    * through them. Publishing them only for the compute dispatches
    * leaves this program reading whatever was in those registers, which
    * measured as a point covering the ENTIRE render target while the
    * same points without a geometry shader drew four pixels.
    */
   /* Ascending base order, matching the compute builder: 24, 28, 32, 36. */
   if (poly) {
      if (poly->geom_params) {
         agx_usc_uniform(&b, D12_UNIFORM_GEOM_PARAMS, D12_UNIFORM_PARAM_REGS,
                         poly->geom_params);
      }
      if (poly->tess_params) {
         agx_usc_uniform(&b, D12_UNIFORM_TESS_PARAMS, D12_UNIFORM_PARAM_REGS,
                         poly->tess_params);
      }
      if (poly->vs_outputs) {
         agx_usc_uniform(&b, D12_UNIFORM_VS_OUTPUTS, D12_UNIFORM_PARAM_REGS,
                         poly->vs_outputs);
      }
      if (poly->vertex_params) {
         agx_usc_uniform(&b, D12_UNIFORM_VERTEX_PARAMS,
                         D12_UNIFORM_PARAM_REGS, poly->vertex_params);
      }
      if (poly->mesh_params) {
         agx_usc_uniform(&b, D12_UNIFORM_MESH_PARAMS, D12_UNIFORM_MESH_REGS,
                         poly->mesh_params);
      }
      if (poly->vb_params) {
         agx_usc_uniform(&b, D12_UNIFORM_VB_PARAMS, D12_UNIFORM_VB_REGS,
                         poly->vb_params);
      }
   }

   if (heap_gpu) {
      agx_usc_uniform(&b, D12_UNIFORM_HEAP, D12_UNIFORM_HEAP_REGS,
                      heap_gpu);
   }
   if (pbes_gpu) {
      agx_usc_uniform(&b, D12_UNIFORM_PBES, D12_UNIFORM_PBE_REGS,
                      pbes_gpu);
   }
   if (heap_textures_gpu) {
      agx_usc_uniform(&b, D12_UNIFORM_HEAP_TEXTURES,
                      D12_UNIFORM_HEAP_TEXTURE_REGS, heap_textures_gpu);
   }
   if (heap_pbes_gpu) {
      agx_usc_uniform(&b, D12_UNIFORM_HEAP_PBES,
                      D12_UNIFORM_HEAP_PBE_REGS, heap_pbes_gpu);
   }
   if (heap_samplers_gpu) {
      agx_usc_uniform(&b, D12_UNIFORM_HEAP_SAMPLERS,
                      D12_UNIFORM_HEAP_SAMPLER_REGS, heap_samplers_gpu);
   }

   /* Promoted constants start above D12_UNIFORM_RESERVED, so bind them only
    * after every driver-owned uniform to preserve ascending register order. */
   if (st->info.rodata.size_16) {
      agx_usc_immediates(&b, &st->info.rodata, st->linked.bo->va->addr);
   }

   if (!d12_list_push_samplers(list, &b, &st->info, false, visibility))
      return 0;

   if (fragment) {
      agx_usc_push_packed(&b, SHARED, &tib->usc);
   } else {
      agx_usc_shared_none(&b);
   }

   agx_usc_push_packed(&b, SHADER, &st->linked.shader);
   agx_usc_push_packed(&b, REGISTERS, &st->linked.regs);

   if (fragment) {
      agx_usc_push_packed(&b, FRAGMENT_PROPERTIES,
                          &st->linked.fragment_props);
   }

   if (st->info.has_preamble) {
      bool preamble_valid = st->code_bo &&
         st->preamble_va >= st->code_bo->va->addr &&
         st->preamble_va - st->code_bo->va->addr < st->code_bo->size;

      if (!preamble_valid) {
         mesa_loge("binding validation: %s preamble 0x%llx is not owned "
                   "by its compiled-part BO",
                   fragment ? "fragment" : "vertex",
                   (unsigned long long)st->preamble_va);
         list->state = D12_LIST_ERROR;
         return 0;
      }

      if (getenv("D3D12AGX_DUMP_TEX"))
         mesa_logi("%s preamble: va=0x%llx bo=0x%llx..0x%llx valid=1 "
                   "tex_states=%u sampler_states=%u pregprs=%u",
                   fragment ? "fragment" : "vertex",
                   (unsigned long long)st->preamble_va,
                   (unsigned long long)st->code_bo->va->addr,
                   (unsigned long long)(st->code_bo->va->addr +
                                        st->code_bo->size),
                   st->info.texture_state_count,
                   st->info.sampler_state_count,
                   st->info.nr_preamble_gprs);

      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code = agx_usc_addr(&device->dev, st->preamble_va);
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
   }

   size_t usc_used = b.head - cpu;
   if (getenv("D3D12AGX_VALIDATE_BINDINGS") &&
       usc_used > agx_usc_size(18)) {
      mesa_loge("binding validation: graphics USC used %zu bytes of %zu",
                usc_used, (size_t)agx_usc_size(18));
      list->state = D12_LIST_ERROR;
      return 0;
   }

   return usc;
}

/*
 * Add a hardware scissor entry for this draw and return its index.
 *
 * These are not only the XY bounds. Each entry also carries the depth CLIP
 * range, and the hardware consults it for every fragment: with no array
 * uploaded, ISP_SCISSOR_BASE reads as zero, the clip range is [0, 0], and
 * every fragment with a nonzero window Z is discarded. That failure looks
 * exactly like a broken depth test, which is how it was found.
 *
 * Entries are deduplicated against the previous one, so a pass that never
 * changes viewport or scissor uses a single entry regardless of draw count.
 */
static unsigned
d12_list_push_scissor(struct d12_cmd_list *list, struct d12_segment *seg,
                      unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
   struct agx_scissor_packed packed;

   agx_pack(&packed, SCISSOR, cfg) {
      cfg.min_x = x0;
      cfg.min_y = y0;
      cfg.max_x = x1;
      cfg.max_y = y1;

      /* D3D12 clips against 0 <= z <= w in clip space, which is the whole
       * window depth range. MinDepth/MaxDepth are a mapping, not a clip.
       */
      cfg.min_z = 0.0f;
      cfg.max_z = 1.0f;
   }

   uint8_t *array = d12_list_data_cpu(list, seg->scissor_base);
   if (!array) {
      mesa_loge("draw into a render pass with no scissor array");
      list->state = D12_LIST_ERROR;
      return ~0u;
   }

   if (seg->scissor_count) {
      uint8_t *prev = array + (seg->scissor_count - 1) * AGX_SCISSOR_LENGTH;
      if (!memcmp(prev, &packed, AGX_SCISSOR_LENGTH))
         return seg->scissor_count - 1;
   }

   if (seg->scissor_count >= D12_MAX_SCISSORS) {
      mesa_loge("render pass exceeded %u distinct scissor/viewport states",
                D12_MAX_SCISSORS);
      list->state = D12_LIST_ERROR;
      return ~0u;
   }

   unsigned index = seg->scissor_count++;
   memcpy(array + index * AGX_SCISSOR_LENGTH, &packed, AGX_SCISSOR_LENGTH);
   return index;
}

/*
 * Push one face's stencil state. When the test is off the slot still has to
 * be written: the hardware keeps this state across the control stream, and
 * an all-zero FRAGMENT_STENCIL means compare NEVER, which discards every
 * fragment rather than doing nothing.
 */
static void
d12_ppp_push_stencil(struct agx_ppp_update *ppp,
                     const struct d12_stencil_face *face,
                     const struct d12_depth_stencil_state *ds, bool enabled,
                     bool read_only)
{
   agx_ppp_push(ppp, FRAGMENT_STENCIL, cfg) {
      if (enabled) {
         cfg.compare = face->compare;
         cfg.read_mask = ds->stencil_read_mask;
         cfg.write_mask = read_only ? 0 : ds->stencil_write_mask;
         cfg.depth_pass = face->pass;
         cfg.depth_fail = face->depth_fail;
         cfg.stencil_fail = face->stencil_fail;
      } else {
         cfg.compare = AGX_ZS_FUNC_ALWAYS;
         cfg.read_mask = 0xFF;
         cfg.write_mask = 0xFF;
         cfg.depth_pass = AGX_STENCIL_OP_KEEP;
         cfg.depth_fail = AGX_STENCIL_OP_KEEP;
         cfg.stencil_fail = AGX_STENCIL_OP_KEEP;
      }
   }
}

/*
 * Emit one draw into the open render segment's VDM stream: vertex-shader
 * state, the per-draw PPP block, viewport/scissor, then the draw itself.
 */
/*
 * The rasterizer's object type. It is NOT cosmetic and NOT implied by the
 * VDM primitive: it tells the rasterizer what shape to fill. It was
 * hardcoded to TRIANGLE, so a point list rasterised as triangles --
 * measured as zero pixels without a geometry shader and the ENTIRE render
 * target with one. hk derives it the same way (hk_cmd_draw.c:2158).
 */
static enum agx_object_type
d12_object_type(enum mesa_prim prim)
{
   if (prim == MESA_PRIM_POINTS)
      return AGX_OBJECT_TYPE_POINT_SPRITE_UV01;
   else if (prim <= MESA_PRIM_LINE_STRIP)
      return AGX_OBJECT_TYPE_LINE;
   else
      return AGX_OBJECT_TYPE_TRIANGLE;
}

/* The hardware primitive for a mesa_prim. */
static enum agx_primitive
d12_agx_primitive(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:
      return AGX_PRIMITIVE_POINTS;
   case MESA_PRIM_LINES:
      return AGX_PRIMITIVE_LINES;
   case MESA_PRIM_LINE_STRIP:
      return AGX_PRIMITIVE_LINE_STRIP;
   case MESA_PRIM_TRIANGLE_STRIP:
      return AGX_PRIMITIVE_TRIANGLE_STRIP;
   default:
      return AGX_PRIMITIVE_TRIANGLES;
   }
}

static bool
d12_list_dispatch_gs_stage(struct d12_cmd_list *list,
                           struct d12_segment *seg, uint64_t va,
                           const struct agx_shader_info *info,
                           struct agx_grid grid, struct agx_workgroup wg,
                           const struct d12_dispatch_uniforms *u,
                           D3D12_SHADER_VISIBILITY visibility)
{
   if (!va || !info->binary_size ||
       !d12_segment_reserve_scratch(list, seg, D12_SCRATCH_CS, info))
      return false;

   uint32_t usc = d12_list_build_compute_usc(
      list, va, info, u, false, visibility, NULL);
   if (!usc)
      return false;

   if (list->pregfx_cur + 0x100 > list->pregfx_end) {
      mesa_loge("pre-graphics stream full");
      list->state = D12_LIST_ERROR;
      return false;
   }

   struct agx_cdm_launch_word_0_packed launch =
      d12_compute_launch_word(info);
   list->pregfx_cur = (uint8_t *)agx_cdm_launch(
      (uint32_t *)list->pregfx_cur, list->device->dev.chip, grid, wg,
      launch, usc);
   list->pregfx_cur = (uint8_t *)agx_cdm_barrier(
      (uint32_t *)list->pregfx_cur, list->device->dev.chip);
   seg->dispatches++;
   return true;
}

/*
 * Run a geometry shader's compute half and report what the rasterization
 * draw becomes.
 *
 * For transform feedback, this includes the count, optional prefix-sum and
 * pre-GS programs before the ordinary geometry body. When XFB is absent,
 * the same path remains the two-dispatch software-VS/GS sequence.
 *
 * The dispatches go in the PRE-GRAPHICS stream so they land before the
 * open render pass rather than ending it.
 */
static bool
d12_list_gs_prerast(struct d12_cmd_list *list, struct d12_pipeline_state *pso,
                    unsigned vertex_count, unsigned instance_count,
                    bool indexed, unsigned first_index,
                    const struct agx_draw *upstream_draw,
                    uint64_t root_gpu, uint64_t textures_gpu,
                    uint64_t pbes_gpu,
                    uint64_t draw_gpu, unsigned *out_verts,
                    unsigned *out_instances, struct agx_draw *out_draw,
                    struct d12_dispatch_uniforms *out_poly)
{
   /* 64 threads, and this value has to agree in three places: both
    * parameter blocks bake it, and the launch below uses it. Nothing
    * cross-checks them.
    */
   const uint32_t wg_size[3] = {64, 1, 1};
   struct agx_workgroup wg = agx_workgroup(wg_size[0], wg_size[1], wg_size[2]);

   /* The software VS writes one vec4 per live output per vertex; the GS
    * reads it back. Zero outputs would mean a zero-size allocation, so
    * keep at least one word rather than handing the shader a null.
    */
   const bool indirect =
      upstream_draw && agx_is_indirect(upstream_draw->b);
   const uint64_t sw_outputs =
      pso->has_tess ? pso->sw_tes_outputs : pso->sw_vs_outputs;
   const enum mesa_prim input_prim =
      pso->has_tess
         ? (pso->tess_points
               ? MESA_PRIM_POINTS
               : pso->tess_mode == TESS_PRIMITIVE_ISOLINES
                    ? MESA_PRIM_LINES
                    : MESA_PRIM_TRIANGLES)
         : list->topology;

   struct poly_vertex_params vp;
   poly_vertex_params_init(&vp, sw_outputs, wg_size);
   if (!indirect)
      poly_vertex_params_set_draw(&vp, vertex_count, instance_count);

   if (!indirect && indexed) {
      enum agx_index_size index_size = list->ib.addr
                                          ? list->ib.index_size
                                          : AGX_INDEX_SIZE_U16;
      unsigned index_size_B = agx_index_size_to_B(index_size);
      unsigned range_el = list->ib.size_B / index_size_B;

      vp.index_size_B = index_size_B;
      if (list->ib.addr && first_index < range_el) {
         vp.index_buffer = list->ib.addr +
                           (uint64_t)first_index * index_size_B;
         vp.index_buffer_range_el = range_el - first_index;
      } else {
         /* poly_load_index still performs one physical load before it
          * substitutes zero for an OOB element, so an empty/null view needs
          * mapped zero storage rather than VA 0.
          */
         vp.index_buffer = list->device->null_va;
         vp.index_buffer_range_el = 0;
      }
   }

   size_t out_size = 0;
   if (!indirect) {
      out_size = poly_tcs_in_size((uint64_t)vertex_count * instance_count,
                                  sw_outputs);
      vp.output_buffer = d12_list_upload_zero(list, MAX2(out_size, 16), 16);
      if (!vp.output_buffer)
         return false;
   }

   /* Remember it so the vertex buffer can be read back once the GPU is
    * idle. Whether the software vertex shader actually wrote what the
    * rasterization program then reads is the one question a command
    * stream dump cannot answer.
    */
   if (!indirect && getenv("D3D12AGX_DUMP_GS_VB")) {
      list->device->dbg_gs_vb = d12_list_data_cpu(list, vp.output_buffer);
      list->device->dbg_gs_vb_size = MAX2(out_size, 16);
      mesa_logi("gs vb: gpu=0x%llx cpu=%p size=%zu outputs=0x%llx "
                "verts=%u insts=%u",
                (unsigned long long)vp.output_buffer,
                list->device->dbg_gs_vb, list->device->dbg_gs_vb_size,
                (unsigned long long)sw_outputs, vertex_count,
                instance_count);
   }

   /* poly's software input assembly always emits the index-buffer pull,
    * predicated on index_size != 0. We draw non-indexed, so the branch is
    * never taken -- but the pointer is still materialised, and hk never
    * leaves it null: it points it at a sink address even when unused
    * (hk_index_buffer). Leaving it zero puts a null dereference in the
    * shader, and a fault aborts the whole command buffer, which presents
    * as the compute half silently doing nothing.
    */
   if (!vp.index_buffer)
      vp.index_buffer = list->device->null_va;

   struct poly_geometry_params gp;
   poly_geometry_params_init(&gp, input_prim, wg_size);
   if (!indirect) {
      poly_geometry_params_set_draw(&gp, input_prim, pso->gs_info.shape,
                                    pso->gs_info.max_indices, vertex_count,
                                    instance_count);
   }

   /* poly's pre-GS program always dereferences the query/counter pointers,
    * even when D3D12 has no corresponding query active. Give every unused
    * field its own writable word in the command-list BO rather than a null
    * address. This is also the sink for an unbound SO slot's offset.
    */
   uint64_t sink = d12_list_upload_zero(list, 128, 8);
   if (!sink)
      return false;

   unsigned sink_word = 0;
   for (unsigned i = 0; i < POLY_MAX_VERTEX_STREAMS; i++) {
      gp.prims_generated_counter[i] = sink + 4 * sink_word++;
      gp.xfb_prims_generated_counter[i] = sink + 4 * sink_word++;
      gp.xfb_overflow[i] = sink + 4 * sink_word++;

      if (i < ARRAY_SIZE(list->active_so_stats_heap) &&
          list->active_so_stats_heap[i]) {
         struct d12_query_heap *heap = list->active_so_stats_heap[i];
         uint64_t result = heap->bo->va->addr +
                           (uint64_t)list->active_so_stats_index[i] *
                              heap->stride;

         /* D3D12_QUERY_DATA_SO_STATISTICS is two uint64_t fields:
          * {written, storage-needed}. Poly increments uint32_t counters;
          * BeginQuery has already zeroed both complete fields.
          */
         gp.xfb_prims_generated_counter[i] = result;
         gp.prims_generated_counter[i] = result + sizeof(uint64_t);
      }
   }
   gp.xfb_any_overflow = sink + 4 * sink_word++;

   for (unsigned i = 0; i < POLY_MAX_SO_BUFFERS; i++) {
      const D3D12_STREAM_OUTPUT_BUFFER_VIEW *view = &list->so[i];
      bool bound = pso->gs_info.xfb && view->BufferLocation &&
                   view->SizeInBytes && view->BufferFilledSizeLocation;

      if (bound) {
         gp.xfb_base_original[i] = view->BufferLocation;
         gp.xfb_size[i] = view->SizeInBytes;
         gp.xfb_offs_ptrs[i] = view->BufferFilledSizeLocation;
      } else {
         gp.xfb_base_original[i] = sink + 4 * sink_word++;
         gp.xfb_size[i] = 0;
         gp.xfb_offs_ptrs[i] = sink + 4 * sink_word++;
      }
   }

   gp.count_buffer_stride = pso->gs_info.count_words * 4;
   if (!indirect && gp.count_buffer_stride) {
      /* With prefix sums, each input primitive owns one count record. The
       * fixed-count form only needs poly's documented 16-byte scratch.
       */
      size_t count_size = pso->gs_info.prefix_sum
                             ? (size_t)gp.input_primitives *
                                  gp.count_buffer_stride
                             : 16;
      gp.count_buffer =
         d12_list_upload_zero(list, MAX2(count_size, 16), 4);
      if (!gp.count_buffer)
         return false;
   }

   uint64_t static_index_buffer = 0;
   size_t static_index_size = 0;
   if (pso->gs_info.shape == POLY_GS_SHAPE_DYNAMIC_INDEXED && !indirect) {
      size_t index_size = (size_t)gp.draw.index_count * sizeof(uint32_t);
      if (!index_size || index_size > UINT32_MAX) {
         mesa_loge("geometry output index buffer size %zu is invalid",
                   index_size);
         list->state = D12_LIST_ERROR;
         return false;
      }

      gp.output_index_buffer = d12_list_upload_zero(list, index_size, 4);
      if (!gp.output_index_buffer)
         return false;

      *out_draw = agx_draw_indexed(
         gp.draw.index_count, gp.draw.instance_count, 0, 0, 0,
         gp.output_index_buffer, index_size, AGX_INDEX_SIZE_U32, false);
   } else if (pso->gs_info.shape == POLY_GS_SHAPE_STATIC_INDEXED) {
      /* Poly proved the emitted topology at compile time. Its compact table
       * is a byte index buffer (0xff is primitive restart), instanced once
       * per input primitive. Uploading that table avoids the dynamic index
       * writer while preserving arbitrary static strip breaks. */
      static_index_size = pso->gs_info.max_indices;
      static_index_buffer = d12_list_upload(
         list, pso->gs_info.topology, static_index_size, 1);
      if (!static_index_buffer)
         return false;

      if (!indirect) {
         *out_draw = agx_draw_indexed(
            gp.draw.index_count, gp.draw.instance_count, 0, 0, 0,
            static_index_buffer, static_index_size, AGX_INDEX_SIZE_U8, true);
      }
   }

   if (getenv("D3D12AGX_DUMP_SO")) {
      mesa_logi("SO draw: xfb=%u prefix=%u count_words=%u count=0x%llx "
                "stride=%u input_prims=%u grid=%ux%u shape=%u",
                pso->gs_info.xfb, pso->gs_info.prefix_sum,
                pso->gs_info.count_words,
                (unsigned long long)gp.count_buffer,
                gp.count_buffer_stride, gp.input_primitives, gp.grid[0],
                gp.grid[1], pso->gs_info.shape);
      for (unsigned i = 0; i < POLY_MAX_SO_BUFFERS; i++) {
         mesa_logi("  SO%u: base=0x%llx size=%u filled=0x%llx",
                   i, (unsigned long long)gp.xfb_base_original[i],
                   gp.xfb_size[i],
                   (unsigned long long)gp.xfb_offs_ptrs[i]);
      }
   }

   /* Both blocks are read through 64-bit constant loads whose offsets the
    * shaders bake, so the alignment is not cosmetic.
    */
   uint64_t vp_gpu = d12_list_upload(list, &vp, sizeof(vp), 8);
   uint64_t gp_gpu = d12_list_upload(list, &gp, sizeof(gp), 8);
   if (!vp_gpu || !gp_gpu)
      return false;

   if (indirect && getenv("D3D12AGX_VALIDATE_BINDINGS")) {
      const uint64_t heap_base = list->device->heap_bo->va->addr;
      const uint64_t heap_size = list->device->heap_bo->size;
      bool valid =
         d12_list_stream_contains(list, upstream_draw->b.ptr,
                                  sizeof(struct poly_indirect_draw)) &&
         d12_list_stream_contains(list, vp_gpu, sizeof(vp)) &&
         d12_list_stream_contains(list, gp_gpu, sizeof(gp)) &&
         upstream_draw->index_buffer == heap_base &&
         upstream_draw->index_buffer_range_B <= heap_size;

      mesa_logi("tess->GS validate: draw=0x%llx vp=0x%llx gp=0x%llx "
                "index=[0x%llx,+0x%x] heap=[0x%llx,+0x%llx] valid=%d",
                (unsigned long long)upstream_draw->b.ptr,
                (unsigned long long)vp_gpu, (unsigned long long)gp_gpu,
                (unsigned long long)upstream_draw->index_buffer,
                upstream_draw->index_buffer_range_B,
                (unsigned long long)heap_base,
                (unsigned long long)heap_size, valid);
      if (!valid) {
         list->state = D12_LIST_ERROR;
         return false;
      }
   }

   /* THE INDIRECTION. agx_usc_uniform fills uniform registers FROM
    * memory, so publishing a POINTER means uploading the pointer and
    * pointing the uniform at that -- the address-of-an-address the root
    * table already uses (docs/findings.md).
    *
    * Pointing the uniform straight at the parameter block instead makes
    * the shader load the block's first eight bytes AS the block address:
    * poly_vertex_params begins with index_buffer, so the software vertex
    * shader took the index buffer pointer to be its own parameter block
    * and wrote nothing anywhere.
    *
    * VS_OUTPUTS is not like this: it is a VALUE (the output mask), so the
    * uniform correctly points at the mask itself.
    */
   uint64_t vp_ptr = d12_list_upload(list, &vp_gpu, sizeof(vp_gpu), 8);
   uint64_t gp_ptr = d12_list_upload(list, &gp_gpu, sizeof(gp_gpu), 8);
   if (!vp_ptr || !gp_ptr)
      return false;

   /* The mask lives in memory because the uniform is filled from memory. */
   uint64_t outputs_mask = sw_outputs;
   uint64_t outputs_gpu =
      d12_list_upload(list, &outputs_mask, sizeof(outputs_mask), 8);
   if (!outputs_gpu)
      return false;

   struct d12_dispatch_uniforms u = {
      .root = root_gpu,
      .textures = textures_gpu,
      .pbes = pbes_gpu,
      .heap = out_poly->heap,
      .heap_textures = out_poly->heap_textures,
      .heap_pbes = out_poly->heap_pbes,
      .heap_samplers = out_poly->heap_samplers,
      /* poly_nir_lower_sw_vs reads the base vertex from here. */
      .draw = draw_gpu,
      .vertex_params = vp_ptr,
      .geom_params = gp_ptr,
      .tess_params = out_poly->tess_params,
      .vs_outputs = outputs_gpu,
      /* Published by the caller BEFORE this function runs: the
       * software VS fetches attributes through it. */
      .vb_params = out_poly->vb_params,
   };

   struct d12_segment *seg = d12_list_get_pregfx(list);
   if (!seg)
      return false;

   struct agx_grid sw_grid = agx_3d(vp.grid[0], vp.grid[1], 1);
   struct agx_grid gs_grid = agx_3d(gp.grid[0], gp.grid[1], 1);

   if (indirect) {
      if (!upstream_draw->indexed) {
         mesa_loge("geometry after tessellation requires the indexed "
                   "tessellator draw");
         list->state = D12_LIST_ERROR;
         return false;
      }

      list->uses_heap = true;
      struct libagx_gs_setup_indirect_args args = {
         .index_buffer = upstream_draw->index_buffer,
         .draw = upstream_draw->b.ptr,
         .vp = vp_gpu,
         .p = gp_gpu,
         .heap = list->device->heap_desc_va,
         .vs_outputs = sw_outputs,
         .index_size_B = agx_index_size_to_B(upstream_draw->index_size),
         .index_buffer_range_el =
            upstream_draw->index_buffer_range_B /
            agx_index_size_to_B(upstream_draw->index_size),
         .prim = input_prim,
         .is_prefix_summing = pso->gs_info.prefix_sum,
         .max_indices = pso->gs_info.max_indices,
         .shape = pso->gs_info.shape,
      };
      d12_list_dispatch_precomp_to(list, LIBAGX_GS_SETUP_INDIRECT, &args,
                                   sizeof(args), agx_1d(1), true);
      if (list->state != D12_LIST_RECORDING)
         return false;

      sw_grid = agx_grid_indirect_local(
         vp_gpu + offsetof(struct poly_vertex_params, grid));
      gs_grid = agx_grid_indirect_local(
         gp_gpu + offsetof(struct poly_geometry_params, grid));

      uint64_t indirect_draw =
         gp_gpu + offsetof(struct poly_geometry_params, draw);
      if (pso->gs_info.shape == POLY_GS_SHAPE_DYNAMIC_INDEXED) {
         *out_draw = agx_draw_indexed_indirect(
            indirect_draw, list->device->heap_bo->va->addr,
            list->device->heap_bo->size, AGX_INDEX_SIZE_U32, true);
      } else if (pso->gs_info.shape == POLY_GS_SHAPE_STATIC_INDEXED) {
         *out_draw = agx_draw_indexed_indirect(
            indirect_draw, static_index_buffer, static_index_size,
            AGX_INDEX_SIZE_U8, true);
      } else {
         *out_draw = agx_draw_indirect(indirect_draw);
      }
   }

   /* Software VS/TES first. Grids are in THREADS, not workgroups -- unlike the
    * API dispatch path, which multiplies. Each helper appends a full barrier
    * because every following program consumes the previous one's output.
    */
   if (!d12_list_dispatch_gs_stage(
          list, seg, pso->has_tess ? pso->sw_tes_va : pso->sw_vs_va,
          pso->has_tess ? &pso->sw_tes_info : &pso->sw_vs_info,
          sw_grid, wg, &u,
          pso->has_tess ? D3D12_SHADER_VISIBILITY_DOMAIN
                        : D3D12_SHADER_VISIBILITY_VERTEX))
      return false;

   if (pso->gs_info.xfb) {
      if (pso->gs_info.count_words &&
          !d12_list_dispatch_gs_stage(
             list, seg, pso->gs_count_va, &pso->gs_count_info,
             gs_grid, wg, &u,
             D3D12_SHADER_VISIBILITY_GEOMETRY))
         return false;

      if (pso->gs_info.prefix_sum) {
         struct libagx_prefix_sum_geom_args args = {.p = gp_gpu};
         d12_list_dispatch_precomp_to(
            list, LIBAGX_PREFIX_SUM_GEOM, &args, sizeof(args),
            agx_1d(1024 * pso->gs_info.count_words), true);
         if (list->state != D12_LIST_RECORDING)
            return false;
      }

      if (!d12_list_dispatch_gs_stage(
             list, seg, pso->gs_pre_va, &pso->gs_pre_info, agx_1d(1),
             agx_workgroup(1, 1, 1), &u,
             D3D12_SHADER_VISIBILITY_GEOMETRY))
         return false;
   }

   if (!d12_list_dispatch_gs_stage(
          list, seg, pso->gs_main_va, &pso->gs_main_info,
          gs_grid, wg, &u,
          D3D12_SHADER_VISIBILITY_GEOMETRY))
      return false;

   /* The rasterization draw's extent is closed-form in the shape, not a
    * GPU-written count -- that is only needed for indirect draws.
    */
   *out_verts = indirect ? 0 : gp.draw.index_count;
   *out_instances = indirect ? 0 : gp.draw.instance_count;
   *out_poly = u;
   return true;
}

/*
 * Run a tessellation pipeline's compute half and hand back the indexed
 * indirect draw that rasterises the result.
 *
 * Five dispatches, in this order, all into the pre-graphics stream:
 *
 *   1. the software vertex shader, writing the VS output buffer
 *   2. the hull shader, one workgroup per patch, writing the TCS buffer
 *   3. the tessellator in COUNT mode, writing only per-patch counts
 *   4. a prefix sum over those counts, which also allocates the index
 *      buffer out of the heap and writes the indirect draw descriptor
 *   5. the tessellator again in WITH_COUNTS mode, now emitting domain
 *      points and indices
 *
 * Steps 3 and 5 are the same kernel run twice on purpose: the counts are
 * prefix-summed IN PLACE between them, and step 5 reads counts[patch-1]
 * as its write offset. It cannot be fused (hk does the same).
 */

/*
 * VERTEX ATTRIBUTE FETCH parameters. The PSO resolved which signature
 * element each attribute feeds and where inside a vertex it sits; the view
 * supplies the address and the stride. agx_calculate_vbo_clamp folds the
 * attribute's byte offset into the base and returns the highest in-bounds
 * element index, and it is also what redirects an out-of-range attribute
 * at the zero page rather than at nothing.
 *
 * Returns 0 when the pipeline fetches no attributes, or
 * D12_VB_PARAMS_FAILED if an upload failed. Callers must publish this
 * BEFORE any pre-rasterisation compute dispatch -- see the call site.
 */
static uint64_t
d12_list_upload_vb_params(struct d12_cmd_list *list,
                          struct d12_pipeline_state *pso)
{
   if (!pso->nr_vb_attribs)
      return 0;

   struct d12_vb_params vbp;
   memset(&vbp, 0, sizeof(vbp));

   for (unsigned i = 0; i < pso->nr_vb_attribs; i++) {
      const struct d12_vb_attrib *a = &pso->vb_attribs[i];

      if (!a->live || !list->vb[a->slot].addr) {
         vbp.base[i] = AGX_ZERO_PAGE_ADDRESS;
         continue;
      }

      vbp.clamp[i] = agx_calculate_vbo_clamp(
         list->vb[a->slot].addr, a->format, list->vb[a->slot].size_B,
         list->vb[a->slot].stride_B, a->offset_B, &vbp.base[i]);

      /* The shader multiplies the element index by this directly, so
       * it is in interchange elements rather than bytes. */
      vbp.stride_el[i] =
         a->interchange_align
            ? list->vb[a->slot].stride_B / a->interchange_align
            : 0;
   }

   if (getenv("D3D12AGX_DUMP_VB")) {
      for (unsigned i = 0; i < pso->nr_vb_attribs; i++) {
         const struct d12_vb_attrib *a = &pso->vb_attribs[i];
         mesa_logi("vb attrib %u: live=%d slot=%u off=%u ialign=%u "
                   "-> base=0x%llx clamp=%u stride_el=%u "
                   "(vb addr=0x%llx size=%u stride=%u)",
                   i, a->live, a->slot, a->offset_B, a->interchange_align,
                   (unsigned long long)vbp.base[i], vbp.clamp[i],
                   vbp.stride_el[i],
                   (unsigned long long)list->vb[a->slot].addr,
                   list->vb[a->slot].size_B, list->vb[a->slot].stride_B);
      }
   }

   uint64_t vbp_gpu = d12_list_upload(list, &vbp, sizeof(vbp), 8);
   if (!vbp_gpu)
      return D12_VB_PARAMS_FAILED;

   uint64_t ptr = d12_list_upload(list, &vbp_gpu, sizeof(vbp_gpu), 8);
   return ptr ? ptr : D12_VB_PARAMS_FAILED;
}

static bool
d12_list_tess_prerast(struct d12_cmd_list *list,
                      struct d12_pipeline_state *pso, unsigned vertex_count,
                      unsigned instance_count, uint64_t root_gpu,
                      uint64_t textures_gpu, uint64_t pbes_gpu,
                      uint64_t draw_gpu,
                      struct agx_draw *out_draw,
                      struct d12_dispatch_uniforms *out_poly)
{
   struct d12_device *device = list->device;

   if (!pso->tcs_output_patch_size || !pso->tcs_output_stride) {
      mesa_loge("tessellation pipeline has no patch layout");
      list->state = D12_LIST_ERROR;
      return false;
   }

   /* D3D12 carries the input patch size in the topology; the control
    * point count is the only thing API state contributes here.
    */
   unsigned input_patch_size = list->patch_control_points;
   if (!input_patch_size) {
      mesa_loge("draw with a tessellation pipeline but no patch topology; "
                "call IASetPrimitiveTopology with a control-point patch list");
      list->state = D12_LIST_ERROR;
      return false;
   }

   unsigned in_patches = vertex_count / input_patch_size;
   unsigned unrolled = in_patches * instance_count;
   if (!unrolled) {
      mesa_loge("tessellation draw covers no whole patches");
      list->state = D12_LIST_ERROR;
      return false;
   }

   const uint32_t wg_size[3] = {64, 1, 1};

   /* The VS prepass and its output buffer, exactly as the geometry path
    * builds them: the hull shader reads this.
    */
   struct poly_vertex_params vp;
   poly_vertex_params_init(&vp, pso->sw_vs_outputs, wg_size);
   poly_vertex_params_set_draw(&vp, vertex_count, instance_count);

   size_t vsout = poly_tcs_in_size((uint64_t)vertex_count * instance_count,
                                   pso->sw_vs_outputs);
   vp.output_buffer = d12_list_upload_zero(list, MAX2(vsout, 16), 16);
   if (!vp.output_buffer)
      return false;

   /* Never null: the software input assembly materialises this pointer
    * even on the non-indexed path it does not take. */
   vp.index_buffer = vp.output_buffer;

   /* One blob for the tessellator's scratch, carved by offset the way hk
    * does it. out_draws is the 5-word indexed draw the GPU fills in.
    */
   size_t tcs_out_B = (size_t)unrolled * pso->tcs_output_stride;
   size_t coord_B = (size_t)unrolled * 4;
   size_t count_B = (size_t)unrolled * 4;
   size_t draw_B = 5 * 4;

   uint64_t tcs_buffer = d12_list_upload_zero(list, tcs_out_B, 16);
   uint64_t coord_allocs = d12_list_upload_zero(list, coord_B, 4);
   uint64_t counts = d12_list_upload_zero(list, count_B, 4);
   uint64_t out_draws = d12_list_upload_zero(list, draw_B, 4);
   if (!tcs_buffer || !coord_allocs || !counts || !out_draws)
      return false;

   /* This list bump-allocates from the device tessellation heap, so it owes
    * the heap a GPU-side reset before it finishes. See d12_list_Close.
    */
   list->uses_heap = true;

   struct poly_tess_params tp = {
      .heap = (DEVICE(struct poly_heap))device->heap_desc_va,
      .patch_coord_buffer =
         (DEVICE(struct poly_tess_point))device->heap_bo->va->addr,
      .coord_allocs = (DEVICE(uint32_t))coord_allocs,
      .out_draws = (DEVICE(uint32_t))out_draws,
      .tcs_buffer = (DEVICE(float))tcs_buffer,
      .counts = (DEVICE(uint32_t))counts,
      /* index_buffer is GPU-written by the prefix sum. */
      /* No pipeline-statistics query is active. Pointing this at the heap
       * descriptor corrupts its 64-bit base when the tessellator performs
       * the documented atomic increment before allocating domain points. */
      .statistic = 0,
      .tcs_per_vertex_outputs = pso->tcs_per_vertex_outputs,
      .input_patch_size = input_patch_size,
      .output_patch_size = pso->tcs_output_patch_size,
      .tcs_patch_constants = pso->tcs_nr_patch_outputs,
      .patches_per_instance = in_patches,
      .tcs_stride_el = pso->tcs_output_stride / 4,
      .nr_patches = unrolled,
      .points_mode = pso->tess_points,
      .isolines = pso->tess_mode == TESS_PRIMITIVE_ISOLINES,
      .ccw = pso->tess_ccw && !pso->tess_points &&
             pso->tess_mode != TESS_PRIMITIVE_ISOLINES,
   };

   switch (pso->tess_spacing) {
   case TESS_SPACING_EQUAL:
      tp.partitioning = POLY_TESS_PARTITIONING_INTEGER;
      break;
   case TESS_SPACING_FRACTIONAL_ODD:
      tp.partitioning = POLY_TESS_PARTITIONING_FRACTIONAL_ODD;
      break;
   default:
      tp.partitioning = POLY_TESS_PARTITIONING_FRACTIONAL_EVEN;
      break;
   }

   uint64_t vp_gpu = d12_list_upload(list, &vp, sizeof(vp), 8);
   uint64_t tp_gpu = d12_list_upload(list, &tp, sizeof(tp), 8);
   if (!vp_gpu || !tp_gpu)
      return false;

   /* Published BY REFERENCE: the uniform is filled FROM memory, so the
    * pointer itself has to live somewhere. See docs/findings.md.
    */
   uint64_t outputs_mask = pso->sw_vs_outputs;
   uint64_t vp_ptr = d12_list_upload(list, &vp_gpu, sizeof(vp_gpu), 8);
   uint64_t tp_ptr = d12_list_upload(list, &tp_gpu, sizeof(tp_gpu), 8);
   uint64_t vso_ptr =
      d12_list_upload(list, &outputs_mask, sizeof(outputs_mask), 8);
   if (!vp_ptr || !tp_ptr || !vso_ptr)
      return false;

   struct d12_dispatch_uniforms u = {
      .root = root_gpu,
      .textures = textures_gpu,
      .pbes = pbes_gpu,
      .heap = out_poly->heap,
      .heap_textures = out_poly->heap_textures,
      .heap_pbes = out_poly->heap_pbes,
      .heap_samplers = out_poly->heap_samplers,
      .draw = draw_gpu,
      .vertex_params = vp_ptr,
      .tess_params = tp_ptr,
      .vs_outputs = vso_ptr,
      /* Published by the caller BEFORE this function runs: the
       * software VS fetches attributes through it. */
      .vb_params = out_poly->vb_params,
   };

   struct d12_segment *seg = d12_list_get_pregfx(list);
   if (!seg)
      return false;

   /* 1 and 2: software VS, then the hull shader. The hull shader runs one
    * WORKGROUP per patch -- its workgroup size is the output patch size,
    * not 64, because a patch has to fit in one workgroup for the barrier
    * elision poly relies on.
    */
   const struct {
      uint64_t va;
      const struct agx_shader_info *info;
      struct agx_grid grid;
      struct agx_workgroup wg;
      D3D12_SHADER_VISIBILITY visibility;
   } stages[] = {
      {pso->sw_vs_va, &pso->sw_vs_info, agx_3d(vp.grid[0], vp.grid[1], 1),
       agx_workgroup(64, 1, 1), D3D12_SHADER_VISIBILITY_VERTEX},
      {pso->tcs_va, &pso->tcs_info,
       agx_3d(in_patches * pso->tcs_output_patch_size, instance_count, 1),
       agx_workgroup(pso->tcs_output_patch_size, 1, 1),
       D3D12_SHADER_VISIBILITY_HULL},
   };

   for (unsigned i = 0; i < ARRAY_SIZE(stages); i++) {
      if (!d12_segment_reserve_scratch(list, seg, D12_SCRATCH_CS,
                                       stages[i].info))
         return false;

      uint32_t usc = d12_list_build_compute_usc(list, stages[i].va,
                                                stages[i].info, &u, false,
                                                stages[i].visibility, NULL);
      if (!usc)
         return false;

      if (list->pregfx_cur + 0x100 > list->pregfx_end) {
         mesa_loge("pre-graphics stream full");
         list->state = D12_LIST_ERROR;
         return false;
      }

      struct agx_cdm_launch_word_0_packed launch =
         d12_compute_launch_word(stages[i].info);

      list->pregfx_cur = (uint8_t *)agx_cdm_launch(
         (uint32_t *)list->pregfx_cur, device->dev.chip, stages[i].grid,
         stages[i].wg, launch, usc);
      list->pregfx_cur = (uint8_t *)agx_cdm_barrier(
         (uint32_t *)list->pregfx_cur, device->dev.chip);
      seg->dispatches++;
   }

   /* 3, 4, 5: the tessellator twice with a prefix sum between. The mode
    * selects the kernel; the _0/_1 suffix is the COUNT/WITH_COUNTS
    * shader-key variant, compiled as separate binaries rather than a
    * runtime branch.
    */
   enum libagx_program count_prog, emit_prog;
   if (pso->tess_mode == TESS_PRIMITIVE_ISOLINES) {
      count_prog = LIBAGX_TESS_ISOLINE_0;
      emit_prog = LIBAGX_TESS_ISOLINE_1;
   } else if (pso->tess_mode == TESS_PRIMITIVE_QUADS) {
      count_prog = LIBAGX_TESS_QUAD_0;
      emit_prog = LIBAGX_TESS_QUAD_1;
   } else {
      count_prog = LIBAGX_TESS_TRI_0;
      emit_prog = LIBAGX_TESS_TRI_1;
   }

   struct {
      uint64_t params;
   } targs = {tp_gpu};

   d12_list_dispatch_precomp_to(list, count_prog, &targs, sizeof(targs),
                                agx_1d(unrolled), true);

   struct {
      uint64_t params;
      uint64_t c_prims;
      uint64_t c_inv;
   } sargs = {tp_gpu, 0, 0};

   d12_list_dispatch_precomp_to(list, LIBAGX_PREFIX_SUM_TESS_0, &sargs,
                                sizeof(sargs), agx_1d(1024), true);

   d12_list_dispatch_precomp_to(list, emit_prog, &targs, sizeof(targs),
                                agx_1d(unrolled), true);

   if (list->state != D12_LIST_RECORDING)
      return false;

   if (getenv("D3D12AGX_DUMP_TESS")) {
      /* The GPU faults here read a real but unmapped address that changes
       * every run, so the address alone says nothing. Print the extent of
       * every allocation this draw hands the tessellator, in the same run,
       * and the faulting address can be attributed to one of them (or to
       * none, which is its own answer).
       */
      mesa_logi("tess VA map:");
      mesa_logi("  heap        0x%llx..0x%llx",
                (unsigned long long)device->heap_bo->va->addr,
                (unsigned long long)(device->heap_bo->va->addr +
                                     device->heap_bo->size));
      mesa_logi("  heap_desc   0x%llx  (bottom now %u, size %u)",
                (unsigned long long)device->heap_desc_va,
                device->heap_desc_cpu ? device->heap_desc_cpu->bottom : 0,
                device->heap_desc_cpu ? device->heap_desc_cpu->size : 0);
      mesa_logi("  tcs_buffer  0x%llx..0x%llx  (%zu B)",
                (unsigned long long)tcs_buffer,
                (unsigned long long)(tcs_buffer + tcs_out_B), tcs_out_B);
      mesa_logi("  coord_alloc 0x%llx..0x%llx  (%zu B)",
                (unsigned long long)coord_allocs,
                (unsigned long long)(coord_allocs + coord_B), coord_B);
      mesa_logi("  counts      0x%llx..0x%llx  (%zu B)",
                (unsigned long long)counts,
                (unsigned long long)(counts + count_B), count_B);
      mesa_logi("  out_draws   0x%llx..0x%llx  (%zu B)",
                (unsigned long long)out_draws,
                (unsigned long long)(out_draws + draw_B), draw_B);
      mesa_logi("  vs_out      0x%llx..0x%llx  (%zu B)",
                (unsigned long long)vp.output_buffer,
                (unsigned long long)(vp.output_buffer + vsout), vsout);
      mesa_logi("  stream_bo   0x%llx..0x%llx",
                (unsigned long long)list->stream_bo->va->addr,
                (unsigned long long)(list->stream_bo->va->addr +
                                     list->stream_bo->size));

      list->device->dbg_gs_vb = d12_list_data_cpu(list, vp.output_buffer);
      list->device->dbg_gs_vb_size = MAX2(vsout, 16);
      list->device->dbg_tess_draw = d12_list_data_cpu(list, out_draws);
      list->device->dbg_tess_counts = d12_list_data_cpu(list, counts);
      list->device->dbg_tess_counts_n = unrolled;
      list->device->dbg_tcs_buf = d12_list_data_cpu(list, tcs_buffer);
      list->device->dbg_tcs_buf_n =
         unrolled * (pso->tcs_output_stride / 4);
      mesa_logi("tess: patches=%u unrolled=%u patch_size=%u out_patch=%u "
                "stride=%u mode=%u part=%u tcs_pv_out=0x%llx",
                in_patches, unrolled, input_patch_size,
                pso->tcs_output_patch_size, pso->tcs_output_stride,
                pso->tess_mode, tp.partitioning,
                (unsigned long long)pso->tcs_per_vertex_outputs);
   }

   /* The rasterisation draw is always indexed, always indirect and always
    * U32: the index count only exists after the GPU prefix sum. The WHOLE
    * heap is the index buffer and out_draws[2] carries the element offset
    * into it.
    */
   *out_draw = agx_draw_indexed_indirect(out_draws, device->heap_bo->va->addr,
                                         device->heap_bo->size,
                                         AGX_INDEX_SIZE_U32, false);
   *out_poly = u;
   return true;
}

/*
 * Run a mesh pipeline's compute half and report the draw it becomes.
 *
 * Three dispatches into the pre-graphics stream:
 *   1. the mesh shader itself, one workgroup per DispatchMesh group
 *   2. ms_setup, turning the GPU-written counts into an indirect draw
 *   3. (nothing else -- the rasterization VS is the hardware stage)
 *
 * The mesh shader writes its vertices, primitives and indices through
 * D12_UNIFORM_MESH_PARAMS. Unlike geometry and tessellation there is no
 * poly emulation behind this: the ABI is this driver's own.
 */
struct d12_mesh_stage {
   uint64_t va;
   const struct agx_shader_info *info;
   struct agx_grid grid;
   struct agx_workgroup wg;
   D3D12_SHADER_VISIBILITY visibility;
};

static bool
d12_list_mesh_prerast(struct d12_cmd_list *list,
                      struct d12_pipeline_state *pso, unsigned tg_x,
                      unsigned tg_y, unsigned tg_z, uint64_t root_gpu,
                      uint64_t textures_gpu, uint64_t pbes_gpu,
                      uint64_t draw_gpu,
                      struct agx_draw *out_draw,
                      struct d12_dispatch_uniforms *out_poly)
{
   struct d12_device *device = list->device;

   unsigned groups = tg_x * tg_y * tg_z;
   if (!groups) {
      mesa_loge("DispatchMesh with an empty thread group count");
      list->state = D12_LIST_ERROR;
      return false;
   }

   /* Worst case per workgroup, from the shader's declared maxima. D3D12
    * caps a mesh shader at 256 vertices and 256 primitives per group.
    */
   const unsigned max_verts = 256, max_prims = 256;
   unsigned nr_slots = MAX2(util_last_bit64(pso->ms_outputs), 1);

   /* Fixed stride: the translator addresses slot (sigId + row) within a
    * D12_MESH_MAX_SLOTS-wide record per vertex, so the buffer has to be
    * sized that way whatever the shader actually writes.
    */
   const size_t vtx_stride = D12_MESH_MAX_SLOTS * D12_MESH_SLOT_STRIDE;
   size_t verts_B = (size_t)groups * max_verts * vtx_stride;
   size_t prims_B = (size_t)groups * max_prims * vtx_stride;
   size_t idx_B = (size_t)groups * max_prims * 3 * 4;

   /* With an amplification shader the mesh grid is GPU-written, so the
    * worst case has to cover whatever it asks for. D3D12 caps
    * DispatchMesh at 2^22 groups total but a test-sized bound is what
    * the buffers can actually hold; anything larger is refused rather
    * than silently overrun.
    */
   if (pso->has_amp)
      groups = D12_MESH_MAX_AMP_GROUPS;

   uint64_t counts = d12_list_upload_zero(list, 8, 8);
   uint64_t vertices = d12_list_upload_zero(list, verts_B, 16);
   uint64_t primitives = d12_list_upload_zero(list, prims_B, 16);
   uint64_t indices = d12_list_upload_zero(list, idx_B, 4);
   uint64_t drawdesc = d12_list_upload_zero(list, 5 * 4, 4);
   uint64_t dispatch = d12_list_upload_zero(list, 3 * 4, 4);
   uint64_t gridbuf = d12_list_upload_zero(list, 6 * 4, 4);
   uint64_t payload =
      pso->as_payload_size
         ? d12_list_upload_zero(list, pso->as_payload_size, 16)
         : 0;
   if (!counts || !vertices || !primitives || !indices || !drawdesc ||
       !dispatch || !gridbuf)
      return false;

   /* The parameter block the shader indirects through. Laid out by
    * D12_MESH_OFF_*; the payload is null because amplification shaders
    * are refused at PSO creation.
    */
   uint64_t block[8] = {counts,   vertices, primitives, indices,
                        payload,  dispatch, drawdesc,   gridbuf};
   uint64_t mp_gpu = d12_list_upload(list, block, sizeof(block), 8);
   if (!mp_gpu)
      return false;

   /* Published BY REFERENCE, like every other parameter block. */
   uint64_t mp_ptr = d12_list_upload(list, &mp_gpu, sizeof(mp_gpu), 8);
   if (!mp_ptr)
      return false;

   struct d12_dispatch_uniforms u = {
      .root = root_gpu,
      .textures = textures_gpu,
      .pbes = pbes_gpu,
      .heap = out_poly->heap,
      .heap_textures = out_poly->heap_textures,
      .heap_pbes = out_poly->heap_pbes,
      .heap_samplers = out_poly->heap_samplers,
      .draw = draw_gpu,
      .mesh_params = mp_ptr,
      /* Published by the caller BEFORE this function runs: the
       * software VS fetches attributes through it. */
      .vb_params = out_poly->vb_params,
   };

   struct d12_segment *seg = d12_list_get_pregfx(list);
   if (!seg)
      return false;

   /* With an amplification shader the chain grows at the front: AS runs
    * on the DispatchMesh grid the application gave, writes the payload
    * and its own DispatchMesh(x,y,z), then as_setup turns that thread
    * GROUP count into the six-word indirect-local grid the mesh
    * dispatch consumes. Without one, the mesh grid is known on the CPU.
    */
   struct d12_mesh_stage stages[4];
   unsigned nr_stages = 0;

   if (pso->has_amp) {
      stages[nr_stages++] = (struct d12_mesh_stage){
         pso->as_va, &pso->as_info,
         agx_3d(tg_x * pso->as_workgroup[0], tg_y * pso->as_workgroup[1],
                tg_z * pso->as_workgroup[2]),
         agx_workgroup(pso->as_workgroup[0], pso->as_workgroup[1],
                       pso->as_workgroup[2]),
         D3D12_SHADER_VISIBILITY_AMPLIFICATION};

      stages[nr_stages++] = (struct d12_mesh_stage){
         pso->as_setup_va, &pso->as_setup_info, agx_3d(1, 1, 1),
         agx_workgroup(1, 1, 1), D3D12_SHADER_VISIBILITY_ALL};

      stages[nr_stages++] = (struct d12_mesh_stage){
         pso->ms_va, &pso->ms_info, agx_grid_indirect_local(gridbuf),
         agx_workgroup(pso->ms_workgroup[0], pso->ms_workgroup[1],
                       pso->ms_workgroup[2]), D3D12_SHADER_VISIBILITY_MESH};
   } else {
      stages[nr_stages++] = (struct d12_mesh_stage){
         pso->ms_va, &pso->ms_info,
         agx_3d(tg_x * pso->ms_workgroup[0], tg_y * pso->ms_workgroup[1],
                tg_z * pso->ms_workgroup[2]),
         agx_workgroup(pso->ms_workgroup[0], pso->ms_workgroup[1],
                       pso->ms_workgroup[2]), D3D12_SHADER_VISIBILITY_MESH};
   }

   stages[nr_stages++] = (struct d12_mesh_stage){
      pso->ms_setup_va, &pso->ms_setup_info, agx_3d(1, 1, 1),
      agx_workgroup(1, 1, 1), D3D12_SHADER_VISIBILITY_ALL};

   for (unsigned i = 0; i < nr_stages; i++) {
      if (!d12_segment_reserve_scratch(list, seg, D12_SCRATCH_CS,
                                       stages[i].info))
         return false;

      uint32_t usc = d12_list_build_compute_usc(list, stages[i].va,
                                                stages[i].info, &u, false,
                                                stages[i].visibility, NULL);
      if (!usc)
         return false;

      if (list->pregfx_cur + 0x100 > list->pregfx_end) {
         mesa_loge("pre-graphics stream full");
         list->state = D12_LIST_ERROR;
         return false;
      }

      struct agx_cdm_launch_word_0_packed launch =
         d12_compute_launch_word(stages[i].info);

      list->pregfx_cur = (uint8_t *)agx_cdm_launch(
         (uint32_t *)list->pregfx_cur, device->dev.chip, stages[i].grid,
         stages[i].wg, launch, usc);
      list->pregfx_cur = (uint8_t *)agx_cdm_barrier(
         (uint32_t *)list->pregfx_cur, device->dev.chip);
      seg->dispatches++;
   }

   if (list->state != D12_LIST_RECORDING)
      return false;

   if (getenv("D3D12AGX_DUMP_MESH")) {
      device->dbg_tess_draw = d12_list_data_cpu(list, drawdesc);
      device->dbg_tess_counts = d12_list_data_cpu(list, counts);
      device->dbg_tess_counts_n = 2;
      device->dbg_mesh_verts = d12_list_data_cpu(list, vertices);
      device->dbg_mesh_idx = d12_list_data_cpu(list, indices);
      device->dbg_mesh_grid = d12_list_data_cpu(list, gridbuf);
      device->dbg_mesh_disp = d12_list_data_cpu(list, dispatch);
      mesa_logi("mesh: groups=%u slots=%u vtx_stride=%zu idx_B=%zu amp=%d",
                groups, nr_slots, vtx_stride, idx_B, pso->has_amp);
   }

   *out_draw = agx_draw_indexed_indirect(drawdesc, indices, idx_B,
                                         AGX_INDEX_SIZE_U32, false);
   *out_poly = u;
   return true;
}

static bool
d12_strip_cut_enabled(const struct d12_pipeline_state *pso,
                      enum mesa_prim topology)
{
   if (topology != MESA_PRIM_LINE_STRIP &&
       topology != MESA_PRIM_TRIANGLE_STRIP)
      return false;

   return pso->strip_cut != D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
}

static void
d12_list_draw(struct d12_cmd_list *list, unsigned vertex_count,
              unsigned instance_count, unsigned first_vertex,
              unsigned first_instance, bool indexed, unsigned first_index,
              uint64_t indirect_args)
{
   struct d12_device *device = list->device;
   struct d12_pipeline_state *pso = list->pso;

   if (list->state != D12_LIST_RECORDING)
      return;

   /* Native D3D12 ignores a draw recorded before any PSO is bound.  This is
    * not a command-list recording error; later complete draws still execute.
    */
   if (!pso)
      return;

   if (pso->is_compute) {
      mesa_loge("Draw without a graphics pipeline state");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (list->active_pipeline_stats_heap && vertex_count && instance_count) {
      uint64_t vertices = (uint64_t)vertex_count * instance_count;
      uint64_t primitives;
      switch (list->topology) {
      case MESA_PRIM_POINTS:
         primitives = vertices;
         break;
      case MESA_PRIM_LINES:
         primitives = (vertex_count / 2u) * (uint64_t)instance_count;
         break;
      case MESA_PRIM_LINE_STRIP:
         primitives = vertex_count > 1
                         ? (uint64_t)(vertex_count - 1) * instance_count
                         : 0;
         break;
      case MESA_PRIM_TRIANGLE_STRIP:
         primitives = vertex_count > 2
                         ? (uint64_t)(vertex_count - 2) * instance_count
                         : 0;
         break;
      default:
         primitives = (vertex_count / 3u) * (uint64_t)instance_count;
         break;
      }

      list->pipeline_stats.IAVertices += vertices;
      list->pipeline_stats.IAPrimitives += primitives;
      list->pipeline_stats.VSInvocations += vertices;
      list->pipeline_stats.CInvocations += primitives;
      list->pipeline_stats.CPrimitives += primitives;
      if (pso->has_pixel_shader && primitives)
         list->pipeline_stats.PSInvocations++;
   }

   if (list->render_open) {
      struct d12_segment *open =
         &list->segments[list->segment_count - 1];
      if (!d12_segment_matches_bindings(list, open,
                                        pso->has_pixel_shader) ||
          memcmp(&open->tib, &pso->tib, sizeof(open->tib)) != 0)
         d12_list_close_cdm(list);
   }

   if (!list->render_open) {
      const unsigned rtv_count =
         pso->has_pixel_shader ? list->bound_rtv_count : 0;
      const struct d12_descriptor *rtvs =
         rtv_count ? list->bound_rtv : NULL;
      const struct d12_descriptor *dsv =
         list->has_bound_dsv ? &list->bound_dsv : NULL;
      if (!d12_list_begin_render(list, rtvs, rtv_count, dsv, pso,
                                 AGX_BG_LOAD, NULL, false))
         return;
   }

   struct d12_segment *seg = &list->segments[list->segment_count - 1];

   /* Root table, exactly as the dispatch path builds it -- from the
    * GRAPHICS bindings, which D3D12 keeps separate from the compute ones.
    */
   uint64_t root_gpu = 0, textures_gpu = 0, pbes_gpu = 0;
   uint64_t heap_gpu = 0, heap_textures_gpu = 0, heap_pbes_gpu = 0;
   uint64_t heap_samplers_gpu = 0;
   if (!d12_list_upload_bindings(list, false, &root_gpu, &textures_gpu,
                                 &pbes_gpu, &heap_gpu, &heap_textures_gpu,
                                 &heap_pbes_gpu, &heap_samplers_gpu))
      return;

   /* SM 6.8 draw parameters. agx_usc_uniform fills registers FROM memory.
    * The two D3D12 indirect structures both end in the exact pair this ABI
    * needs, so indirect draws can point into the application's buffer and
    * observe its contents at GPU execution time.
    */
   uint64_t draw_gpu;
   if (indirect_args) {
      draw_gpu = indirect_args + (indexed ? 12 : 8);
   } else {
      /* D3D robust null-index behaviour selects vertex zero. Applying a
       * BaseVertexLocation after substituting the zero index defeats that
       * guarantee (and native implementations explicitly ignore it here).
       */
      uint32_t draw_params[2] = {
         indexed && !list->ib.addr ? 0 : first_vertex,
         first_instance,
      };
      draw_gpu = d12_list_upload(list, draw_params, sizeof(draw_params), 8);
   }
   if (!draw_gpu)
      return;

   /* GEOMETRY. The compute half runs first, into the pre-graphics stream,
    * and hands back what the rasterization draw becomes. From here on the
    * "vertex shader" is the GS rasterization program, which reads the
    * emitted vertices rather than vertex attributes.
    */
   enum mesa_prim topology = list->topology;
   struct d12_dispatch_uniforms poly_u = {
      .heap = heap_gpu,
      .heap_textures = heap_textures_gpu,
      .heap_pbes = heap_pbes_gpu,
      .heap_samplers = heap_samplers_gpu,
   };

   /*
    * VERTEX ATTRIBUTE FETCH has to be published BEFORE the pre-rasterisation
    * compute half runs, not after it.
    *
    * The software VS that a tessellation, geometry or mesh pipeline
    * substitutes for the hardware vertex stage is lowered by
    * agx_nir_lower_vbo (compile_sw_vs_stage), so it FETCHES ATTRIBUTES
    * THROUGH THIS UNIFORM. But d12_list_*_prerast emit their dispatches
    * internally, and vb_params used to be filled in below them -- so the
    * software VS was dispatched with vb_params == 0, and
    * d12_list_build_compute_usc pushes that uniform only when it is
    * non-zero. The uniform registers were therefore never written and the
    * shader fetched through whatever the previous dispatch had left in
    * them: a garbage pointer, different every run, belonging to none of
    * our allocations.
    *
    * That is the fault that cost this campaign several days. It reads as
    * `unit: UL1C, read: true, reason: Unmapped` at an address in a region
    * we never map (0xbb..., against allocations at 0x2ff...), and it is
    * why test_quad_tessellation_dxil wedges the GPU while our own g7
    * tessellation probe does not -- g7 derives its geometry from
    * SV_VertexID and so never fetches an attribute at all.
    */
   uint64_t vb_params_gpu = d12_list_upload_vb_params(list, pso);
   if (vb_params_gpu == D12_VB_PARAMS_FAILED)
      return;
   poly_u.vb_params = vb_params_gpu;

   const struct d12_gfx_stage *vstage = &pso->vs;
   const struct agx_unlinked_uvs_layout *uvs = &pso->uvs;
   const struct agx_varyings_vs *varys = &pso->varyings;
   struct agx_draw draw = {0};
   bool draw_set = false;

   /* TESSELLATION. Same shape as geometry: the compute half runs first,
    * into the pre-graphics stream, and the DOMAIN shader becomes the
    * hardware vertex stage. Unlike geometry the draw itself is replaced
    * -- indexed and indirect, because the index count only exists after
    * the GPU prefix sum.
    */
   if (pso->has_mesh) {
      /* vertex_count/instance_count carry the DispatchMesh thread group
       * counts here -- a mesh draw has no vertices of its own.
       */
      if (!d12_list_mesh_prerast(list, pso, vertex_count, instance_count,
                                 first_vertex, root_gpu, textures_gpu,
                                 pbes_gpu, draw_gpu, &draw, &poly_u))
         return;

      seg = &list->segments[list->segment_count - 1];

      vstage = &pso->ms_rast;
      uvs = &pso->ms_uvs;
      varys = &pso->ms_varyings;
      topology = MESA_PRIM_TRIANGLES;
      draw_set = true;
   } else if (pso->has_tess) {
      if (!d12_list_tess_prerast(list, pso, vertex_count, instance_count,
                                 root_gpu, textures_gpu, pbes_gpu, draw_gpu,
                                 &draw,
                                 &poly_u))
         return;

      seg = &list->segments[list->segment_count - 1];

      vstage = &pso->tes;
      uvs = &pso->tes_uvs;
      varys = &pso->tes_varyings;
      topology = pso->tess_points  ? MESA_PRIM_POINTS
                 : pso->tess_mode == TESS_PRIMITIVE_ISOLINES
                    ? MESA_PRIM_LINES
                    : MESA_PRIM_TRIANGLES;
      draw_set = true;
   }

   if (pso->has_gs) {
      if (!d12_list_gs_prerast(list, pso, vertex_count, instance_count,
                               indexed, first_index,
                               pso->has_tess ? &draw : NULL,
                               root_gpu, textures_gpu, pbes_gpu, draw_gpu,
                               &vertex_count, &instance_count, &draw,
                               &poly_u))
         return;

      /* Inserting the pre-graphics segment shifted the render segment up
       * one slot, so any pointer taken before the call now aims at the
       * compute segment. Re-fetch it -- the symptom otherwise is "draw
       * into a render pass with no scissor array", which reads like a
       * missing scissor rather than a stale pointer.
       */
      seg = &list->segments[list->segment_count - 1];

      vstage = &pso->gs_rast;
      uvs = &pso->gs_uvs;
      varys = &pso->gs_varyings;
      topology = pso->gs_info.mode;
      first_vertex = 0;
      /* A geometry stage fed by tessellation is always driven by the
       * tessellator's GPU-written indirect draw, even when the geometry
       * lowering itself uses a non-indexed output shape. */
      draw_set = pso->has_tess || poly_gs_indexed(pso->gs_info.shape);
   }

   if (getenv("D3D12AGX_DUMP_MESH")) {
      mesa_logi("draw: has_mesh=%d mesh_params=0x%llx vstage=%s",
                pso->has_mesh, (unsigned long long)poly_u.mesh_params,
                vstage == &pso->ms_rast ? "ms_rast" : "other");
   }


   uint64_t blend_gpu =
      d12_list_upload(list, list->blend_factor, sizeof(list->blend_factor), 16);
   if (!blend_gpu)
      return;

   D3D12_SHADER_VISIBILITY vertex_visibility =
      vstage == &pso->tes        ? D3D12_SHADER_VISIBILITY_DOMAIN
      : vstage == &pso->gs_rast ? D3D12_SHADER_VISIBILITY_GEOMETRY
      : vstage == &pso->ms_rast ? D3D12_SHADER_VISIBILITY_MESH
                                  : D3D12_SHADER_VISIBILITY_VERTEX;

   if (!d12_segment_bind_sampler_heap(
          list, seg, vstage->info.uses_sampler_heap ||
                     pso->fs.info.uses_sampler_heap))
      return;

   if (!d12_segment_reserve_scratch(list, seg, D12_SCRATCH_VS,
                                    &vstage->info) ||
       !d12_segment_reserve_scratch(list, seg, D12_SCRATCH_FS,
                                    &pso->fs.info))
      return;

   uint32_t vs_usc = d12_gfx_stage_usc(
      list, vstage, false, &seg->tib, root_gpu, textures_gpu, pbes_gpu,
      heap_gpu, heap_textures_gpu, heap_pbes_gpu, heap_samplers_gpu,
      draw_gpu, 0, vertex_visibility,
      (pso->has_gs || pso->has_tess || pso->has_mesh || pso->nr_vb_attribs)
         ? &poly_u
         : NULL);
   uint32_t fs_usc = d12_gfx_stage_usc(list, &pso->fs, true, &seg->tib,
                                       root_gpu, textures_gpu, pbes_gpu,
                                       heap_gpu, heap_textures_gpu,
                                       heap_pbes_gpu, heap_samplers_gpu,
                                       draw_gpu, blend_gpu,
                                       D3D12_SHADER_VISIBILITY_PIXEL, NULL);
   if (!vs_usc || !fs_usc)
      return;

   /* Coefficient-register bindings linking VS outputs to FS inputs. */
   uint32_t varyings = 0;
   unsigned bindings = pso->fs.linked.cf.nr_bindings;
   if (bindings) {
      size_t size =
         AGX_CF_BINDING_HEADER_LENGTH + bindings * AGX_CF_BINDING_LENGTH;
      uint32_t link_usc;
      uint8_t *cpu = d12_list_alloc_usc(list, size, &link_usc);
      if (!cpu)
         return;

      bool generate_prim_id = false;
      agx_link_varyings_vs_fs(cpu, varys, uvs->user_size,
                              &pso->fs.linked.cf, 0, 0, &generate_prim_id);
      varyings = link_usc;
   }

   if (!d12_list_check_stream_space(list))
      return;

   uint8_t *out = list->stream_cur;

   /* Primitive restart has two independent pieces of VDM state: the draw's
    * enable bit and a persistent 32-bit sentinel register. The sentinel is
    * selected by the PSO, not inferred from the bound index width (D3D12
    * deliberately permits 0xffff with a 32-bit buffer and vice versa). */
   bool restart = indexed && d12_strip_cut_enabled(pso, topology);
   bool vdm_restart = draw_set ? draw.restart : restart;
   if (vdm_restart) {
      /* Poly-generated GS index buffers always carry their natural all-ones
       * restart sentinel. They are independent of the application's strip-cut
       * PSO state (and may exist for a non-indexed D3D draw), so key this from
       * the final draw rather than the original Draw[Indexed] call. Leaving
       * the persistent VDM sentinel at its reset value made generated index 0
       * look like a restart and dropped the first stream-output vertex. */
      uint32_t restart_index = draw_set
         ? BITFIELD_MASK(8 * agx_index_size_to_B(draw.index_size))
         : pso->strip_cut == D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF
              ? UINT16_MAX
              : UINT32_MAX;

      agx_push(out, VDM_STATE, cfg) {
         cfg.restart_index_present = true;
      }
      agx_push(out, VDM_STATE_RESTART_INDEX, cfg) {
         cfg.value = restart_index;
      }
   }

   /* Vertex shader state */
   agx_push(out, VDM_STATE, cfg) {
      cfg.vertex_shader_word_0_present = true;
      cfg.vertex_shader_word_1_present = true;
      cfg.vertex_outputs_present = true;
      cfg.vertex_unknown_present = true;
   }
   agx_push_packed(out, vstage->counts, VDM_STATE_VERTEX_SHADER_WORD_0);
   agx_push(out, VDM_STATE_VERTEX_SHADER_WORD_1, cfg) {
      cfg.pipeline = vs_usc;
   }
   agx_push_packed(out, uvs->vdm, VDM_STATE_VERTEX_OUTPUTS);
   agx_push(out, VDM_STATE_VERTEX_UNKNOWN, cfg);
   /* Pad to a multiple of 8 bytes */
   memset(out, 0, 4);
   out += 4;

   /* Viewport, scissor and depth bias */
   {
      struct AGX_PPP_HEADER present = {
         .depth_bias_scissor = true,
         .region_clip = true,
         .viewport = true,
         .viewport_count = 1,
      };

      size_t size = agx_ppp_update_size(&present);
      uint64_t gpu = d12_list_upload_zero(list, size, AGX_PPP_HEADER_ALIGN);
      uint8_t *cpu = d12_list_data_cpu(list, gpu);
      if (!cpu)
         return;

      struct agx_ppp_update ppp =
         agx_new_ppp_update((struct agx_ptr){.cpu = cpu, .gpu = gpu}, size,
                            &present);

      /* Honour RSSetScissorRects, clamped to the target. */
      unsigned clip_x0 = 0, clip_y0 = 0;
      unsigned clip_x1 = seg->width_px, clip_y1 = seg->height_px;

      if (list->has_scissor) {
         clip_x0 = MAX2(list->scissor.left, 0);
         clip_y0 = MAX2(list->scissor.top, 0);
         clip_x1 = MIN2((unsigned)MAX2(list->scissor.right, 0),
                        seg->width_px);
         clip_y1 = MIN2((unsigned)MAX2(list->scissor.bottom, 0),
                        seg->height_px);
      }

      /* Pack a hardware scissor entry. REGION_CLIP below is a coarse tile
       * bound; THIS is the exact one, and it also carries the depth clip
       * range, which is why a pass has to have it even when the
       * application never called RSSetScissorRects.
       */
      unsigned scissor_index = d12_list_push_scissor(
         list, seg, clip_x0, clip_y0, clip_x1, clip_y1);
      if (scissor_index == ~0u)
         return;

      agx_ppp_push(&ppp, DEPTH_BIAS_SCISSOR, cfg) {
         cfg.scissor = scissor_index;
      }

      agx_ppp_push(&ppp, REGION_CLIP, cfg) {
         cfg.enable = true;
         cfg.min_x = clip_x0 / 32;
         cfg.min_y = clip_y0 / 32;
         cfg.max_x = DIV_ROUND_UP(MAX2(clip_x1, 1), 32);
         cfg.max_y = DIV_ROUND_UP(MAX2(clip_y1, 1), 32);
      }

      agx_ppp_push(&ppp, VIEWPORT_CONTROL, cfg);

      /* Honour RSSetViewports. Same transform hk applies, but driven by
       * the viewport the application actually set rather than always the
       * whole target.
       */
      float vp_x = 0.0f, vp_y = 0.0f;
      float vp_w = seg->width_px, vp_h = seg->height_px;
      float vp_zmin = 0.0f, vp_zmax = 1.0f;

      if (list->has_viewport) {
         vp_x = list->viewport.TopLeftX;
         vp_y = list->viewport.TopLeftY;
         vp_w = list->viewport.Width;
         vp_h = list->viewport.Height;
         vp_zmin = list->viewport.MinDepth;
         vp_zmax = list->viewport.MaxDepth;
      }

      agx_ppp_push(&ppp, VIEWPORT, cfg) {
         cfg.translate_x = vp_x + 0.5f * vp_w;
         cfg.translate_y = vp_y + 0.5f * vp_h;
         cfg.translate_z = vp_zmin;

         cfg.scale_x = 0.5f * vp_w;

         /* Y is NEGATED relative to the Vulkan driver this code is modelled
          * on. Vulkan's clip space has +y pointing down the framebuffer, so
          * hk uses a positive scale; D3D12's +y points up, so NDC +1 must
          * land on the TOP row. Getting this wrong renders a vertically
          * mirrored image, which symmetric test geometry cannot detect.
          */
         cfg.scale_y = -0.5f * vp_h;
         cfg.scale_z = vp_zmax - vp_zmin;
      }

      agx_ppp_fini(&out, &ppp);
   }

   /* Fragment state */
   {
      /* Depth and stencil only test against something that exists. A PSO
       * asking for a depth test with no depth attached would otherwise
       * compare against whatever the ZLS unit last had pointed at it.
       */
      const struct d12_depth_stencil_state *ds = &pso->ds;
      const bool has_ds = seg->ds != NULL;
      const bool z_test = has_ds && ds->depth_test;
      const bool z_read_only =
         seg->dsv_flags & D3D12_DSV_FLAG_READ_ONLY_DEPTH;
      const bool s_read_only =
         seg->dsv_flags & D3D12_DSV_FLAG_READ_ONLY_STENCIL;
      const bool z_write = has_ds && ds->depth_write && !z_read_only;
      const bool s_test =
         has_ds && ds->stencil_test && seg->ds->stencil_bo != NULL;

      /* Field order is fixed by AGX_PPP_HEADER and the pushes below must
       * follow it exactly, or agx_ppp_fini catches a size mismatch.
       *
       * Both faces are pushed. Measured on this hardware (M2, G14G): with
       * two-sided state off, back-facing primitives use the FRONT slot --
       * a deliberately back-wound triangle depth-tests correctly even with
       * the back half left unwritten. The back half becomes load-bearing
       * the moment two_sided_stencil is set below, which is exactly when
       * D3D12 gives the two faces different stencil ops, so it is
       * programmed unconditionally rather than left to that coupling.
       */
      struct AGX_PPP_HEADER present = {
         .fragment_control = true,
         .fragment_control_2 = true,
         .fragment_front_face = true,
         .fragment_front_face_2 = true,
         .fragment_front_stencil = true,
         .fragment_back_face = true,
         .fragment_back_face_2 = true,
         .fragment_back_stencil = true,
         .output_select = true,
         .varying_counts_32 = true,
         .varying_counts_16 = true,
         .cull = true,
         .fragment_shader = true,
         .occlusion_query = true,
         .output_size = true,
         .viewport_count = 1,
      };

      size_t size = agx_ppp_update_size(&present);
      uint64_t gpu = d12_list_upload_zero(list, size, AGX_PPP_HEADER_ALIGN);
      uint8_t *cpu = d12_list_data_cpu(list, gpu);
      if (!cpu)
         return;

      struct agx_ppp_update ppp =
         agx_new_ppp_update((struct agx_ptr){.cpu = cpu, .gpu = gpu}, size,
                            &present);

      agx_ppp_push(&ppp, FRAGMENT_CONTROL, cfg) {
         cfg.visibility_mode = list->active_occlusion_queries
                                  ? AGX_VISIBILITY_MODE_COUNTING
                                  : AGX_VISIBILITY_MODE_NONE;
         /* Always on: the scissor entry the draw indexes carries the depth
          * clip range as well as the XY bounds, so this cannot be switched
          * off just because the application set no scissor rect.
          */
         cfg.scissor_enable = true;
         cfg.stencil_test_enable = s_test;
         cfg.two_sided_stencil = s_test;
      }

      /* The linker's own fragment control (tag writes etc.). */
      agx_ppp_push_packed(&ppp, &pso->fs.linked.fragment_control,
                          FRAGMENT_CONTROL);

      /* Depth function. AGX_ZS_FUNC_NEVER is 0, so leaving this unset
       * rejects every fragment — geometry rasterizes and nothing is
       * written. With no depth attachment the test must be ALWAYS.
       */
      struct agx_fragment_face_packed face;
      agx_pack(&face, FRAGMENT_FACE, cfg) {
         cfg.line_width = agx_pack_line_width(1.0f);
         cfg.polygon_mode = AGX_POLYGON_MODE_FILL;
         cfg.disable_depth_write = !z_write;
         cfg.depth_function = z_test ? ds->depth_func : AGX_ZS_FUNC_ALWAYS;
      }

      agx_ppp_push_merged(&ppp, FRAGMENT_FACE, cfg, face) {
         cfg.stencil_reference = list->stencil_ref;
      }

      struct agx_fragment_face_2_packed face_2;
      agx_pack(&face_2, FRAGMENT_FACE_2, cfg) {
         cfg.object_type = d12_object_type(topology);
      }
      agx_merge(face_2, pso->fs.frag_face, FRAGMENT_FACE_2);
      agx_ppp_push_packed(&ppp, &face_2, FRAGMENT_FACE_2);

      d12_ppp_push_stencil(&ppp, &ds->front, ds, s_test, s_read_only);

      /* Back face: same depth state, its own stencil ops. */
      agx_ppp_push_merged(&ppp, FRAGMENT_FACE, cfg, face) {
         cfg.stencil_reference = list->stencil_ref;
      }
      agx_ppp_push_packed(&ppp, &face_2, FRAGMENT_FACE_2);
      d12_ppp_push_stencil(&ppp, &ds->back, ds, s_test, s_read_only);

      /* Output select is the MERGE of the vertex side (which says the
       * position is written) and the fragment side. Pushing only the
       * fragment half rasterizes nothing.
       */
      struct agx_output_select_packed osel = uvs->osel;
      agx_ppp_push_merged_blobs(&ppp, AGX_OUTPUT_SELECT_LENGTH, &osel,
                                &pso->fs.linked.osel);

      agx_ppp_push_packed(&ppp, &varys->counts_32, VARYING_COUNTS);
      agx_ppp_push_packed(&ppp, &varys->counts_16, VARYING_COUNTS);

      agx_ppp_push(&ppp, CULL, cfg) {
         cfg.front_face_ccw = pso->raster.front_face_ccw;
         /* Direct hardware TES needs the tessellator's declared winding folded
          * into culling, as in Honeykrisp. Once any geometry stage follows,
          * however, Poly presents an explicit list of fully ordered output
          * triangles; applying the tessellation XOR again reverses them. This
          * includes the XFB passthrough synthesized by this driver. D3D12 has
          * no selectable domain origin, so there is no lower-left XOR. */
         if (pso->has_tess && !pso->has_gs)
            cfg.front_face_ccw ^= pso->tess_ccw;
         cfg.cull_front = pso->raster.cull_front;
         cfg.cull_back = pso->raster.cull_back;
         cfg.depth_clip = pso->raster.depth_clip;
      }

      agx_ppp_push_packed(&ppp, &pso->fs.fs_counts, FRAGMENT_SHADER_WORD_0);

      agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_1, cfg) {
         cfg.pipeline = fs_usc;
      }

      agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_2, cfg) {
         cfg.cf_bindings = varyings;
      }

      agx_ppp_push(&ppp, FRAGMENT_SHADER_WORD_3, cfg);

      agx_ppp_push(&ppp, FRAGMENT_OCCLUSION_QUERY, cfg);

      agx_ppp_push(&ppp, OUTPUT_SIZE, cfg) {
         cfg.count = uvs->size;
      }

      agx_ppp_fini(&out, &ppp);
   }

   /* The draw. Tessellation and mesh already built an indexed indirect
    * one, so only the ordinary path constructs anything here.
    *
    * For an indexed draw `first_vertex` is D3D12's BaseVertexLocation --
    * added to every fetched index -- and is NOT the same thing as the
    * `start` of a non-indexed draw, which is a vertex offset. Passing it
    * as `start` would skip indices instead of biasing vertices.
   */
   if (!draw_set) {
      uint64_t index_buffer = list->ib.addr ? list->ib.addr
                                            : device->null_va;
      uint32_t index_range = list->ib.addr ? list->ib.size_B : 4;
      enum agx_index_size index_size = list->ib.addr
                                          ? list->ib.index_size
                                          : AGX_INDEX_SIZE_U16;
      if (indirect_args && indexed) {
         draw = agx_draw_indexed_indirect(
            indirect_args, index_buffer, index_range, index_size, restart);
      } else if (indirect_args) {
         draw = agx_draw_indirect(indirect_args);
      } else if (indexed) {
         draw = agx_draw_indexed(vertex_count, instance_count, first_index,
                                 first_vertex, first_instance, index_buffer,
                                 index_range, index_size, restart);
      } else {
         draw = (struct agx_draw){
            .b = agx_3d(vertex_count, instance_count, 1),
            .start = first_vertex,
            .start_instance = first_instance,
         };
      }
   }

   out = (uint8_t *)agx_vdm_draw((uint32_t *)out, device->dev.chip, draw,
                                 d12_agx_primitive(topology));

   list->stream_cur = out;
   seg->has_draws = true;
}

static void STDMETHODCALLTYPE
d12_list_DispatchMesh(ID3D12GraphicsCommandList6 *iface, UINT x, UINT y,
                      UINT z)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!x || !y || !z)
      return;

   /* A mesh draw has no vertices; the thread group counts ride in the
    * same three arguments the vertex path uses, and d12_list_draw routes
    * on pso->has_mesh.
    */
   if (!list->pso || !list->pso->has_mesh) {
      mesa_loge("DispatchMesh without a mesh pipeline state");
      list->state = D12_LIST_ERROR;
      return;
   }

   d12_list_draw(list, x, y, z, 0, false, 0, 0);
}

static void STDMETHODCALLTYPE
d12_list_DrawInstanced(ID3D12GraphicsCommandList6 *iface,
                       UINT vertex_count_per_instance, UINT instance_count,
                       UINT start_vertex, UINT start_instance)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!vertex_count_per_instance || !instance_count)
      return;

   d12_list_draw(list, vertex_count_per_instance, instance_count,
                 start_vertex, start_instance, false, 0, 0);
}

static void STDMETHODCALLTYPE
d12_list_DrawIndexedInstanced(ID3D12GraphicsCommandList6 *iface,
                              UINT index_count_per_instance,
                              UINT instance_count, UINT start_index,
                              INT base_vertex, UINT start_instance)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!index_count_per_instance || !instance_count)
      return;

   /* Match the non-indexed path: without a PSO this draw is ignored before
    * any indexed-draw state is validated.
    */
   if (list->state != D12_LIST_RECORDING || !list->pso)
      return;

   d12_list_draw(list, index_count_per_instance, instance_count,
                 (unsigned)base_vertex, start_instance, true, start_index, 0);
}

static uint64_t
d12_list_set_root_constants(struct d12_cmd_list *list, bool compute,
                            UINT root_param_index, UINT num_values,
                            const void *data, UINT offset_in_values);

static void STDMETHODCALLTYPE
d12_list_ExecuteIndirect(ID3D12GraphicsCommandList6 *iface,
                         ID3D12CommandSignature *signature_iface,
                         UINT max_command_count,
                         ID3D12Resource *argument_buffer_iface,
                         UINT64 argument_buffer_offset,
                         ID3D12Resource *count_buffer_iface,
                         UINT64 count_buffer_offset)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING || !max_command_count)
      return;

   if (!signature_iface || !argument_buffer_iface) {
      mesa_loge("ExecuteIndirect: null signature or argument buffer");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_command_signature *signature =
      (struct d12_command_signature *)signature_iface;
   struct d12_resource *argument_buffer =
      d12_resource_from_iface(argument_buffer_iface);
   struct d12_resource *count_buffer = count_buffer_iface
      ? d12_resource_from_iface(count_buffer_iface) : NULL;

   if (signature->device != list->device ||
       argument_buffer->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
      mesa_loge("ExecuteIndirect: incompatible signature or argument buffer");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (count_buffer &&
       (count_buffer->device != list->device ||
        count_buffer->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        count_buffer_offset > count_buffer->desc.Width ||
        sizeof(uint32_t) > count_buffer->desc.Width - count_buffer_offset ||
        (count_buffer->heap_props.Type != D3D12_HEAP_TYPE_UPLOAD &&
         count_buffer->heap_props.Type != D3D12_HEAP_TYPE_GPU_UPLOAD) ||
        (argument_buffer->heap_props.Type != D3D12_HEAP_TYPE_UPLOAD &&
         argument_buffer->heap_props.Type != D3D12_HEAP_TYPE_GPU_UPLOAD))) {
      mesa_loge("ExecuteIndirect: count buffers currently require bounded "
                "CPU-visible argument and count storage");
      list->state = D12_LIST_ERROR;
      return;
   }

   UINT terminal_index = signature->desc.NumArgumentDescs - 1;
   D3D12_INDIRECT_ARGUMENT_TYPE type =
      signature->arguments[terminal_index].Type;
   const bool dispatch = type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
   const bool indexed = type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
   const size_t terminal_size =
      dispatch ? sizeof(D3D12_DISPATCH_ARGUMENTS)
      : indexed ? sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)
                : sizeof(D3D12_DRAW_ARGUMENTS);
   if (type != D3D12_INDIRECT_ARGUMENT_TYPE_DRAW && !indexed && !dispatch) {
      mesa_loge("ExecuteIndirect: argument type %u unsupported", type);
      list->state = D12_LIST_ERROR;
      return;
   }

   if (!list->pso ||
       (dispatch && !list->pso->is_compute) ||
       (!dispatch && (list->pso->is_compute || list->pso->has_gs ||
                      list->pso->has_tess || list->pso->has_mesh))) {
      mesa_loge("ExecuteIndirect: only ordinary graphics pipelines are "
                "supported");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (indexed && !list->ib.addr) {
      mesa_loge("ExecuteIndirect: indexed draw has no index buffer");
      list->state = D12_LIST_ERROR;
      return;
   }

   size_t terminal_offset = 0;
   for (UINT i = 0; i < terminal_index; i++) {
      const D3D12_INDIRECT_ARGUMENT_DESC *arg = &signature->arguments[i];
      if (arg->Type != D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT ||
          !arg->Constant.Num32BitValuesToSet) {
         mesa_loge("ExecuteIndirect: argument type %u before the terminal "
                   "command is unsupported", arg->Type);
         list->state = D12_LIST_ERROR;
         return;
      }
      terminal_offset += (size_t)arg->Constant.Num32BitValuesToSet * 4;
   }

   const size_t argument_size = terminal_offset + terminal_size;
   const uint64_t stride = signature->desc.ByteStride;
   const uint64_t width = argument_buffer->desc.Width;
   if (stride < argument_size || (stride & 3) ||
       argument_buffer_offset > width ||
       (uint64_t)(max_command_count - 1) >
          (width - argument_buffer_offset) / stride ||
       argument_size > width - argument_buffer_offset -
                          (uint64_t)(max_command_count - 1) * stride) {
      mesa_loge("ExecuteIndirect: argument range is out of bounds");
      list->state = D12_LIST_ERROR;
      return;
   }

   for (UINT i = 0; i < max_command_count; i++) {
      uint64_t command_offset = argument_buffer_offset + (uint64_t)i * stride;
      size_t running_offset = 0;

      for (UINT j = 0; j < terminal_index; j++) {
         const D3D12_INDIRECT_ARGUMENT_DESC *arg = &signature->arguments[j];
         uint32_t size_B = arg->Constant.Num32BitValuesToSet * 4;
         uint64_t source_offset = command_offset + running_offset;
         const uint8_t *source = agx_bo_map(argument_buffer->bo) +
                                 argument_buffer->offset_B + source_offset;
         uint64_t destination = d12_list_set_root_constants(
            list, dispatch, arg->Constant.RootParameterIndex,
            arg->Constant.Num32BitValuesToSet, source,
            arg->Constant.DestOffsetIn32BitValues);
         if (!destination || list->state != D12_LIST_RECORDING)
            return;

         struct d12_indirect_constant_fixup fixup = {
            .source = argument_buffer,
            .source_offset_B = source_offset,
            .destination_gpu = destination +
               (uint64_t)arg->Constant.DestOffsetIn32BitValues * 4,
            .size_B = size_B,
         };
         util_dynarray_append_typed(&list->indirect_constant_fixups,
                                    struct d12_indirect_constant_fixup,
                                    fixup);
         running_offset += size_B;
      }

      uint64_t args = argument_buffer->addr + command_offset + terminal_offset;
      if (count_buffer) {
         uint64_t destination =
            d12_list_upload_zero(list, terminal_size, 4);
         if (!destination)
            return;

         struct d12_indirect_argument_fixup fixup = {
            .source = argument_buffer,
            .count_source = count_buffer,
            .source_offset_B = command_offset + terminal_offset,
            .count_offset_B = count_buffer_offset,
            .destination_gpu = destination,
            .size_B = terminal_size,
            .command_index = i,
         };
         util_dynarray_append_typed(&list->indirect_argument_fixups,
                                    struct d12_indirect_argument_fixup,
                                    fixup);
         args = destination;
      }
      if (dispatch)
         d12_list_dispatch_pso(list, 0, 0, 0, args);
      else
         d12_list_draw(list, 0, 0, 0, 0, indexed, 0, args);
      if (list->state != D12_LIST_RECORDING)
         return;
   }

   (void)count_buffer_offset;
}

/*
 * Record the bound vertex buffers. The view carries the STRIDE, which
 * D3D12 makes draw state rather than pipeline state -- that is why the
 * PSO compiles its fetch with dynamic strides and this is published per
 * draw rather than baked.
 */
static void STDMETHODCALLTYPE
d12_list_IASetVertexBuffers(ID3D12GraphicsCommandList6 *iface, UINT start_slot,
                            UINT num_views,
                            const D3D12_VERTEX_BUFFER_VIEW *views)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   /* Unlike IASetIndexBuffer(NULL), a NULL view-array pointer does not
    * unbind anything.  The call is ignored; zero BufferLocation entries in
    * a real array are how individual vertex-buffer slots are unbound. */
   if (!views)
      return;

   for (UINT i = 0; i < num_views; i++) {
      UINT slot = start_slot + i;
      if (slot >= D12_MAX_VB_SLOTS) {
         mesa_logw("IASetVertexBuffers: slot %u beyond %u", slot,
                   D12_MAX_VB_SLOTS - 1);
         continue;
      }

      list->vb[slot].addr = views[i].BufferLocation;
      list->vb[slot].size_B = views[i].SizeInBytes;
      list->vb[slot].stride_B = views[i].StrideInBytes;
   }
}

/*
 * Record the bound index buffer. DXGI gives the index WIDTH as a format;
 * AGX wants it as an enum, and there are only two legal values -- D3D12
 * has no 8-bit index buffers.
 */
static void STDMETHODCALLTYPE
d12_list_IASetIndexBuffer(ID3D12GraphicsCommandList6 *iface,
                          const D3D12_INDEX_BUFFER_VIEW *view)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!view) {
      list->ib.addr = 0;
      list->ib.size_B = 0;
      list->ib.index_size = AGX_INDEX_SIZE_U16;
      return;
   }

   switch (view->Format) {
   case DXGI_FORMAT_R16_UINT:
      list->ib.index_size = AGX_INDEX_SIZE_U16;
      break;
   case DXGI_FORMAT_R32_UINT:
      list->ib.index_size = AGX_INDEX_SIZE_U32;
      break;
   case DXGI_FORMAT_UNKNOWN:
      /* Native drivers commonly accept UNKNOWN as the historical 16-bit
       * default. vkd3d's robustness coverage relies on that behaviour.
       */
      list->ib.index_size = AGX_INDEX_SIZE_U16;
      break;
   default:
      mesa_loge("IASetIndexBuffer: format %u is not a valid index format",
                (unsigned)view->Format);
      list->state = D12_LIST_ERROR;
      return;
   }

   list->ib.addr = view->BufferLocation;
   list->ib.size_B = view->SizeInBytes;
}

static void STDMETHODCALLTYPE
d12_list_IASetPrimitiveTopology(ID3D12GraphicsCommandList6 *iface,
                                D3D12_PRIMITIVE_TOPOLOGY topology)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   /* Geometry shaders take points and lines as input, so the topology
    * can no longer be assumed. Anything not listed here would be drawn
    * as the wrong primitive, so it is refused rather than approximated.
    */
   switch (topology) {
   case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
      list->topology = MESA_PRIM_POINTS;
      list->patch_control_points = 0;
      break;
   case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
      list->topology = MESA_PRIM_LINES;
      break;
   case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
      list->topology = MESA_PRIM_LINE_STRIP;
      break;
   case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
      list->topology = MESA_PRIM_TRIANGLES;
      break;
   case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
      list->topology = MESA_PRIM_TRIANGLE_STRIP;
      break;
   default:
      /* Patch lists encode their control-point count in the topology
       * value itself. That count is the only thing API state contributes
       * to tessellation, so it is recorded rather than refused.
       */
      if (topology >= D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST &&
          topology <= D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST) {
         list->patch_control_points =
            1 + (topology - D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST);
         list->topology = MESA_PRIM_PATCHES;
         break;
      }
      mesa_loge("IASetPrimitiveTopology: topology %d is not supported",
                (int)topology);
      list->state = D12_LIST_ERROR;
      break;
   }
}


static void STDMETHODCALLTYPE
d12_list_RSSetViewports(ID3D12GraphicsCommandList6 *iface, UINT count,
                        const D3D12_VIEWPORT *viewports)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (count >= 1 && viewports) {
      list->viewport = viewports[0];
      list->has_viewport = true;
   }
}

static void STDMETHODCALLTYPE
d12_list_RSSetScissorRects(ID3D12GraphicsCommandList6 *iface, UINT count,
                           const D3D12_RECT *rects)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (count > 1)
      mesa_logw("RSSetScissorRects: only the first rect is applied");

   if (count >= 1 && rects) {
      list->scissor = rects[0];
      list->has_scissor = true;
   } else {
      list->has_scissor = false;
   }
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootSignature(ID3D12GraphicsCommandList6 *iface,
                                  ID3D12RootSignature *root_sig)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   list->graphics_rs = (struct d12_root_signature *)root_sig;
}

/*
 * Bind one root descriptor: a raw GPU address for a single shader
 * register, named indirectly through a root PARAMETER index.
 *
 * The root signature is what turns one into the other. Assuming the
 * parameter index IS the register happens to hold when an application
 * declares its parameters in register order and uses one class, and
 * silently binds the wrong resource the moment it does not.
 */
static void
d12_list_set_root_descriptor(struct d12_cmd_list *list, bool compute,
                             UINT root_param_index,
                             D3D12_GPU_VIRTUAL_ADDRESS address,
                             D3D12_ROOT_PARAMETER_TYPE expected)
{
   struct d12_root_signature *rs =
      compute ? list->compute_rs : list->graphics_rs;

   if (!rs) {
      mesa_loge("root descriptor bound with no root signature set");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (root_param_index >= rs->layout.num_params) {
      mesa_loge("root parameter %u is past the signature's %u parameters",
                root_param_index, rs->layout.num_params);
      list->state = D12_LIST_ERROR;
      return;
   }

   const struct d12_root_param *rp = &rs->layout.params[root_param_index];
   if (rp->type != D3D12_ROOT_PARAMETER_TYPE_CBV &&
       rp->type != D3D12_ROOT_PARAMETER_TYPE_SRV &&
       rp->type != D3D12_ROOT_PARAMETER_TYPE_UAV) {
      mesa_loge("root parameter %u is not an address descriptor (type %u)",
                root_param_index, rp->type);
      list->state = D12_LIST_ERROR;
      return;
   }

   /* The three Set*Root*View entry points all write the same 64-bit root
    * descriptor slot. The signature, not the chosen setter spelling,
    * determines how the shader interprets that address. */
   (void)expected;

   unsigned root_range =
      d12_root_param_range_base(&rs->layout, root_param_index);
   unsigned slot = d12_binding_map_find_root_range(
      &rs->map, root_range, rp->base_register);
   if (slot == ~0u) {
      mesa_loge("root parameter %u names register %u in space %u, which the "
                "signature's binding map does not cover",
                root_param_index, rp->base_register, rp->space);
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_descriptor *table =
      compute ? list->compute_root_table : list->graphics_root_table;
   const struct d12_descriptor **sources =
      compute ? list->compute_root_source : list->graphics_root_source;

   /* A root descriptor is a bare address: no resource behind it, so no
    * hardware texture descriptor can be built from one.
    */
   memset(&table[slot], 0, sizeof(table[slot]));
   sources[slot] = NULL;
   table[slot].address = address;
   table[slot].kind = rp->type == D3D12_ROOT_PARAMETER_TYPE_SRV
                         ? D12_DESC_SRV
                         : (rp->type == D3D12_ROOT_PARAMETER_TYPE_UAV
                               ? D12_DESC_UAV
                               : D12_DESC_CBV);
}

/*
 * Bind a descriptor table: a contiguous run of descriptors in a
 * shader-visible heap, covering one or more register ranges.
 *
 * Our shader ABI reaches resources through a flat table of addresses, so
 * binding a table means copying each descriptor's address into the slot
 * for the register that range covers. Descriptor indexing in the shader is
 * therefore not supported yet -- the flattening happens here, at bind
 * time, rather than in the shader.
 */
static void
d12_list_set_root_table(struct d12_cmd_list *list, bool compute,
                        UINT root_param_index,
                        D3D12_GPU_DESCRIPTOR_HANDLE base)
{
   struct d12_root_signature *rs =
      compute ? list->compute_rs : list->graphics_rs;

   if (!rs) {
      mesa_loge("descriptor table bound with no root signature set");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (root_param_index >= rs->layout.num_params) {
      mesa_loge("root parameter %u is past the signature's %u parameters",
                root_param_index, rs->layout.num_params);
      list->state = D12_LIST_ERROR;
      return;
   }

   const struct d12_root_param *rp = &rs->layout.params[root_param_index];
   if (rp->type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
      mesa_loge("root parameter %u is not a descriptor table",
                root_param_index);
      list->state = D12_LIST_ERROR;
      return;
   }

   /* GPU handles are opaque CPU pointers into a heap's slot array.  Finite
    * ranges are snapshotted below.  An unbounded range instead resolves the
    * pointer through SetDescriptorHeaps to the coherent GPU BO and publishes
    * that descriptor-array address in one root slot. */
   const struct d12_descriptor *slots = (const struct d12_descriptor *)base.ptr;
   if (!slots) {
      mesa_loge("descriptor table bound with a null GPU handle");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_descriptor *resource_table =
      compute ? list->compute_root_table : list->graphics_root_table;
   const struct d12_descriptor **resource_sources =
      compute ? list->compute_root_source : list->graphics_root_source;
   struct d12_descriptor *sampler_table =
      compute ? list->compute_sampler_table : list->graphics_sampler_table;

   unsigned cursor = 0;
   unsigned root_range_base =
      d12_root_param_range_base(&rs->layout, root_param_index);
   for (unsigned r = 0; r < rp->num_ranges; r++) {
      const struct d12_root_range *range = &rp->ranges[r];
      unsigned root_range = root_range_base + r;

      unsigned offset = range->offset_in_descriptors;
      if (offset == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
         offset = cursor;

      unsigned klass = d12_descriptor_range_class(range->type);
      if (klass >= D12_RES_CLASS_COUNT) {
         mesa_loge("descriptor range has unknown type %u", range->type);
         list->state = D12_LIST_ERROR;
         return;
      }

      if (range->num_descriptors == UINT32_MAX ||
          d12_binding_map_root_range_unbounded(
             &rs->map, root_range, range->base_register)) {
         if (klass == D12_RES_SAMPLER) {
            struct d12_descriptor_heap *heap =
               list->sampler_descriptor_heap;
            if (!heap || !heap->sampler_bo ||
                heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
               mesa_loge("heap-indexed sampler table has no shader-visible "
                         "sampler heap bound");
               list->state = D12_LIST_ERROR;
               return;
            }

            uintptr_t heap_start = (uintptr_t)heap->slots;
            size_t heap_bytes = (size_t)heap->desc.NumDescriptors *
                                sizeof(struct d12_descriptor);
            uintptr_t handle = (uintptr_t)slots;
            if (handle < heap_start || handle - heap_start >= heap_bytes ||
                (handle - heap_start) % sizeof(struct d12_descriptor)) {
               mesa_loge("heap-indexed sampler table handle is outside the "
                         "bound sampler heap");
               list->state = D12_LIST_ERROR;
               return;
            }

            size_t heap_index =
               (handle - heap_start) / sizeof(struct d12_descriptor);
            if ((size_t)offset >= heap->desc.NumDescriptors - heap_index) {
               mesa_loge("unbounded sampler range offset %u is outside a "
                         "%u-slot heap at index %zu", offset,
                         heap->desc.NumDescriptors, heap_index);
               list->state = D12_LIST_ERROR;
               return;
            }

            unsigned slot = d12_binding_map_find_root_range(
               &rs->map, root_range, range->base_register);
            if (slot == ~0u ||
                !d12_binding_map_root_range_unbounded(
                   &rs->map, root_range, range->base_register)) {
               mesa_loge("heap-indexed sampler range has no binding-map "
                         "base");
               list->state = D12_LIST_ERROR;
               return;
            }

            size_t first = heap_index + offset;
            sampler_table[slot] = (struct d12_descriptor){
               /* This plane is an index, not an address. Index zero is a
                * valid first sampler and must remain distinguishable from
                * an absent plane through the root-signature map. */
               .address = first,
            };

            if (getenv("D3D12AGX_VALIDATE_BINDINGS")) {
               size_t remaining = heap->desc.NumDescriptors - first;
               size_t scan = MIN2(remaining, 1024);
               unsigned populated = 0;
               for (size_t i = 0; i < scan; i++)
                  populated += heap->slots[first + i].sampler_kind ==
                               D12_DESC_SAMPLER;

               mesa_logi("binding validation: unbounded sampler range "
                         "space=%u reg=%u root-slot=%u heap-index=%zu "
                         "remaining=%zu scan=%zu populated=%u valid=%u",
                         range->space, range->base_register, slot, first,
                         remaining, scan, populated, populated == scan);
               if (populated != scan) {
                  list->state = D12_LIST_ERROR;
                  return;
               }
            }
            continue;
         }

         struct d12_descriptor_heap *heap =
            list->resource_descriptor_heap;
         if (!heap || !heap->bo ||
             heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            mesa_loge("heap-indexed descriptor table has no shader-visible "
                      "resource heap bound");
            list->state = D12_LIST_ERROR;
            return;
         }

         uintptr_t heap_start = (uintptr_t)heap->slots;
         size_t heap_bytes = (size_t)heap->desc.NumDescriptors *
                             sizeof(struct d12_descriptor);
         uintptr_t handle = (uintptr_t)slots;
         if (handle < heap_start || handle - heap_start >= heap_bytes ||
             (handle - heap_start) % sizeof(struct d12_descriptor)) {
            mesa_loge("heap-indexed descriptor table handle is outside the "
                      "bound resource heap");
            list->state = D12_LIST_ERROR;
            return;
         }

         size_t heap_index =
            (handle - heap_start) / sizeof(struct d12_descriptor);
         if ((size_t)offset >= heap->desc.NumDescriptors - heap_index) {
            mesa_loge("unbounded descriptor range offset %u is outside a "
                      "%u-slot heap at index %zu", offset,
                      heap->desc.NumDescriptors, heap_index);
            list->state = D12_LIST_ERROR;
            return;
         }

         unsigned slot = d12_binding_map_find_root_range(
            &rs->map, root_range, range->base_register);
         if (slot == ~0u ||
             !d12_binding_map_root_range_unbounded(
                &rs->map, root_range, range->base_register)) {
            mesa_loge("heap-indexed descriptor range has no binding-map base");
            list->state = D12_LIST_ERROR;
            return;
         }

         resource_table[slot] = (struct d12_descriptor){
            .address = heap->bo->va->addr +
                       (heap_index + offset) *
                          sizeof(struct d12_descriptor),
         };
         resource_sources[slot] = NULL;

         if (getenv("D3D12AGX_VALIDATE_BINDINGS")) {
            size_t first = heap_index + offset;
            size_t remaining =
               (size_t)heap->desc.NumDescriptors - first;
            size_t scan = MIN2(remaining, 256);
            unsigned populated = 0, counter_ptrs = 0, invalid = 0;

            for (size_t i = 0; i < scan; i++) {
               const struct d12_descriptor *d = &heap->slots[first + i];
               populated += d->kind != D12_DESC_NONE;
               counter_ptrs += d->counter_address != 0;

               if (d->counter_address && (d->counter_address & 3))
                  invalid++;

               if (d->resource && d->resource->bo) {
                  uint64_t bo_base = d->resource->bo->va->addr;
                  if (!d->address || d->address < bo_base ||
                      d->address - bo_base >= d->resource->bo->size)
                     invalid++;
               }
            }

            mesa_logi("binding validation: unbounded range class=%u "
                      "space=%u reg=%u root-slot=%u heap-index=%zu "
                      "gpu=0x%llx remaining=%zu scan=%zu populated=%u "
                      "counters=%u invalid=%u valid=%u", klass,
                      range->space, range->base_register, slot,
                      first,
                      (unsigned long long)resource_table[slot].address,
                      remaining, scan, populated, counter_ptrs, invalid,
                      invalid == 0);
            if (invalid) {
               list->state = D12_LIST_ERROR;
               return;
            }
         }
         continue;
      }

      for (unsigned i = 0; i < range->num_descriptors; i++) {
         unsigned slot = d12_binding_map_find_root_range(
            &rs->map, root_range, range->base_register + i);
         if (slot == ~0u) {
            mesa_loge("descriptor range reaches a register the signature's "
                      "binding map does not cover");
            list->state = D12_LIST_ERROR;
            return;
         }

         struct d12_descriptor *table =
            klass == D12_RES_SAMPLER ? sampler_table : resource_table;
         table[slot] = slots[offset + i];
         if (klass != D12_RES_SAMPLER)
            resource_sources[slot] = &slots[offset + i];
      }

      cursor = offset + range->num_descriptors;
   }
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList6 *iface,
                                        UINT root_param_index,
                                        D3D12_GPU_DESCRIPTOR_HANDLE base)
{
   d12_list_set_root_table(d12_cmd_list_from_iface(iface), false,
                           root_param_index, base);
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList6 *iface,
                                       UINT root_param_index,
                                       D3D12_GPU_DESCRIPTOR_HANDLE base)
{
   d12_list_set_root_table(d12_cmd_list_from_iface(iface), true,
                           root_param_index, base);
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList6 *iface,
                                           UINT index,
                                           D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), false, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_SRV);
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootShaderResourceView(ID3D12GraphicsCommandList6 *iface,
                                          UINT index,
                                          D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), true, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_SRV);
}

/*
 * ROOT CONSTANTS. The application hands over N 32-bit values inline; the
 * shader sees them as a constant buffer at the parameter's register. Our
 * shader ABI reaches a CBV through an address, so the values are uploaded
 * into the command list's data area and that address goes in the slot --
 * the same slot a root CBV would use, which is why the binding map
 * already assigns one (d12_root_layout_build_map).
 *
 * Uploading per call rather than accumulating means a later partial write
 * has to preserve the earlier values, so the current contents are copied
 * forward before the new ones land on top.
 */
static uint64_t
d12_list_set_root_constants(struct d12_cmd_list *list, bool compute,
                            UINT root_param_index, UINT num_values,
                            const void *data, UINT offset_in_values)
{
   struct d12_root_signature *rs =
      compute ? list->compute_rs : list->graphics_rs;

   if (!rs) {
      mesa_loge("root constants bound with no root signature set");
      list->state = D12_LIST_ERROR;
      return 0;
   }

   if (root_param_index >= rs->layout.num_params) {
      mesa_loge("root parameter %u is past the signature's %u parameters",
                root_param_index, rs->layout.num_params);
      list->state = D12_LIST_ERROR;
      return 0;
   }

   const struct d12_root_param *rp = &rs->layout.params[root_param_index];
   if (rp->type != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
      mesa_loge("root parameter %u is type %u, bound as root constants",
                root_param_index, rp->type);
      list->state = D12_LIST_ERROR;
      return 0;
   }

   unsigned total = MAX2(rp->num_constants, offset_in_values + num_values);
   if (!total || !data)
      return 0;

   unsigned root_range =
      d12_root_param_range_base(&rs->layout, root_param_index);
   unsigned slot = d12_binding_map_find_root_range(
      &rs->map, root_range, rp->base_register);
   if (slot == ~0u) {
      mesa_loge("root constants at register %u space %u are not in the "
                "signature's binding map",
                rp->base_register, rp->space);
      list->state = D12_LIST_ERROR;
      return 0;
   }

   struct d12_descriptor *table =
      compute ? list->compute_root_table : list->graphics_root_table;
   const struct d12_descriptor **sources =
      compute ? list->compute_root_source : list->graphics_root_source;

   /* A constant buffer is read in 16-byte units, so round up rather than
    * leave the tail of the last vector reading past the allocation. */
   size_t size = ALIGN_POT((size_t)total * 4, 16);

   uint32_t *staging = calloc(1, size);
   if (!staging)
      return 0;

   /* Preserve what a previous call to this same parameter wrote. */
   if (table[slot].kind == D12_DESC_CBV && table[slot].address) {
      const void *old = d12_list_data_cpu(list, table[slot].address);
      if (old)
         memcpy(staging, old, MIN2(size, (size_t)rp->num_constants * 4));
   }

   memcpy(staging + offset_in_values, data, (size_t)num_values * 4);

   uint64_t gpu = d12_list_upload(list, staging, size, 16);
   free(staging);

   if (!gpu) {
      list->state = D12_LIST_ERROR;
      return 0;
   }

   memset(&table[slot], 0, sizeof(table[slot]));
   sources[slot] = NULL;
   table[slot].address = gpu;
   table[slot].kind = D12_DESC_CBV;
   return gpu;
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList6 *iface,
                                       UINT index, UINT num, const void *data,
                                       UINT offset)
{
   d12_list_set_root_constants(d12_cmd_list_from_iface(iface), false, index,
                               num, data, offset);
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList6 *iface,
                                      UINT index, UINT num, const void *data,
                                      UINT offset)
{
   d12_list_set_root_constants(d12_cmd_list_from_iface(iface), true, index,
                               num, data, offset);
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList6 *iface,
                                      UINT index, UINT value, UINT offset)
{
   d12_list_set_root_constants(d12_cmd_list_from_iface(iface), false, index, 1,
                               &value, offset);
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList6 *iface,
                                     UINT index, UINT value, UINT offset)
{
   d12_list_set_root_constants(d12_cmd_list_from_iface(iface), true, index, 1,
                               &value, offset);
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList6 *iface,
                                           UINT index,
                                           D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), false, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_CBV);
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootConstantBufferView(ID3D12GraphicsCommandList6 *iface,
                                          UINT index,
                                          D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), true, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_CBV);
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList6 *iface,
                                            UINT index,
                                            D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), false, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_UAV);
}

static void STDMETHODCALLTYPE
d12_list_SetDescriptorHeaps(ID3D12GraphicsCommandList6 *iface, UINT count,
                            ID3D12DescriptorHeap *const *heaps)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (count > 2 || (count && !heaps)) {
      mesa_loge("SetDescriptorHeaps: invalid heap array");
      list->state = D12_LIST_ERROR;
      return;
   }

   for (UINT i = 0; i < count; i++) {
      if (!heaps[i]) {
         mesa_loge("SetDescriptorHeaps: null heap");
         list->state = D12_LIST_ERROR;
         return;
      }

      struct d12_descriptor_heap *heap =
         (struct d12_descriptor_heap *)heaps[i];
      if (!(heap->desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ||
          (heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
           heap->desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)) {
         mesa_loge("SetDescriptorHeaps: heap %u is not shader-visible", i);
         list->state = D12_LIST_ERROR;
         return;
      }

      if (heap->desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
         list->resource_descriptor_heap = heap;
      else {
         /* The DRM command, not an individual USC program, owns the
          * bindless sampler heap. End any existing command before changing
          * it so earlier dispatches/draws retain their recorded heap. */
         if (list->sampler_descriptor_heap != heap &&
             (list->cdm_open || list->render_open))
            d12_list_close_cdm(list);
         list->sampler_descriptor_heap = heap;
      }
   }
}

static void STDMETHODCALLTYPE
d12_list_SetPipelineState(ID3D12GraphicsCommandList6 *iface,
                          ID3D12PipelineState *pso)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   list->pso = pso ? d12_pipeline_state_from_iface(pso) : NULL;
}

/* Clear API state without rewinding the command stream. Commands recorded
 * before ClearState remain part of the list; only the state inherited by
 * later commands returns to defaults, with the optional PSO installed.
 */
static void STDMETHODCALLTYPE
d12_list_ClearState(ID3D12GraphicsCommandList6 *iface,
                    ID3D12PipelineState *initial_state)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   /* Render-target bindings are part of the cleared state. Finish an open
    * hardware render segment so a later draw cannot inherit its target.
    */
   if (list->render_open)
      d12_list_close_cdm(list);

   list->pso = initial_state ? d12_pipeline_state_from_iface(initial_state)
                             : NULL;
   list->compute_rs = NULL;
   list->graphics_rs = NULL;
   memset(list->compute_root_table, 0, sizeof(list->compute_root_table));
   memset(list->graphics_root_table, 0, sizeof(list->graphics_root_table));
   memset(list->compute_root_source, 0, sizeof(list->compute_root_source));
   memset(list->graphics_root_source, 0,
          sizeof(list->graphics_root_source));
   memset(list->compute_sampler_table, 0,
          sizeof(list->compute_sampler_table));
   memset(list->graphics_sampler_table, 0,
          sizeof(list->graphics_sampler_table));

   memset(list->vb, 0, sizeof(list->vb));
   memset(&list->ib, 0, sizeof(list->ib));
   list->topology = MESA_PRIM_TRIANGLES;
   list->patch_control_points = 0;

   memset(&list->viewport, 0, sizeof(list->viewport));
   memset(&list->scissor, 0, sizeof(list->scissor));
   list->has_viewport = false;
   list->has_scissor = false;
   memset(list->bound_rtv, 0, sizeof(list->bound_rtv));
   memset(&list->bound_dsv, 0, sizeof(list->bound_dsv));
   list->bound_rtv_count = 0;
   list->has_bound_dsv = false;
   list->stencil_ref = 0;
   for (unsigned i = 0; i < 4; i++)
      list->blend_factor[i] = 1.0f;
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootSignature(ID3D12GraphicsCommandList6 *iface,
                                 ID3D12RootSignature *root_sig)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   list->compute_rs = (struct d12_root_signature *)root_sig;
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList6 *iface,
                                           UINT root_param_index,
                                           D3D12_GPU_VIRTUAL_ADDRESS address)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   /* A root UAV is a raw GPU virtual address, which is exactly what the
    * shader's binding ABI wants. Which shader register it feeds comes from
    * the root signature, not from the parameter index.
    */
   d12_list_set_root_descriptor(list, true, root_param_index, address,
                                D3D12_ROOT_PARAMETER_TYPE_UAV);
}

static void STDMETHODCALLTYPE
d12_list_Dispatch(ID3D12GraphicsCommandList6 *iface, UINT x, UINT y, UINT z)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!x || !y || !z)
      return;

   d12_list_dispatch_pso(list, x, y, z, 0);
}

static HRESULT STDMETHODCALLTYPE
d12_list_QueryInterface(ID3D12GraphicsCommandList6 *iface, REFIID riid,
                        void **out)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12CommandList) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList1) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList2) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList3) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList4) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList5) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList6)) {
      /* Same widening as the device: DispatchMesh is on list 6. */
      p_atomic_inc(&list->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12GraphicsCommandList::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_list_AddRef(ID3D12GraphicsCommandList6 *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return p_atomic_inc_return(&list->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_list_Release(ID3D12GraphicsCommandList6 *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&list->refcount);

   if (refs == 0) {
      /* A caller may release a list without closing it (or QueryInterface may
       * fail while the freshly-created list is still recording). Keep the
       * allocator occupancy invariant balanced on that path too. Close has
       * already transitioned successful/error recordings to CLOSED and
       * decremented the same counter.
       */
      if ((list->state == D12_LIST_RECORDING ||
           list->state == D12_LIST_ERROR) &&
          list->allocator && list->allocator->recording_lists)
         list->allocator->recording_lists--;

      /*
       * Same hazard as Reset: a list released while its last recording is
       * still executing would free the control stream and USC words out
       * from under the GPU. Park them on the allocator that recorded them,
       * which frees them once D3D12 guarantees completion.
       */
      if (list->submitted && list->allocator) {
         d12_list_release_segment_scratch(list, list->allocator);
         util_dynarray_append_typed(&list->allocator->retired_bos,
                                    struct agx_bo *, list->stream_bo);
         util_dynarray_append_typed(&list->allocator->retired_bos,
                                    struct agx_bo *, list->usc_bo);
         list->stream_bo = NULL;
         list->usc_bo = NULL;
      }

      d12_list_release_segment_scratch(list, NULL);

      if (list->stream_bo)
         agx_bo_unreference(&list->device->dev, list->stream_bo);
      if (list->usc_bo)
         agx_bo_unreference(&list->device->dev, list->usc_bo);
      d12_private_data_store_destroy(&list->private_data);
      util_dynarray_fini(&list->descriptor_fixups);
      util_dynarray_fini(&list->indirect_constant_fixups);
      util_dynarray_fini(&list->indirect_argument_fixups);
      d12_device_release(list->device);
      free(list);
   }

   return refs;
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_list, ID3D12GraphicsCommandList6,
                                d12_cmd_list_from_iface)

static HRESULT STDMETHODCALLTYPE
d12_list_GetDevice(ID3D12GraphicsCommandList6 *iface, REFIID riid, void **out)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return ID3D12Device_QueryInterface(&list->device->iface, riid, out);
}

static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE
d12_list_GetType(ID3D12GraphicsCommandList6 *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return list->type;
}

static HRESULT STDMETHODCALLTYPE
d12_list_Close(ID3D12GraphicsCommandList6 *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   /*
    * A list that hit an error during recording still CLOSES. D3D12
    * reports the recording error from Close and transitions the list to
    * closed regardless; it simply cannot be executed.
    *
    * Returning early without closing was a cascade, not a nicety. The
    * list stayed RECORDING, so the allocator's recording_lists never
    * dropped, so CommandAllocator::Reset failed for the rest of the
    * test -- and because these tests LOOP, every later iteration failed
    * too. It showed up as the two largest failure messages in the whole
    * vkd3d suite ("Failed to close command list", 43 tests; "Failed to
    * reset command allocator", 33) and none of them were about the thing
    * the test was actually checking.
    */
   if (list->state != D12_LIST_RECORDING &&
       list->state != D12_LIST_ERROR)
      return E_FAIL;

   bool errored = list->state == D12_LIST_ERROR;
   if (errored)
      list->had_error = true;

   /*
    * Free the tessellation heap from the GPU, at the end of this list's own
    * execution -- exactly hk_heap()'s queued write.
    *
    * The CPU zero in d12_list_reset_state is not enough and the comment
    * there was wrong about why. D3D12 lets a CLOSED list be handed to
    * ExecuteCommandLists as many times as the caller likes with no Reset in
    * between, and the vkd3d tests do precisely that in loops. Every one of
    * those executions bump-allocates from `bottom` and nothing ever puts it
    * back, so the heap walks off its end and the tessellator reads unmapped
    * memory -- the fault first-faulting-test.sh attributed to
    * test_quad_tessellation_dxil (line 301, unit UL1C, reason Unmapped).
    *
    * Growing the heap to 128 MiB did not move that number (+66 against
    * +69, inside the run-to-run spread), which is what a leak rather than
    * an under-size predicts: a bigger bucket takes longer to overflow but
    * still overflows.
    *
    * A compute dispatch here ends any open render pass and lands in a
    * segment that barriers on everything before it, so the reset is
    * ordered after the tessellation work that consumed the heap.
    */
   if (!errored && list->uses_heap && list->device->heap_desc_va) {
      struct libagx_write_u32_args args = {
         .address = list->device->heap_desc_va +
                    offsetof(struct poly_heap, bottom),
         .value = 0,
      };
      d12_list_dispatch_precomp(list, LIBAGX_WRITE_U32, &args, sizeof(args),
                                agx_1d(1));

      /* That dispatch can fail (segment limit, stream full) and signals it
       * by setting D12_LIST_ERROR. `errored` was sampled before it ran, so
       * without this re-read a list whose stream is now incomplete would
       * be closed as good and submitted -- the exact way this bench wedges.
       */
      if (list->state == D12_LIST_ERROR) {
         errored = true;
         list->had_error = true;
      }
   }

   if (!errored)
      d12_list_close_cdm(list);

   list->state = D12_LIST_CLOSED;

   if (list->allocator && list->allocator->recording_lists)
      list->allocator->recording_lists--;

   if (errored) {
      mesa_loge("Close on errored command list");
      return FAILED(list->record_error) ? list->record_error : E_FAIL;
   }

   return S_OK;
}

/*
 * Swap in freshly allocated recording storage, parking the submitted set on
 * the allocator that recorded it. Returns false only on allocation failure,
 * in which case the list keeps its existing storage untouched.
 */
static bool
d12_list_take_fresh_storage(struct d12_cmd_list *list)
{
   struct d12_device *device = list->device;

   struct agx_bo *stream =
      agx_bo_create(&device->dev, D12_STREAM_BO_SIZE, 0, AGX_BO_WRITEBACK,
                    "d3d12 cmdlist stream");
   struct agx_bo *usc =
      agx_bo_create(&device->dev, D12_USC_BO_SIZE, 0,
                    AGX_BO_LOW_VA | AGX_BO_WRITEBACK, "d3d12 cmdlist usc");

   if (!stream || !usc) {
      if (stream)
         agx_bo_unreference(&device->dev, stream);
      if (usc)
         agx_bo_unreference(&device->dev, usc);
      mesa_loge("command list Reset: out of memory for fresh storage");
      return false;
   }

   /*
    * The allocator that RECORDED the in-flight commands owns them, which is
    * not necessarily the allocator being passed to this Reset. Without an
    * allocator to park them on -- which should not happen for a submitted
    * list -- releasing immediately is still better than rewinding under the
    * GPU, and is what the old code effectively did.
    */
   if (list->allocator) {
      d12_list_release_segment_scratch(list, list->allocator);
      util_dynarray_append_typed(&list->allocator->retired_bos,
                                 struct agx_bo *, list->stream_bo);
      util_dynarray_append_typed(&list->allocator->retired_bos,
                                 struct agx_bo *, list->usc_bo);
   } else {
      d12_list_release_segment_scratch(list, NULL);
      agx_bo_unreference(&device->dev, list->stream_bo);
      agx_bo_unreference(&device->dev, list->usc_bo);
   }

   list->stream_bo = stream;
   list->usc_bo = usc;
   list->submitted = false;
   return true;
}

static HRESULT STDMETHODCALLTYPE
d12_list_Reset(ID3D12GraphicsCommandList6 *iface,
               ID3D12CommandAllocator *allocator,
               ID3D12PipelineState *initial_state)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!allocator)
      return E_INVALIDARG;

   struct d12_cmd_allocator *new_allocator =
      d12_cmd_allocator_from_iface(allocator);
   if (new_allocator->type != list->type)
      return E_INVALIDARG;

   /* Reset is only valid for a closed list, and an allocator can back at most
    * one actively recording list. Test these before touching list state or
    * parking any in-flight storage so a rejected call is side-effect free.
    */
   if (list->state != D12_LIST_CLOSED)
      return E_FAIL;
   if (new_allocator->recording_lists)
      return E_INVALIDARG;

   /* Reset starts a fresh recording, so the previous error is gone. */
   list->had_error = false;
   list->record_error = S_OK;
   if (initial_state)
      list->pso = d12_pipeline_state_from_iface(initial_state);

   /*
    * If this recording has already been submitted, the GPU may still be
    * reading its control stream, its USC words and its uploaded data --
    * all of which live in stream_bo/usc_bo. D3D12 explicitly allows Reset
    * immediately after ExecuteCommandLists, so we cannot rewind them:
    * ID3D12CommandAllocator::Reset is where completion is guaranteed, not
    * here. Park the old storage on the allocator that recorded it and take
    * fresh storage.
    *
    * Rewinding regardless is what corrupted test_copy_queue_buffer (the
    * readback was recorded over commands still executing) and what hung
    * the VDM in test_render_pass_suspend_resume_opts_disabled, where one
    * list was re-recorded 32 times and the hardware walked a control
    * stream being rewritten beneath it.
    */
   if (list->submitted && !d12_list_take_fresh_storage(list))
      return E_OUTOFMEMORY;

   /* Re-entering the recording state re-registers with the allocator. */
   if (list->state != D12_LIST_RECORDING) {
      list->allocator = new_allocator;
      list->allocator->recording_lists++;
   }

   d12_list_begin_recording(list);
   return S_OK;
}

static bool
d12_query_type_matches(const struct d12_query_heap *heap,
                       D3D12_QUERY_TYPE type)
{
   switch (heap->desc.Type) {
   case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
      return type == D3D12_QUERY_TYPE_OCCLUSION ||
             type == D3D12_QUERY_TYPE_BINARY_OCCLUSION;
   case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
      return type == D3D12_QUERY_TYPE_TIMESTAMP;
   case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
      return type == D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
   case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
      return type >= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0 &&
             type <= D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3;
   default:
      return false;
   }
}

static bool
d12_list_validate_query(struct d12_cmd_list *list,
                        struct d12_query_heap *heap,
                        D3D12_QUERY_TYPE type, UINT index)
{
   if (!heap || index >= heap->desc.Count ||
       !d12_query_type_matches(heap, type)) {
      mesa_loge("query type/index does not match its heap");
      list->state = D12_LIST_ERROR;
      return false;
   }

   return true;
}

static void
d12_list_copy_gpu_bytes(struct d12_cmd_list *list, uint64_t dst,
                        uint64_t src, size_t size_B)
{
   struct libagx_copy_uchar_args args = {
      .dest = dst,
      .src = src,
   };

   d12_list_dispatch_precomp(list, LIBAGX_COPY_UCHAR, &args, sizeof(args),
                             agx_1d(size_B));
}

static void STDMETHODCALLTYPE
d12_list_BeginQuery(ID3D12GraphicsCommandList6 *iface,
                    ID3D12QueryHeap *query_heap, D3D12_QUERY_TYPE type,
                    UINT index)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   struct d12_query_heap *heap = (struct d12_query_heap *)query_heap;

   if (list->state != D12_LIST_RECORDING)
      return;

   if (!d12_list_validate_query(list, heap, type, index))
      return;

   if (heap->desc.Type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS) {
      if (list->active_pipeline_stats_heap) {
         mesa_loge("BeginQuery: pipeline statistics query already active");
         list->state = D12_LIST_ERROR;
         return;
      }
      list->active_pipeline_stats_heap = heap;
      list->active_pipeline_stats_index = index;
      memset(&list->pipeline_stats, 0, sizeof(list->pipeline_stats));
      return;
   }

   if (heap->desc.Type == D3D12_QUERY_HEAP_TYPE_SO_STATISTICS) {
      unsigned stream = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
      if (stream >= ARRAY_SIZE(list->active_so_stats_heap) ||
          list->active_so_stats_heap[stream]) {
         mesa_loge("BeginQuery: stream-output statistics query already "
                   "active for stream %u", stream);
         list->state = D12_LIST_ERROR;
         return;
      }

      const D3D12_QUERY_DATA_SO_STATISTICS zero = {0};
      uint64_t source = d12_list_upload(list, &zero, sizeof(zero), 8);
      uint64_t result = heap->bo->va->addr + (uint64_t)index * heap->stride;
      if (!source)
         return;

      d12_list_copy_gpu_bytes(list, result, source, sizeof(zero));
      if (list->state != D12_LIST_RECORDING)
         return;

      list->active_so_stats_heap[stream] = heap;
      list->active_so_stats_index[stream] = index;
      return;
   }

   if (heap->desc.Type != D3D12_QUERY_HEAP_TYPE_OCCLUSION) {
      mesa_loge("BeginQuery currently supports occlusion heaps only");
      list->state = D12_LIST_ERROR;
      return;
   }

   uint64_t start = heap->bo->va->addr + heap->start_offset +
                    (uint64_t)index * heap->stride;
   d12_list_copy_gpu_bytes(list, start, list->device->oclqry_bo->va->addr,
                           sizeof(uint64_t));
   if (list->state == D12_LIST_RECORDING)
      list->active_occlusion_queries++;
}

static void STDMETHODCALLTYPE
d12_list_EndQuery(ID3D12GraphicsCommandList6 *iface,
                  ID3D12QueryHeap *query_heap, D3D12_QUERY_TYPE type,
                  UINT index)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   struct d12_query_heap *heap = (struct d12_query_heap *)query_heap;

   if (list->state != D12_LIST_RECORDING)
      return;

   if (!d12_list_validate_query(list, heap, type, index))
      return;

   uint64_t result = heap->bo->va->addr + (uint64_t)index * heap->stride;

   if (heap->desc.Type == D3D12_QUERY_HEAP_TYPE_SO_STATISTICS) {
      unsigned stream = type - D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
      if (stream >= ARRAY_SIZE(list->active_so_stats_heap) ||
          list->active_so_stats_heap[stream] != heap ||
          list->active_so_stats_index[stream] != index) {
         mesa_loge("EndQuery has no matching stream-output statistics query");
         list->state = D12_LIST_ERROR;
         return;
      }

      list->active_so_stats_heap[stream] = NULL;
      return;
   }

   if (heap->desc.Type == D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS) {
      if (list->active_pipeline_stats_heap != heap ||
          list->active_pipeline_stats_index != index) {
         mesa_loge("EndQuery has no matching pipeline statistics query");
         list->state = D12_LIST_ERROR;
         return;
      }

      uint64_t source = d12_list_upload(
         list, &list->pipeline_stats, sizeof(list->pipeline_stats), 8);
      if (source)
         d12_list_copy_gpu_bytes(list, result, source,
                                 sizeof(list->pipeline_stats));
      list->active_pipeline_stats_heap = NULL;
      return;
   }

   if (heap->desc.Type == D3D12_QUERY_HEAP_TYPE_TIMESTAMP) {
      /* The kernel-calibrated GPU clock is sampled while recording, then
       * copied by the GPU at this exact point in the command stream.  This
       * preserves Resolve-before-rewrite ordering across submissions.
       */
      uint64_t timestamp = agx_get_gpu_timestamp(&list->device->dev);
      uint64_t source =
         d12_list_upload(list, &timestamp, sizeof(timestamp), 8);
      if (source)
         d12_list_copy_gpu_bytes(list, result, source, sizeof(timestamp));
      return;
   }

   if (heap->desc.Type != D3D12_QUERY_HEAP_TYPE_OCCLUSION ||
       !list->active_occlusion_queries) {
      mesa_loge("EndQuery has no matching active occlusion query");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct libagx_query_delta_args args = {
      .counter = list->device->oclqry_bo->va->addr,
      .start = heap->bo->va->addr + heap->start_offset +
               (uint64_t)index * heap->stride,
      .result = result,
   };
   d12_list_dispatch_precomp(list, LIBAGX_QUERY_DELTA, &args, sizeof(args),
                             agx_1d(1));
   list->active_occlusion_queries--;
}

static void STDMETHODCALLTYPE
d12_list_ResolveQueryData(ID3D12GraphicsCommandList6 *iface,
                          ID3D12QueryHeap *query_heap,
                          D3D12_QUERY_TYPE type, UINT first_query,
                          UINT query_count, ID3D12Resource *destination,
                          UINT64 destination_offset)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   struct d12_query_heap *heap = (struct d12_query_heap *)query_heap;
   struct d12_resource *dst = destination
                                 ? d12_resource_from_iface(destination)
                                 : NULL;

   if (list->state != D12_LIST_RECORDING || !query_count)
      return;

   if (!heap || first_query >= heap->desc.Count ||
       query_count > heap->desc.Count - first_query ||
       !d12_query_type_matches(heap, type) || !dst ||
       dst->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
       destination_offset > dst->desc.Width ||
       (uint64_t)query_count * heap->stride >
          dst->desc.Width - destination_offset) {
      mesa_loge("ResolveQueryData: invalid heap, range, or destination");
      list->state = D12_LIST_ERROR;
      return;
   }

   uint64_t src = heap->bo->va->addr +
                  (uint64_t)first_query * heap->stride;
   uint64_t out = dst->addr + destination_offset;

   if (type == D3D12_QUERY_TYPE_BINARY_OCCLUSION) {
      /* libagx_copy_query_gl's U64/bool path canonicalises every nonzero
       * sample count to the 64-bit value one required by D3D12.
       */
      for (UINT i = 0; i < query_count; i++) {
         struct libagx_copy_query_gl_args args = {
            .query = src + (uint64_t)i * heap->stride,
            .dest = out + (uint64_t)i * heap->stride,
            .value_type = 3, /* PIPE_QUERY_TYPE_U64 */
            .bool_size = 8,
         };
         d12_list_dispatch_precomp(list, LIBAGX_COPY_QUERY_GL, &args,
                                   sizeof(args), agx_1d(1));
      }
   } else {
      d12_list_copy_gpu_bytes(list, out, src,
                              (size_t)query_count * heap->stride);
   }
}

static void STDMETHODCALLTYPE
d12_list_CopyBufferRegion(ID3D12GraphicsCommandList6 *iface,
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
      .dest = dstr->addr + dst_offset,
      .src = srcr->addr + src_offset,
   };

   d12_list_dispatch_precomp(list, LIBAGX_COPY_UCHAR, &args, sizeof(args),
                             agx_1d(num_bytes));
}

static void STDMETHODCALLTYPE
d12_list_WriteBufferImmediate(
   ID3D12GraphicsCommandList6 *iface, UINT count,
   const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *params,
   const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING)
      return;

   if (count && !params) {
      mesa_loge("WriteBufferImmediate: null parameter array");
      list->state = D12_LIST_ERROR;
      return;
   }

   for (UINT i = 0; i < count; i++) {
      D3D12_WRITEBUFFERIMMEDIATE_MODE mode =
         modes ? modes[i] : D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT;
      if (mode != D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT &&
          mode != D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN &&
          mode != D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT) {
         mesa_loge("WriteBufferImmediate: invalid mode %u", (unsigned)mode);
         list->state = D12_LIST_ERROR;
         list->record_error = E_INVALIDARG;
         return;
      }

      if (!params[i].Dest || (params[i].Dest & 3)) {
         mesa_loge("WriteBufferImmediate: invalid destination 0x%llx",
                   (unsigned long long)params[i].Dest);
         list->state = D12_LIST_ERROR;
         return;
      }

      /* Marker modes differ only in observability to profiling tools. The
       * memory operation is still the same ordered 32-bit write. */
      struct libagx_write_u32_args args = {
         .address = params[i].Dest,
         .value = params[i].Value,
      };
      d12_list_dispatch_precomp(list, LIBAGX_WRITE_U32, &args, sizeof(args),
                                agx_1d(1));
      if (list->state == D12_LIST_ERROR)
         return;
   }
}

struct d12_copy_surface {
   struct agx_bo *bo;
   uint64_t base;
   uint64_t storage_offset_B;
   uint64_t size_B;
   uint32_t width_px;
   uint32_t height_px;
   uint32_t width_el;
   uint32_t height_el;
   uint32_t block_width_px;
   uint32_t block_height_px;
   uint32_t blocksize_B;
   uint32_t pitch_B;
   uint32_t stride_el;
   uint32_t tile_width_el;
   uint32_t tile_height_el;
   uint8_t sample_count;
   bool tiled;
};

static bool
d12_copy_surface_check_storage(const struct d12_copy_surface *surface,
                               const char *name)
{
   if (!surface->bo || surface->storage_offset_B > surface->bo->size ||
       surface->size_B > surface->bo->size - surface->storage_offset_B) {
      mesa_loge("CopyTextureRegion: %s surface [0x%" PRIx64
                ", +0x%" PRIx64 ") exceeds BO size 0x%zx",
                name, surface->storage_offset_B, surface->size_B,
                surface->bo ? (size_t)surface->bo->size : 0);
      return false;
   }

   return true;
}

static bool
d12_copy_surface_from_texture(struct d12_copy_surface *surface,
                              struct d12_resource *res, UINT subresource,
                              const char *name)
{
   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
      mesa_loge("CopyTextureRegion: %s is not a 2D texture", name);
      return false;
   }

   const unsigned mips = MAX2(res->desc.MipLevels, 1);
   const unsigned layers = MAX2(res->desc.DepthOrArraySize, 1);
   const uint64_t plane_size = (uint64_t)mips * layers;
   const unsigned plane = subresource / plane_size;
   const unsigned in_plane = subresource % plane_size;
   const unsigned mip = in_plane % mips;
   const unsigned layer = in_plane / mips;

   const struct ail_layout *layout;
   struct agx_bo *bo;
   uint64_t plane_base;
   if (plane == 0) {
      layout = &res->layout;
      bo = res->bo;
      plane_base = res->addr;
   } else if (plane == 1 && res->stencil_bo &&
              res->stencil_layout.size_B) {
      layout = &res->stencil_layout;
      bo = res->stencil_bo;
      plane_base = res->stencil_addr;
   } else {
      mesa_loge("CopyTextureRegion: %s plane %u unsupported", name, plane);
      return false;
   }

   if (mip >= layout->levels || layer >= layout->depth_px) {
      mesa_loge("CopyTextureRegion: %s subresource %u is out of range", name,
                subresource);
      return false;
   }

   const enum pipe_format format = layout->format;
   const uint32_t width_px = u_minify(layout->width_px, mip);
   const uint32_t height_px = u_minify(layout->height_px, mip);
   const uint32_t samples = MAX2(layout->sample_count_sa, 1);
   const uint64_t offset_B = ail_get_layer_level_B(layout, layer, mip);
   const uint64_t size_B = ail_get_level_size_B(layout, mip);
   if (!bo || plane_base < bo->va->addr) {
      mesa_loge("CopyTextureRegion: %s plane has invalid storage", name);
      return false;
   }

   *surface = (struct d12_copy_surface){
      .bo = bo,
      .base = plane_base + offset_B,
      .storage_offset_B = plane_base - bo->va->addr + offset_B,
      .size_B = size_B,
      .width_px = width_px,
      .height_px = height_px,
      .width_el = util_format_get_nblocksx(format, width_px),
      .height_el = util_format_get_nblocksy(format, height_px),
      .block_width_px = util_format_get_blockwidth(format),
      .block_height_px = util_format_get_blockheight(format),
      /* AIL tiles one logical pixel as a block containing all samples. Raw
       * texture copies therefore copy the whole sample block together. */
      .blocksize_B = util_format_get_blocksize(format) * samples,
      .sample_count = samples,
      .tiled = layout->tiling != AIL_TILING_LINEAR,
   };

   if (surface->tiled) {
      if (!ail_is_level_twiddled_uncompressed(layout, mip)) {
         mesa_loge("CopyTextureRegion: %s uses compressed metadata", name);
         return false;
      }

      surface->stride_el = layout->stride_el[mip];
      surface->tile_width_el = layout->tilesize_el[mip].width_el;
      surface->tile_height_el = layout->tilesize_el[mip].height_el;
      if (!surface->stride_el || !surface->tile_width_el ||
          !surface->tile_height_el) {
         mesa_loge("CopyTextureRegion: %s has an invalid tiled layout", name);
         return false;
      }
   } else {
      surface->pitch_B = ail_get_linear_stride_B(layout, mip);
   }

   return d12_copy_surface_check_storage(surface, name);
}

static bool
d12_copy_surface_from_footprint(
   struct d12_copy_surface *surface, struct d12_resource *res,
   const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *placed, const char *name)
{
   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
      mesa_loge("CopyTextureRegion: %s footprint is not in a buffer", name);
      return false;
   }

   enum pipe_format format = d12_format_to_pipe(placed->Footprint.Format);
   if (format == PIPE_FORMAT_NONE)
      format = d12_format_depth_plane(placed->Footprint.Format);

   if (format == PIPE_FORMAT_NONE || placed->Footprint.Depth != 1 ||
       !placed->Footprint.Width || !placed->Footprint.Height) {
      mesa_loge("CopyTextureRegion: %s has an unsupported footprint", name);
      return false;
   }

   const uint32_t block_width = util_format_get_blockwidth(format);
   const uint32_t block_height = util_format_get_blockheight(format);
   const uint32_t blocksize = util_format_get_blocksize(format);
   const uint32_t width_el =
      util_format_get_nblocksx(format, placed->Footprint.Width);
   const uint32_t height_el =
      util_format_get_nblocksy(format, placed->Footprint.Height);
   const uint64_t minimum_pitch = (uint64_t)width_el * blocksize;

   if (placed->Footprint.RowPitch < minimum_pitch) {
      mesa_loge("CopyTextureRegion: %s row pitch %u is smaller than %" PRIu64,
                name, placed->Footprint.RowPitch, minimum_pitch);
      return false;
   }

   const uint64_t size_B =
      (uint64_t)(height_el - 1) * placed->Footprint.RowPitch + minimum_pitch;
   if (placed->Offset > res->desc.Width ||
       size_B > res->desc.Width - placed->Offset) {
      mesa_loge("CopyTextureRegion: %s footprint exceeds its buffer", name);
      return false;
   }

   *surface = (struct d12_copy_surface){
      .bo = res->bo,
      .base = res->addr + placed->Offset,
      .storage_offset_B = res->offset_B + placed->Offset,
      .size_B = size_B,
      .width_px = placed->Footprint.Width,
      .height_px = placed->Footprint.Height,
      .width_el = width_el,
      .height_el = height_el,
      .block_width_px = block_width,
      .block_height_px = block_height,
      .blocksize_B = blocksize,
      .pitch_B = placed->Footprint.RowPitch,
      .sample_count = 1,
   };

   return d12_copy_surface_check_storage(surface, name);
}

static bool
d12_copy_surface_from_location(struct d12_copy_surface *surface,
                               const D3D12_TEXTURE_COPY_LOCATION *location,
                               const char *name)
{
   struct d12_resource *res = d12_resource_from_iface(location->pResource);

   if (location->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
      return d12_copy_surface_from_texture(surface, res,
                                           location->SubresourceIndex, name);
   if (location->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
      return d12_copy_surface_from_footprint(surface, res,
                                             &location->PlacedFootprint, name);

   mesa_loge("CopyTextureRegion: %s location type %u unsupported", name,
             location->Type);
   return false;
}

static bool
d12_copy_element_coord(uint32_t px, uint32_t block_px, uint32_t *el,
                       const char *name)
{
   if (px % block_px) {
      mesa_loge("CopyTextureRegion: %s coordinate %u is not block aligned",
                name, px);
      return false;
   }

   *el = px / block_px;
   return true;
}

static void
d12_copy_texture_region(struct d12_cmd_list *list,
                        const D3D12_TEXTURE_COPY_LOCATION *dst,
                        UINT dst_x, UINT dst_y, UINT dst_z,
                        const D3D12_TEXTURE_COPY_LOCATION *src,
                        const D3D12_BOX *src_box)
{
   if (!dst || !src || !dst->pResource || !src->pResource) {
      mesa_loge("CopyTextureRegion: null location");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (src_box && (src_box->left >= src_box->right ||
                   src_box->top >= src_box->bottom ||
                   src_box->front >= src_box->back))
      return;

   if (dst_z != 0 || (src_box && (src_box->front != 0 || src_box->back != 1))) {
      mesa_loge("CopyTextureRegion: 3D copies are unsupported");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_copy_surface d, s;
   if (!d12_copy_surface_from_location(&d, dst, "destination") ||
       !d12_copy_surface_from_location(&s, src, "source")) {
      list->state = D12_LIST_ERROR;
      return;
   }

   if (d.blocksize_B != s.blocksize_B) {
      mesa_loge("CopyTextureRegion: source/destination elements are %u/%u "
                "bytes", s.blocksize_B, d.blocksize_B);
      list->state = D12_LIST_ERROR;
      return;
   }

   if (d.sample_count != s.sample_count) {
      mesa_loge("CopyTextureRegion: source/destination sample counts are "
                "%u/%u", s.sample_count, d.sample_count);
      list->state = D12_LIST_ERROR;
      return;
   }

   const uint32_t src_left = src_box ? src_box->left : 0;
   const uint32_t src_top = src_box ? src_box->top : 0;
   const uint32_t src_right = src_box ? src_box->right : s.width_px;
   const uint32_t src_bottom = src_box ? src_box->bottom : s.height_px;
   uint32_t src_x_el, src_y_el, dst_x_el, dst_y_el;

   if (!d12_copy_element_coord(src_left, s.block_width_px, &src_x_el,
                               "source x") ||
       !d12_copy_element_coord(src_top, s.block_height_px, &src_y_el,
                               "source y") ||
       !d12_copy_element_coord(dst_x, d.block_width_px, &dst_x_el,
                               "destination x") ||
       !d12_copy_element_coord(dst_y, d.block_height_px, &dst_y_el,
                               "destination y")) {
      list->state = D12_LIST_ERROR;
      return;
   }

   const uint32_t src_right_el =
      DIV_ROUND_UP(src_right, s.block_width_px);
   const uint32_t src_bottom_el =
      DIV_ROUND_UP(src_bottom, s.block_height_px);
   if (src_right_el <= src_x_el || src_bottom_el <= src_y_el)
      return;

   const uint32_t width_el = src_right_el - src_x_el;
   const uint32_t height_el = src_bottom_el - src_y_el;
   if (src_right_el > s.width_el || src_bottom_el > s.height_el ||
       width_el > d.width_el - MIN2(d.width_el, dst_x_el) ||
       height_el > d.height_el - MIN2(d.height_el, dst_y_el)) {
      mesa_loge("CopyTextureRegion: element rectangle is out of bounds "
                "(src %u,%u..%u,%u of %ux%u; dst %u,%u + %ux%u of %ux%u)",
                src_x_el, src_y_el, src_right_el, src_bottom_el,
                s.width_el, s.height_el, dst_x_el, dst_y_el, width_el,
                height_el, d.width_el, d.height_el);
      list->state = D12_LIST_ERROR;
      return;
   }

   struct libagx_copy_image_args args = {
      .dest = d.base,
      .src = s.base,
      .width_el = width_el,
      .height_el = height_el,
      .dest_x_el = dst_x_el,
      .dest_y_el = dst_y_el,
      .src_x_el = src_x_el,
      .src_y_el = src_y_el,
      .dest_pitch_B = d.pitch_B,
      .src_pitch_B = s.pitch_B,
      .dest_stride_el = d.stride_el,
      .src_stride_el = s.stride_el,
      .dest_tile_width_el = d.tile_width_el,
      .dest_tile_height_el = d.tile_height_el,
      .src_tile_width_el = s.tile_width_el,
      .src_tile_height_el = s.tile_height_el,
      .blocksize_B = s.blocksize_B,
      .dest_tiled = d.tiled,
      .src_tiled = s.tiled,
   };

   d12_list_dispatch_precomp(list, LIBAGX_COPY_IMAGE, &args, sizeof(args),
                             agx_1d((uint64_t)width_el * height_el));
}

static void
d12_resolve(struct d12_cmd_list *list, ID3D12Resource *dst,
            UINT dst_subresource, UINT dst_x, UINT dst_y,
            ID3D12Resource *src, UINT src_subresource,
            const D3D12_RECT *src_rect, DXGI_FORMAT format,
            D3D12_RESOLVE_MODE mode)
{
   struct d12_resource *dstr = d12_resource_from_iface(dst);
   struct d12_resource *srcr = d12_resource_from_iface(src);
   const unsigned dst_samples = MAX2(dstr->layout.sample_count_sa, 1);
   const unsigned src_samples = MAX2(srcr->layout.sample_count_sa, 1);

   const bool average = mode == D3D12_RESOLVE_MODE_AVERAGE;
   const bool min_float32 =
      mode == D3D12_RESOLVE_MODE_MIN && format == DXGI_FORMAT_D32_FLOAT;
   const bool unorm8x4 = average && format == DXGI_FORMAT_R8G8B8A8_UNORM;
   const bool float32x4 =
      average && format == DXGI_FORMAT_R32G32B32A32_FLOAT;
   const unsigned pixel_B =
      min_float32 || unorm8x4 ? 4 : float32x4 ? 16 : 0;
   if (dst_samples != 1 || (src_samples != 2 && src_samples != 4) ||
       !pixel_B) {
      mesa_loge("ResolveSubresource: unsupported format/mode/sample shape "
                "(format %u, mode %u, %u -> %u samples)",
                (unsigned)format, (unsigned)mode, src_samples, dst_samples);
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_copy_surface d, s;
   if (!d12_copy_surface_from_texture(&d, dstr, dst_subresource,
                                      "resolve destination") ||
       !d12_copy_surface_from_texture(&s, srcr, src_subresource,
                                      "resolve source") ||
       d.blocksize_B != pixel_B ||
       s.blocksize_B != pixel_B * src_samples || !s.tiled) {
      mesa_loge("ResolveSubresource: incompatible source/destination");
      list->state = D12_LIST_ERROR;
      return;
   }

   const int64_t left = src_rect ? src_rect->left : 0;
   const int64_t top = src_rect ? src_rect->top : 0;
   const int64_t right = src_rect ? src_rect->right : s.width_px;
   const int64_t bottom = src_rect ? src_rect->bottom : s.height_px;
   if (left < 0 || top < 0 || right < left || bottom < top ||
       right > s.width_px || bottom > s.height_px ||
       (uint64_t)dst_x + (right - left) > d.width_px ||
       (uint64_t)dst_y + (bottom - top) > d.height_px) {
      mesa_loge("ResolveSubresource: resolve rectangle is out of bounds");
      list->state = D12_LIST_ERROR;
      return;
   }

   const unsigned width_px = right - left;
   const unsigned height_px = bottom - top;
   if (!width_px || !height_px)
      return;

   struct libagx_resolve_unorm8x4_args args = {
      .dest = d.base,
      .src = s.base,
      .width_px = width_px,
      .height_px = height_px,
      .dest_x_px = dst_x,
      .dest_y_px = dst_y,
      .src_x_px = left,
      .src_y_px = top,
      .dest_pitch_B = d.pitch_B,
      .dest_stride_el = d.stride_el,
      .src_stride_el = s.stride_el,
      .dest_tile_width_el = d.tile_width_el,
      .dest_tile_height_el = d.tile_height_el,
      .src_tile_width_el = s.tile_width_el,
      .src_tile_height_el = s.tile_height_el,
      .sample_count = src_samples,
      .dest_tiled = d.tiled,
      .src_tiled = s.tiled,
   };

   const enum libagx_program shader =
      min_float32 ? LIBAGX_RESOLVE_FLOAT32_MIN
                  : unorm8x4 ? LIBAGX_RESOLVE_UNORM8X4
                              : LIBAGX_RESOLVE_FLOAT32X4;
   d12_list_dispatch_precomp(list, shader, &args, sizeof(args),
                             agx_1d((uint64_t)width_px * height_px));
}

static void STDMETHODCALLTYPE
d12_list_ResolveSubresource(ID3D12GraphicsCommandList6 *iface,
                            ID3D12Resource *dst, UINT dst_subresource,
                            ID3D12Resource *src, UINT src_subresource,
                            DXGI_FORMAT format)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   if (list->state != D12_LIST_RECORDING || !dst || !src)
      return;

   d12_resolve(list, dst, dst_subresource, 0, 0, src, src_subresource, NULL,
               format, D3D12_RESOLVE_MODE_AVERAGE);
}

static void STDMETHODCALLTYPE
d12_list_ResolveSubresourceRegion(ID3D12GraphicsCommandList6 *iface,
                                  ID3D12Resource *dst, UINT dst_subresource,
                                  UINT dst_x, UINT dst_y, ID3D12Resource *src,
                                  UINT src_subresource, D3D12_RECT *src_rect,
                                  DXGI_FORMAT format,
                                  D3D12_RESOLVE_MODE resolve_mode)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   if (list->state != D12_LIST_RECORDING || !dst || !src)
      return;

   if (resolve_mode == D3D12_RESOLVE_MODE_AVERAGE ||
       resolve_mode == D3D12_RESOLVE_MODE_MIN) {
      d12_resolve(list, dst, dst_subresource, dst_x, dst_y, src,
                  src_subresource, src_rect, format, resolve_mode);
      return;
   }

   if (resolve_mode == D3D12_RESOLVE_MODE_DECOMPRESS) {
      D3D12_TEXTURE_COPY_LOCATION d = {
         .pResource = dst,
         .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
         .SubresourceIndex = dst_subresource,
      };
      D3D12_TEXTURE_COPY_LOCATION s = {
         .pResource = src,
         .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
         .SubresourceIndex = src_subresource,
      };
      D3D12_BOX box;
      D3D12_BOX *box_ptr = NULL;
      if (src_rect) {
         if (src_rect->left < 0 || src_rect->top < 0 ||
             src_rect->right < src_rect->left ||
             src_rect->bottom < src_rect->top) {
            mesa_loge("ResolveSubresourceRegion: invalid source rectangle");
            list->state = D12_LIST_ERROR;
            return;
         }
         box = (D3D12_BOX){
            .left = src_rect->left,
            .top = src_rect->top,
            .front = 0,
            .right = src_rect->right,
            .bottom = src_rect->bottom,
            .back = 1,
         };
         box_ptr = &box;
      }

      d12_copy_texture_region(list, &d, dst_x, dst_y, 0, &s, box_ptr);
      return;
   }

   mesa_loge("ResolveSubresourceRegion: unsupported resolve mode %u",
             (unsigned)resolve_mode);
   list->state = D12_LIST_ERROR;
}

static void STDMETHODCALLTYPE
d12_list_CopyResource(ID3D12GraphicsCommandList6 *iface, ID3D12Resource *dst,
                      ID3D12Resource *src)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING || !dst || !src)
      return;

   struct d12_resource *dstr = d12_resource_from_iface(dst);
   struct d12_resource *srcr = d12_resource_from_iface(src);

   if (dstr->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
       srcr->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
      if (dstr->desc.Width != srcr->desc.Width) {
         mesa_loge("CopyResource: buffer sizes differ");
         list->state = D12_LIST_ERROR;
         return;
      }

      d12_list_CopyBufferRegion(iface, dst, 0, src, 0, dstr->desc.Width);
      return;
   }

   if (dstr->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       srcr->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
      mesa_loge("CopyResource: resource dimensions differ or are unsupported");
      list->state = D12_LIST_ERROR;
      return;
   }

   /* Invalid format-size copies are observed in shipped applications. Native
    * implementations either ignore them or perform an implementation-defined
    * bit copy; skipping is safe and matches vkd3d-proton's hang avoidance.
    */
   if (util_format_get_blocksize(dstr->layout.format) !=
       util_format_get_blocksize(srcr->layout.format)) {
      mesa_logw("CopyResource: ignoring incompatible texture element sizes");
      return;
   }

   const unsigned dst_subresources =
      MAX2(dstr->desc.MipLevels, 1) * MAX2(dstr->desc.DepthOrArraySize, 1) *
      (dstr->stencil_bo ? 2 : 1);
   const unsigned src_subresources =
      MAX2(srcr->desc.MipLevels, 1) * MAX2(srcr->desc.DepthOrArraySize, 1) *
      (srcr->stencil_bo ? 2 : 1);
   if (dst_subresources != src_subresources) {
      mesa_loge("CopyResource: texture subresource counts differ");
      list->state = D12_LIST_ERROR;
      return;
   }

   for (unsigned i = 0; i < dst_subresources; ++i) {
      D3D12_TEXTURE_COPY_LOCATION d = {
         .pResource = dst,
         .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
         .SubresourceIndex = i,
      };
      D3D12_TEXTURE_COPY_LOCATION s = {
         .pResource = src,
         .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
         .SubresourceIndex = i,
      };

      d12_copy_texture_region(list, &d, 0, 0, 0, &s, NULL);
      if (list->state == D12_LIST_ERROR)
         return;
   }
}

static void STDMETHODCALLTYPE
d12_list_ClearRenderTargetView(ID3D12GraphicsCommandList6 *iface,
                               D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle,
                               const FLOAT color[4], UINT num_rects,
                               const D3D12_RECT *rects)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING)
      return;

   if (num_rects && !rects) {
      mesa_loge("ClearRenderTargetView: null rectangle array");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_rtv *rtv = (struct d12_rtv *)rtv_handle.ptr;
   if (!rtv || !rtv->resource || rtv->kind != D12_DESC_RTV) {
      mesa_loge("ClearRenderTargetView: invalid RTV handle");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (num_rects != 0) {
      struct d12_resource *res = rtv->resource;
      struct d12_copy_surface surface;
      if (!d12_copy_surface_from_texture(&surface, res, 0,
                                         "partial-clear destination")) {
         list->state = D12_LIST_ERROR;
         return;
      }

      if (rtv->address < res->addr) {
         mesa_loge("ClearRenderTargetView: RTV address precedes its resource");
         list->state = D12_LIST_ERROR;
         return;
      }

      const uint64_t view_offset_B = rtv->address - res->addr;
      if (view_offset_B > res->layout.size_B ||
          surface.size_B > res->layout.size_B - view_offset_B) {
         mesa_loge("ClearRenderTargetView: RTV slice exceeds its resource");
         list->state = D12_LIST_ERROR;
         return;
      }
      surface.base = rtv->address;
      surface.storage_offset_B = res->offset_B + view_offset_B;
      if (!d12_copy_surface_check_storage(&surface,
                                          "partial-clear destination")) {
         list->state = D12_LIST_ERROR;
         return;
      }

      enum pipe_format format = d12_format_to_pipe(rtv->format);
      const unsigned format_blocksize =
         format != PIPE_FORMAT_NONE ? util_format_get_blocksize(format) : 0;
      if (format == PIPE_FORMAT_NONE ||
          format_blocksize * surface.sample_count != surface.blocksize_B ||
          surface.blocksize_B > 64 || surface.block_width_px != 1 ||
          surface.block_height_px != 1) {
         mesa_loge("ClearRenderTargetView: partial clear format %u "
                   "unsupported", (unsigned)rtv->format);
         list->state = D12_LIST_ERROR;
         return;
      }

      union {
         float f[4];
         uint32_t u[4];
         int32_t i[4];
      } converted;
      if (util_format_is_pure_uint(format)) {
         for (unsigned c = 0; c < 4; ++c)
            converted.u[c] = (uint32_t)color[c];
      } else if (util_format_is_pure_sint(format)) {
         for (unsigned c = 0; c < 4; ++c)
            converted.i[c] = (int32_t)color[c];
      } else {
         memcpy(converted.f, color, sizeof(converted.f));
      }

      uint8_t packed[64] = {0};
      util_format_pack_rgba(format, packed, &converted, 1);
      for (unsigned s = 1; s < surface.sample_count; ++s)
         memcpy(packed + s * format_blocksize, packed, format_blocksize);
      uint64_t value = d12_list_upload(list, packed, surface.blocksize_B, 16);
      if (!value)
         return;

      for (unsigned i = 0; i < num_rects; ++i) {
         const int64_t left = MAX2((int64_t)rects[i].left, 0);
         const int64_t top = MAX2((int64_t)rects[i].top, 0);
         const int64_t right =
            MIN2((int64_t)rects[i].right, (int64_t)surface.width_px);
         const int64_t bottom =
            MIN2((int64_t)rects[i].bottom, (int64_t)surface.height_px);

         if (right <= left || bottom <= top)
            continue;

         const uint32_t width = right - left;
         const uint32_t height = bottom - top;
         struct libagx_fill_image_args args = {
            .dest = surface.base,
            .value = value,
            .width_el = width,
            .height_el = height,
            .dest_x_el = left,
            .dest_y_el = top,
            .dest_pitch_B = surface.pitch_B,
            .dest_stride_el = surface.stride_el,
            .dest_tile_width_el = surface.tile_width_el,
            .dest_tile_height_el = surface.tile_height_el,
            .blocksize_B = surface.blocksize_B,
            .dest_tiled = surface.tiled,
         };
         d12_list_dispatch_precomp(list, LIBAGX_FILL_IMAGE, &args,
                                   sizeof(args),
                                   agx_1d((uint64_t)width * height));
         if (list->state == D12_LIST_ERROR)
            return;
      }
      return;
   }

   d12_list_begin_render(list, rtv, 1, NULL, NULL, AGX_BG_CLEAR, color, true);
}

/* Raw element copies cover linear placed footprints and AIL tiled texture
 * subresources through the shared, bounds-checked path above. */
static void STDMETHODCALLTYPE
d12_list_CopyTextureRegion(ID3D12GraphicsCommandList6 *iface,
                           const D3D12_TEXTURE_COPY_LOCATION *dst,
                           UINT dst_x, UINT dst_y, UINT dst_z,
                           const D3D12_TEXTURE_COPY_LOCATION *src,
                           const D3D12_BOX *src_box)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING)
      return;

   d12_copy_texture_region(list, dst, dst_x, dst_y, dst_z, src, src_box);
   return;

}

static void STDMETHODCALLTYPE
d12_list_ResourceBarrier(ID3D12GraphicsCommandList6 *iface, UINT num_barriers,
                         const D3D12_RESOURCE_BARRIER *barriers)
{
   /* Every dispatch already ends in agx_cdm_barrier (cache flush), memory
    * is coherent UMA, and segments execute in order via the submit-header
    * barrier fields. State transitions are accepted as no-ops until image
    * layout transitions matter.
    */
   for (UINT i = 0; i < num_barriers; i++) {
      if (barriers[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
          barriers[i].Type != D3D12_RESOURCE_BARRIER_TYPE_UAV) {
         mesa_logw("ResourceBarrier: type %d ignored",
                   (int)barriers[i].Type);
      }
   }
}

/*
 * D3D12's integer UAV clear does not numerically convert through the view's
 * type.  It truncates each UINT component to the corresponding storage
 * channel.  That distinction matters for views such as R16G16_FLOAT: a UINT
 * clear of {0x1234, 0xabcd} writes 0xabcd1234, whereas a float pack would
 * reinterpret those inputs as tiny floating-point values and write zeroes.
 *
 * Keep the table deliberately limited to the four-byte buffer formats the
 * fill kernel below can write exactly.  Unsupported formats fail recording
 * instead of silently producing a differently converted bit pattern.
 */
static bool
d12_pack_clear_uav_uint(DXGI_FORMAT format, const UINT values[4],
                        uint32_t *packed)
{
   switch (format) {
   case DXGI_FORMAT_R32_TYPELESS:
   case DXGI_FORMAT_R32_FLOAT:
   case DXGI_FORMAT_R32_UINT:
   case DXGI_FORMAT_R32_SINT:
      *packed = values[0];
      return true;

   case DXGI_FORMAT_R16G16_FLOAT:
   case DXGI_FORMAT_R16G16_UNORM:
   case DXGI_FORMAT_R16G16_UINT:
   case DXGI_FORMAT_R16G16_SNORM:
   case DXGI_FORMAT_R16G16_SINT:
      *packed = (values[0] & 0xffffu) | ((values[1] & 0xffffu) << 16);
      return true;

   case DXGI_FORMAT_R8G8B8A8_UNORM:
   case DXGI_FORMAT_R8G8B8A8_UINT:
   case DXGI_FORMAT_R8G8B8A8_SNORM:
   case DXGI_FORMAT_R8G8B8A8_SINT:
      *packed = (values[0] & 0xffu) | ((values[1] & 0xffu) << 8) |
                ((values[2] & 0xffu) << 16) |
                ((values[3] & 0xffu) << 24);
      return true;

   case DXGI_FORMAT_R11G11B10_FLOAT:
      *packed = (values[0] & 0x7ffu) | ((values[1] & 0x7ffu) << 11) |
                ((values[2] & 0x3ffu) << 22);
      return true;

   case DXGI_FORMAT_R10G10B10A2_UNORM:
   case DXGI_FORMAT_R10G10B10A2_UINT:
      *packed = (values[0] & 0x3ffu) | ((values[1] & 0x3ffu) << 10) |
                ((values[2] & 0x3ffu) << 20) |
                ((values[3] & 0x3u) << 30);
      return true;

   default:
      return false;
   }
}

static bool
d12_pack_clear_uav_float(DXGI_FORMAT format, const FLOAT values[4],
                         uint32_t *packed)
{
   enum pipe_format pipe = d12_format_to_pipe(format);

   if (pipe == PIPE_FORMAT_NONE || util_format_get_blocksize(pipe) != 4)
      return false;

   util_format_pack_rgba(pipe, packed, values, 1);
   return true;
}

static void
d12_list_clear_uav_buffer(struct d12_cmd_list *list,
                          D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
                          D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                          ID3D12Resource *resource, uint32_t packed,
                          UINT num_rects)
{
   if (list->state != D12_LIST_RECORDING)
      return;

   /*
    * Hardware implementations generally treat the CPU descriptor as
    * authoritative for ClearUAV; the API's GPU handle is an opaque companion
    * used to make the descriptor shader-visible.  This driver flattens GPU
    * descriptors on the CPU and never dereferences one on the GPU, so using
    * the CPU view is both the native model and avoids trusting an arbitrary
    * GPU token as a host pointer.
    */
   (void)gpu_handle;

   struct d12_descriptor *view = (struct d12_descriptor *)cpu_handle.ptr;
   struct d12_resource *res =
      resource ? d12_resource_from_iface(resource) : NULL;

   if (!view || !res || view->kind != D12_DESC_UAV ||
       view->resource != res ||
       res->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
      mesa_loge("ClearUnorderedAccessView: invalid buffer UAV");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (num_rects != 0) {
      mesa_loge("ClearUnorderedAccessView: buffer rect clears unsupported");
      list->state = D12_LIST_ERROR;
      return;
   }

   if ((view->address & 3) || (view->size_B & 3)) {
      mesa_loge("ClearUnorderedAccessView: buffer view is not uint-aligned");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (!view->size_B)
      return;

   struct libagx_fill_args args = {
      .address = view->address,
      .value = packed,
   };

   d12_list_dispatch_precomp(list, LIBAGX_FILL, &args, sizeof(args),
                             agx_1d(view->size_B / 4));
}

static void
d12_list_clear_uav_texture(struct d12_cmd_list *list,
                           D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
                           D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                           ID3D12Resource *resource, uint32_t packed,
                           UINT num_rects, const D3D12_RECT *rects)
{
   /* As with buffer clears, the CPU descriptor is authoritative. The GPU
    * handle is the shader-visible companion and need not be dereferenced by
    * this command-recording implementation. */
   (void)gpu_handle;

   struct d12_descriptor *view = (struct d12_descriptor *)cpu_handle.ptr;
   struct d12_resource *res =
      resource ? d12_resource_from_iface(resource) : NULL;

   if (!view || !res || view->kind != D12_DESC_UAV ||
       view->resource != res ||
       res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       (num_rects && !rects)) {
      mesa_loge("ClearUnorderedAccessView: invalid texture UAV");
      list->state = D12_LIST_ERROR;
      return;
   }

   const unsigned mips = MAX2(res->desc.MipLevels, 1);
   const unsigned resource_layers = MAX2(res->desc.DepthOrArraySize, 1);
   const uint64_t layer_stride = res->layout.layer_stride_B;
   if (view->address < res->addr || !layer_stride ||
       (view->address - res->addr) % layer_stride) {
      mesa_loge("ClearUnorderedAccessView: invalid texture layer address");
      list->state = D12_LIST_ERROR;
      return;
   }

   const uint64_t first_layer64 =
      (view->address - res->addr) / layer_stride;
   const unsigned layers = MAX2(view->texture_layers, 1);
   const unsigned level = view->texture_level;
   if (first_layer64 >= resource_layers ||
       layers > resource_layers - first_layer64 || level >= mips) {
      mesa_loge("ClearUnorderedAccessView: texture range is out of bounds");
      list->state = D12_LIST_ERROR;
      return;
   }

   enum pipe_format view_format = d12_format_to_pipe(view->format);
   if (view_format == PIPE_FORMAT_NONE ||
       util_format_get_blocksize(view_format) != sizeof(packed) ||
       util_format_get_blockwidth(view_format) != 1 ||
       util_format_get_blockheight(view_format) != 1) {
      mesa_loge("ClearUnorderedAccessView: texture format %u unsupported",
                (unsigned)view->format);
      list->state = D12_LIST_ERROR;
      return;
   }

   uint64_t value = d12_list_upload(list, &packed, sizeof(packed), 4);
   if (!value)
      return;

   for (unsigned layer = 0; layer < layers; layer++) {
      const unsigned subresource =
         level + ((unsigned)first_layer64 + layer) * mips;
      struct d12_copy_surface surface;
      if (!d12_copy_surface_from_texture(
             &surface, res, subresource, "UAV-clear destination") ||
          surface.sample_count != 1 ||
          surface.blocksize_B != sizeof(packed) ||
          surface.block_width_px != 1 || surface.block_height_px != 1) {
         mesa_loge("ClearUnorderedAccessView: texture surface unsupported");
         list->state = D12_LIST_ERROR;
         return;
      }

      const unsigned count = num_rects ? num_rects : 1;
      for (unsigned i = 0; i < count; i++) {
         int64_t left = 0, top = 0;
         int64_t right = surface.width_px, bottom = surface.height_px;
         if (num_rects) {
            left = MAX2((int64_t)rects[i].left, 0);
            top = MAX2((int64_t)rects[i].top, 0);
            right = MIN2((int64_t)rects[i].right,
                         (int64_t)surface.width_px);
            bottom = MIN2((int64_t)rects[i].bottom,
                          (int64_t)surface.height_px);
         }

         if (right <= left || bottom <= top)
            continue;

         const uint32_t width = right - left;
         const uint32_t height = bottom - top;
         struct libagx_fill_image_args args = {
            .dest = surface.base,
            .value = value,
            .width_el = width,
            .height_el = height,
            .dest_x_el = left,
            .dest_y_el = top,
            .dest_pitch_B = surface.pitch_B,
            .dest_stride_el = surface.stride_el,
            .dest_tile_width_el = surface.tile_width_el,
            .dest_tile_height_el = surface.tile_height_el,
            .blocksize_B = surface.blocksize_B,
            .dest_tiled = surface.tiled,
         };
         d12_list_dispatch_precomp(list, LIBAGX_FILL_IMAGE, &args,
                                   sizeof(args),
                                   agx_1d((uint64_t)width * height));
         if (list->state == D12_LIST_ERROR)
            return;
      }
   }
}

static void
d12_list_clear_uav(struct d12_cmd_list *list,
                   D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
                   D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                   ID3D12Resource *resource, uint32_t packed, UINT num_rects,
                   const D3D12_RECT *rects)
{
   struct d12_resource *res =
      resource ? d12_resource_from_iface(resource) : NULL;

   if (res && res->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
      d12_list_clear_uav_texture(list, gpu_handle, cpu_handle, resource,
                                 packed, num_rects, rects);
   } else {
      d12_list_clear_uav_buffer(list, gpu_handle, cpu_handle, resource,
                                packed, num_rects);
   }
}

static void STDMETHODCALLTYPE
d12_list_ClearUnorderedAccessViewUint(
   ID3D12GraphicsCommandList6 *iface, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
   D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
   const UINT values[4], UINT num_rects, const D3D12_RECT *rects)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   struct d12_descriptor *view = (struct d12_descriptor *)cpu_handle.ptr;
   uint32_t packed;

   if (list->state != D12_LIST_RECORDING)
      return;

   if (!values || !view ||
       !d12_pack_clear_uav_uint(view->format, values, &packed)) {
      mesa_loge("ClearUnorderedAccessViewUint: unsupported or invalid view");
      list->state = D12_LIST_ERROR;
      return;
   }

   d12_list_clear_uav(list, gpu_handle, cpu_handle, resource, packed,
                      num_rects, rects);
}

static void STDMETHODCALLTYPE
d12_list_ClearUnorderedAccessViewFloat(
   ID3D12GraphicsCommandList6 *iface, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
   D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource,
   const FLOAT values[4], UINT num_rects, const D3D12_RECT *rects)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   struct d12_descriptor *view = (struct d12_descriptor *)cpu_handle.ptr;
   uint32_t packed;

   if (list->state != D12_LIST_RECORDING)
      return;

   if (!values || !view ||
       !d12_pack_clear_uav_float(view->format, values, &packed)) {
      mesa_loge("ClearUnorderedAccessViewFloat: unsupported or invalid view");
      list->state = D12_LIST_ERROR;
      return;
   }

   d12_list_clear_uav(list, gpu_handle, cpu_handle, resource, packed,
                      num_rects, rects);
}

/*
 * Attach a depth/stencil resource to the open render pass.
 *
 * Depth never goes through a background/end-of-tile USC program the way
 * colour does: the fixed-function ZLS unit loads and stores it, configured
 * from the render command. So attaching is bookkeeping here, and the actual
 * hardware setup happens in d12_fill_render_cmd.
 */
static bool
d12_list_attach_ds(struct d12_cmd_list *list,
                   const struct d12_descriptor *dsv)
{
   struct d12_resource *ds = dsv->resource;
   if (!list->render_open) {
      mesa_loge("depth/stencil needs an open render pass; clear the colour "
                "target first (depth-only passes are not supported yet)");
      list->state = D12_LIST_ERROR;
      return false;
   }

   struct d12_segment *seg = &list->segments[list->segment_count - 1];

   if (seg->ds && seg->ds != ds) {
      mesa_loge("two different depth/stencil targets in one render pass");
      list->state = D12_LIST_ERROR;
      return false;
   }

   /* The ZLS unit addresses one buffer for the whole pass. A depth buffer
    * smaller than the colour target would leave the uncovered region
    * testing against memory outside the allocation.
    */
   if (ds->layout.width_px < seg->width_px ||
       ds->layout.height_px < seg->height_px) {
      mesa_loge("depth/stencil target %ux%u is smaller than the render pass "
                "%ux%u", ds->layout.width_px, ds->layout.height_px,
                seg->width_px, seg->height_px);
      list->state = D12_LIST_ERROR;
      return false;
   }

   seg->ds = ds;
   seg->ds_address = dsv->address;
   seg->stencil_address = dsv->counter_address;
   seg->dsv_flags = dsv->dsv_flags;
   const unsigned ds_layers = MAX2(dsv->size_B, 1);
   if (seg->layers != ds_layers) {
      mesa_loge("render pass and depth/stencil have mismatched layer counts "
                "%u and %u", seg->layers, ds_layers);
      list->state = D12_LIST_ERROR;
      return false;
   }
   return true;
}

static bool
d12_list_clear_surface_rects(struct d12_cmd_list *list,
                             const struct d12_copy_surface *surface,
                             const void *packed, UINT num_rects,
                             const D3D12_RECT *rects, const char *name)
{
   if (!rects || surface->block_width_px != 1 ||
       surface->block_height_px != 1 || surface->blocksize_B > 16) {
      mesa_loge("%s: invalid rectangle or surface format", name);
      list->state = D12_LIST_ERROR;
      return false;
   }

   if (!surface->sample_count ||
       surface->blocksize_B % surface->sample_count != 0) {
      mesa_loge("%s: invalid multisample block shape", name);
      list->state = D12_LIST_ERROR;
      return false;
   }

   const unsigned sample_size_B =
      surface->blocksize_B / surface->sample_count;
   uint8_t value_pattern[16] = {0};
   if (sample_size_B > sizeof(value_pattern)) {
      mesa_loge("%s: sample value is too large", name);
      list->state = D12_LIST_ERROR;
      return false;
   }
   for (unsigned s = 0; s < surface->sample_count; ++s)
      memcpy(value_pattern + s * sample_size_B, packed, sample_size_B);

   uint64_t value = d12_list_upload(list, value_pattern,
                                    surface->blocksize_B, 16);
   if (!value)
      return false;

   for (unsigned i = 0; i < num_rects; ++i) {
      const int64_t left = MAX2((int64_t)rects[i].left, 0);
      const int64_t top = MAX2((int64_t)rects[i].top, 0);
      const int64_t right =
         MIN2((int64_t)rects[i].right, (int64_t)surface->width_px);
      const int64_t bottom =
         MIN2((int64_t)rects[i].bottom, (int64_t)surface->height_px);
      if (right <= left || bottom <= top)
         continue;

      const uint32_t width = right - left;
      const uint32_t height = bottom - top;
      struct libagx_fill_image_args args = {
         .dest = surface->base,
         .value = value,
         .width_el = width,
         .height_el = height,
         .dest_x_el = left,
         .dest_y_el = top,
         .dest_pitch_B = surface->pitch_B,
         .dest_stride_el = surface->stride_el,
         .dest_tile_width_el = surface->tile_width_el,
         .dest_tile_height_el = surface->tile_height_el,
         .blocksize_B = surface->blocksize_B,
         .dest_tiled = surface->tiled,
      };
      d12_list_dispatch_precomp(list, LIBAGX_FILL_IMAGE, &args, sizeof(args),
                                agx_1d((uint64_t)width * height));
      if (list->state == D12_LIST_ERROR)
         return false;
   }

   return true;
}

static void STDMETHODCALLTYPE
d12_list_ClearDepthStencilView(ID3D12GraphicsCommandList6 *iface,
                               D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle,
                               D3D12_CLEAR_FLAGS flags, FLOAT depth,
                               UINT8 stencil, UINT num_rects,
                               const D3D12_RECT *rects)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING)
      return;

   struct d12_rtv *dsv = (struct d12_rtv *)dsv_handle.ptr;
   if (!dsv || !dsv->resource || dsv->kind != D12_DESC_DSV) {
      mesa_loge("ClearDepthStencilView: invalid DSV handle");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_resource *ds = dsv->resource;

   if (num_rects != 0) {
      if (flags & D3D12_CLEAR_FLAG_DEPTH) {
         struct d12_copy_surface surface;
         if (!d12_copy_surface_from_texture(&surface, ds, 0,
                                            "partial depth clear")) {
            list->state = D12_LIST_ERROR;
            return;
         }

         uint32_t packed = ds->layout.format == PIPE_FORMAT_Z16_UNORM
                              ? _mesa_float_to_unorm(depth, 16)
                              : fui(depth);
         for (unsigned layer = 0; layer < MAX2(dsv->size_B, 1); ++layer) {
            surface.base = dsv->address +
               (uint64_t)layer * ds->layout.layer_stride_B;
            surface.storage_offset_B =
               surface.base - surface.bo->va->addr;
            if (!d12_copy_surface_check_storage(&surface,
                                                "partial depth clear") ||
                !d12_list_clear_surface_rects(list, &surface, &packed,
                                              num_rects, rects,
                                              "depth clear"))
               return;
         }
      }

      if (flags & D3D12_CLEAR_FLAG_STENCIL) {
         if (!ds->stencil_bo) {
            mesa_loge("ClearDepthStencilView: stencil clear on a format "
                      "with no stencil plane");
            list->state = D12_LIST_ERROR;
            return;
         }

         struct d12_copy_surface surface;
         const unsigned plane_subresource =
            MAX2(ds->desc.MipLevels, 1) *
            MAX2(ds->desc.DepthOrArraySize, 1);
         if (!d12_copy_surface_from_texture(&surface, ds,
                                            plane_subresource,
                                            "partial stencil clear")) {
            list->state = D12_LIST_ERROR;
            return;
         }
         for (unsigned layer = 0; layer < MAX2(dsv->size_B, 1); ++layer) {
            surface.base = dsv->counter_address +
               (uint64_t)layer * ds->stencil_layout.layer_stride_B;
            surface.storage_offset_B =
               surface.base - surface.bo->va->addr;
            if (!d12_copy_surface_check_storage(&surface,
                                                "partial stencil clear") ||
                !d12_list_clear_surface_rects(list, &surface, &stencil,
                                              num_rects, rects,
                                              "stencil clear"))
               return;
         }
      }
      return;
   }

   if (list->render_open) {
      struct d12_segment *open =
         &list->segments[list->segment_count - 1];
      /* ZLS clear/load state is consumed at pass start. Mutating it after a
       * draw would reorder Draw->Clear into Clear->Draw. A clear view is
       * independent of current OM bindings, so a different slice/range (or
       * an incompatible colour-pass layer count) also needs its own pass.
       */
      if (open->has_draws || open->layers != MAX2(dsv->size_B, 1) ||
          (open->ds &&
           (open->ds != ds || open->ds_address != dsv->address ||
            open->stencil_address != dsv->counter_address ||
            open->dsv_flags != dsv->dsv_flags)))
         d12_list_close_cdm(list);
   }

   if (!list->render_open) {
      if (!d12_list_begin_render(list, NULL, 0, dsv, NULL,
                                 AGX_BG_EOT_NONE, NULL, true))
         return;
   } else if (!d12_list_attach_ds(list, dsv)) {
      return;
   }

   struct d12_segment *seg = &list->segments[list->segment_count - 1];
   seg->process_empty_tiles = true;

   /* The clear is not a draw and not a program: the hardware seeds every
    * tile's depth from ISP_BGOBJDEPTH, encoded in the depth plane's own
    * format, and its stencil from the low byte of ISP_BGOBJVALS.
    */
   if (flags & D3D12_CLEAR_FLAG_DEPTH) {
      if (ds->layout.format == PIPE_FORMAT_Z16_UNORM)
         seg->isp_bgobjdepth = _mesa_float_to_unorm(depth, 16);
      else
         seg->isp_bgobjdepth = fui(depth);

      seg->depth_cleared = true;
   }

   if (flags & D3D12_CLEAR_FLAG_STENCIL) {
      if (!ds->stencil_bo) {
         mesa_loge("ClearDepthStencilView: stencil clear on a format with no "
                   "stencil plane");
         list->state = D12_LIST_ERROR;
         return;
      }

      seg->stencil_clear = stencil;
      seg->stencil_cleared = true;
   }
}

static void STDMETHODCALLTYPE
d12_list_OMSetStencilRef(ID3D12GraphicsCommandList6 *iface, UINT ref)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   list->stencil_ref = (uint8_t)ref;
}

static void STDMETHODCALLTYPE
d12_list_OMSetBlendFactor(ID3D12GraphicsCommandList6 *iface,
                          const FLOAT factor[4])
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   static const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
   memcpy(list->blend_factor, factor ? factor : one,
          sizeof(list->blend_factor));
}

static void STDMETHODCALLTYPE
d12_list_OMSetRenderTargets(ID3D12GraphicsCommandList6 *iface, UINT num_rtvs,
                            const D3D12_CPU_DESCRIPTOR_HANDLE *rtvs,
                            BOOL single_handle,
                            const D3D12_CPU_DESCRIPTOR_HANDLE *dsv)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING)
      return;

   memset(list->bound_rtv, 0, sizeof(list->bound_rtv));
   list->bound_rtv_count = 0;
   if (num_rtvs > AGX_MAX_RENDER_TARGETS || (num_rtvs && !rtvs)) {
      mesa_loge("OMSetRenderTargets: invalid RTV count %u", num_rtvs);
      list->state = D12_LIST_ERROR;
      return;
   }

   for (unsigned i = 0; i < num_rtvs; i++) {
      uintptr_t handle = single_handle
                            ? rtvs[0].ptr +
                                 i * sizeof(struct d12_descriptor)
                            : rtvs[i].ptr;
      const struct d12_rtv *rtv = (const struct d12_rtv *)handle;
      if (!rtv || !rtv->resource)
         continue;

      if (rtv->kind != D12_DESC_RTV) {
         mesa_loge("OMSetRenderTargets: handle %u is not a render-target "
                   "view", i);
         list->state = D12_LIST_ERROR;
         return;
      }

      list->bound_rtv[i] = *rtv;
   }
   list->bound_rtv_count = num_rtvs;

   memset(&list->bound_dsv, 0, sizeof(list->bound_dsv));
   list->has_bound_dsv = false;
   if (dsv) {
      struct d12_rtv *view = (struct d12_rtv *)dsv->ptr;
      if (view && view->resource && view->kind == D12_DESC_DSV) {
         list->bound_dsv = *view;
         list->has_bound_dsv = true;
      } else if (view && view->resource) {
         mesa_loge("OMSetRenderTargets: DSV handle is not a depth/stencil "
                   "view");
         list->state = D12_LIST_ERROR;
         return;
      }
   }

   /* Ending here makes attachment changes authoritative even if no draw
    * follows. The next draw opens a pass with the new descriptor snapshot.
    */
   if (list->render_open &&
       !d12_segment_matches_bindings(
          list, &list->segments[list->segment_count - 1], true))
      d12_list_close_cdm(list);
}

static void STDMETHODCALLTYPE
d12_list_SOSetTargets(ID3D12GraphicsCommandList6 *iface, UINT start_slot,
                      UINT num_views,
                      const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING)
      return;
   if (start_slot > ARRAY_SIZE(list->so) ||
       num_views > ARRAY_SIZE(list->so) - start_slot) {
      mesa_loge("SOSetTargets: slot range %u+%u exceeds four buffers",
                start_slot, num_views);
      list->state = D12_LIST_ERROR;
      return;
   }

   for (unsigned i = 0; i < num_views; i++) {
      D3D12_STREAM_OUTPUT_BUFFER_VIEW view = views ? views[i] :
                                                    (D3D12_STREAM_OUTPUT_BUFFER_VIEW){0};
      if ((view.BufferLocation || view.SizeInBytes ||
           view.BufferFilledSizeLocation) &&
          (!view.BufferLocation || !view.SizeInBytes ||
           !view.BufferFilledSizeLocation || view.SizeInBytes > UINT32_MAX)) {
         mesa_loge("SOSetTargets: incomplete or oversized buffer view %u", i);
         list->state = D12_LIST_ERROR;
         return;
      }
      list->so[start_slot + i] = view;
   }
}

/* Same widening as the device: ID3D12GraphicsCommandList6 is a superset,
 * and DispatchMesh lives on it.
 */
static const ID3D12GraphicsCommandList6Vtbl d12_list_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12GraphicsCommandList6_DEFAULTS,
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
   .WriteBufferImmediate = d12_list_WriteBufferImmediate,
   .CopyResource = d12_list_CopyResource,
   .CopyTextureRegion = d12_list_CopyTextureRegion,
   .ResolveSubresource = d12_list_ResolveSubresource,
   .ResolveSubresourceRegion = d12_list_ResolveSubresourceRegion,
   .ResourceBarrier = d12_list_ResourceBarrier,
   .OMSetRenderTargets = d12_list_OMSetRenderTargets,
   .SOSetTargets = d12_list_SOSetTargets,
   .OMSetStencilRef = d12_list_OMSetStencilRef,
   .OMSetBlendFactor = d12_list_OMSetBlendFactor,
   .ClearRenderTargetView = d12_list_ClearRenderTargetView,
   .ClearDepthStencilView = d12_list_ClearDepthStencilView,
   .ClearUnorderedAccessViewUint = d12_list_ClearUnorderedAccessViewUint,
   .ClearUnorderedAccessViewFloat = d12_list_ClearUnorderedAccessViewFloat,
   .ClearState = d12_list_ClearState,
   .SetPipelineState = d12_list_SetPipelineState,
   .SetComputeRootSignature = d12_list_SetComputeRootSignature,
   .SetComputeRootUnorderedAccessView =
      d12_list_SetComputeRootUnorderedAccessView,
   .SetGraphicsRootUnorderedAccessView =
      d12_list_SetGraphicsRootUnorderedAccessView,
   .SetComputeRootShaderResourceView =
      d12_list_SetComputeRootShaderResourceView,
   .SetGraphicsRootShaderResourceView =
      d12_list_SetGraphicsRootShaderResourceView,
   .SetComputeRootConstantBufferView =
      d12_list_SetComputeRootConstantBufferView,
   .SetGraphicsRoot32BitConstants = d12_list_SetGraphicsRoot32BitConstants,
   .SetComputeRoot32BitConstants = d12_list_SetComputeRoot32BitConstants,
   .SetGraphicsRoot32BitConstant = d12_list_SetGraphicsRoot32BitConstant,
   .SetComputeRoot32BitConstant = d12_list_SetComputeRoot32BitConstant,
   .SetGraphicsRootConstantBufferView =
      d12_list_SetGraphicsRootConstantBufferView,
   .SetComputeRootDescriptorTable = d12_list_SetComputeRootDescriptorTable,
   .SetGraphicsRootDescriptorTable = d12_list_SetGraphicsRootDescriptorTable,
   .SetDescriptorHeaps = d12_list_SetDescriptorHeaps,
   .Dispatch = d12_list_Dispatch,
   .SetGraphicsRootSignature = d12_list_SetGraphicsRootSignature,
   .IASetPrimitiveTopology = d12_list_IASetPrimitiveTopology,
   .IASetVertexBuffers = d12_list_IASetVertexBuffers,
   .RSSetViewports = d12_list_RSSetViewports,
   .RSSetScissorRects = d12_list_RSSetScissorRects,
   .DrawInstanced = d12_list_DrawInstanced,
   .DrawIndexedInstanced = d12_list_DrawIndexedInstanced,
   .ExecuteIndirect = d12_list_ExecuteIndirect,
   .IASetIndexBuffer = d12_list_IASetIndexBuffer,
   .BeginQuery = d12_list_BeginQuery,
   .EndQuery = d12_list_EndQuery,
   .ResolveQueryData = d12_list_ResolveQueryData,
   .DispatchMesh = d12_list_DispatchMesh,
   /* Everything unset is NULL: loud crash over silent lie. */
};

HRESULT
d12_cmd_list_create(struct d12_device *device, D3D12_COMMAND_LIST_TYPE type,
                    struct d12_cmd_allocator *allocator, REFIID riid,
                    void **out)
{
   if (!out)
      return E_POINTER;

   switch (type) {
   case D3D12_COMMAND_LIST_TYPE_DIRECT:
   case D3D12_COMMAND_LIST_TYPE_BUNDLE:
   case D3D12_COMMAND_LIST_TYPE_COMPUTE:
   case D3D12_COMMAND_LIST_TYPE_COPY:
      break;
   default:
      return E_INVALIDARG;
   }

   if (!allocator || allocator->type != type || allocator->recording_lists)
      return E_INVALIDARG;

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
   list->allocator = allocator;
   d12_private_data_store_init(&list->private_data);
   util_dynarray_init(&list->descriptor_fixups, NULL);
   util_dynarray_init(&list->indirect_constant_fixups, NULL);
   util_dynarray_init(&list->indirect_argument_fixups, NULL);
   if (allocator)
      allocator->recording_lists++;
   d12_device_add_ref(device);

   /* D3D12 command lists are created in the recording state. */
   d12_list_begin_recording(list);

   HRESULT hr = d12_list_QueryInterface((ID3D12GraphicsCommandList6 *)&list->iface, riid, out);
   d12_list_Release((ID3D12GraphicsCommandList6 *)&list->iface);
   return hr;
}
