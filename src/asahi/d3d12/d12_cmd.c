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
#include "agx_usc.h"
#include "libagx_dgc.h"
#include "libagx_shaders.h"
#include "util/format/format_utils.h"
#include "util/format/u_format.h"
#include "util/u_math.h"

#define D12_STREAM_BO_SIZE (1024 * 1024)
#define D12_USC_BO_SIZE    (256 * 1024)
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

static ULONG STDMETHODCALLTYPE
d12_alloc_Release(ID3D12CommandAllocator *iface)
{
   struct d12_cmd_allocator *alloc = d12_cmd_allocator_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&alloc->refcount);

   if (refs == 0) {
      d12_device_release(alloc->device);
      free(alloc);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_GetPrivateData(ID3D12CommandAllocator *iface, REFGUID guid,
                         UINT *size, void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_SetPrivateData(ID3D12CommandAllocator *iface, REFGUID guid,
                         UINT size, const void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_SetPrivateDataInterface(ID3D12CommandAllocator *iface, REFGUID guid,
                                  const IUnknown *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_alloc_SetName(ID3D12CommandAllocator *iface, LPCWSTR name)
{
   return S_OK;
}

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

   switch (type) {
   case D3D12_COMMAND_LIST_TYPE_DIRECT:
   case D3D12_COMMAND_LIST_TYPE_COMPUTE:
   case D3D12_COMMAND_LIST_TYPE_COPY:
      break;
   default:
      mesa_loge("CreateCommandAllocator: type %d not supported", (int)type);
      return E_NOTIMPL;
   }

   struct d12_cmd_allocator *alloc = calloc(1, sizeof(*alloc));
   if (!alloc)
      return E_OUTOFMEMORY;

   alloc->iface.lpVtbl = (ID3D12CommandAllocatorVtbl *)&d12_alloc_vtbl;
   alloc->refcount = 1;
   alloc->device = device;
   alloc->type = type;
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

static void
d12_list_begin_recording(struct d12_cmd_list *list)
{
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
   list->state = D12_LIST_RECORDING;

   /* Bindings do not survive a reset: a stale render target or depth buffer
    * here would let the next recording draw into last frame's texture.
    */
   list->bound_rt = NULL;
   list->bound_ds = NULL;
   list->stencil_ref = 0;
   list->compute_rs = NULL;
   list->graphics_rs = NULL;
   memset(list->compute_root_table, 0, sizeof(list->compute_root_table));
   memset(list->graphics_root_table, 0, sizeof(list->graphics_root_table));
}

/* Upload a data blob into the top of the stream BO; returns its GPU VA. */
static uint64_t
d12_list_upload(struct d12_cmd_list *list, const void *data, size_t size,
                size_t align)
{
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

/* CPU pointer for a GPU address previously returned by an upload. */
static uint8_t *
d12_list_data_cpu(struct d12_cmd_list *list, uint64_t gpu)
{
   if (!gpu)
      return NULL;
   return d12_list_stream_map(list) + (gpu - list->stream_bo->va->addr);
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
d12_list_dispatch_precomp(struct d12_cmd_list *list, enum libagx_program idx,
                          const void *args, size_t args_size,
                          struct agx_grid grid)
{
   struct d12_device *device = list->device;

   if (list->state != D12_LIST_RECORDING) {
      mesa_loge("recording into a non-recording command list");
      return;
   }

   struct d12_segment *seg = d12_list_get_cdm(list);
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

   if (!d12_list_check_stream_space(list))
      return;

   list->stream_cur = (uint8_t *)agx_cdm_launch(
      (uint32_t *)list->stream_cur, device->dev.chip, grid, prog->b.workgroup,
      prog->b.launch, usc);
   list->stream_cur =
      (uint8_t *)agx_cdm_barrier((uint32_t *)list->stream_cur,
                                 device->dev.chip);
   seg->dispatches++;
}

/*
 * Build the USC words for a background (clear) or end-of-tile (store)
 * program targeting one RGBA8 linear render target. Mirrors
 * hk_build_bg_eot for the single-RT, non-layered, single-sampled case.
 */
static uint32_t
d12_build_bg_eot_usc(struct d12_cmd_list *list, struct d12_resource *rt,
                     const float color[4], bool store,
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
   unsigned uniforms = 0, nr_tex = 0;

   if (!store) {
      key.op[0] = AGX_BG_CLEAR;

      /* 16 bytes of clear color, register base 4 — hk's fixed ABI */
      uint64_t colour = d12_list_upload(list, color, 16, 16);
      if (!colour)
         return 0;

      agx_usc_uniform(&b, 4, 8, colour);
      uniforms = 4 + 8;
   } else {
      key.op[0] = AGX_EOT_STORE;

      /* PBE descriptor for the linear RGBA8 target, following the gallium
       * driver's linear-2D packing (agx_state.c).
       */
      struct agx_pbe_packed desc;
      agx_pack(&desc, PBE, cfg) {
         cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
         cfg.layout = agx_translate_layout(AIL_TILING_LINEAR);
         enum pipe_format pf = rt->layout.format;
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
            case PIPE_SWIZZLE_X: cfg.swizzle_r = c; break;
            case PIPE_SWIZZLE_Y: cfg.swizzle_g = c; break;
            case PIPE_SWIZZLE_Z: cfg.swizzle_b = c; break;
            case PIPE_SWIZZLE_W: cfg.swizzle_a = c; break;
            default: break;
            }
         }
         cfg.buffer = rt->bo->va->addr;
         cfg.width = rt->layout.width_px;
         cfg.height = rt->layout.height_px;
         cfg.level = 0;
         cfg.layers = 1;
         cfg.levels = 1;
         cfg.stride = ail_get_linear_stride_B(&rt->layout, 0) - 4;
      }

      uint64_t desc_ptr = d12_list_upload(list, &desc, sizeof(desc), 8);
      if (!desc_ptr)
         return 0;

      agx_usc_pack(&b, TEXTURE, cfg) {
         cfg.start = 0;
         cfg.count = 1;
         cfg.buffer = desc_ptr;
      }
      nr_tex = 1;
   }

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
         agx_translate_sampler_state_count(0, false);
   }

   return usc;
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
                         uint64_t *out_root, uint64_t *out_textures)
{
   const struct d12_descriptor *table =
      compute ? list->compute_root_table : list->graphics_root_table;

   /* Two arrays back to back (D12_ROOT_COUNTER_BASE): resource base
    * addresses, then UAV counter addresses for bufferUpdateCounter.
    */
   uint64_t addrs[2 * D12_MAX_RESOURCE_BINDINGS];
   for (unsigned i = 0; i < D12_MAX_RESOURCE_BINDINGS; i++) {
      addrs[i] = table[i].address;
      addrs[D12_ROOT_COUNTER_BASE + i] = table[i].counter_address;
   }

   uint64_t addrs_gpu = d12_list_upload(list, addrs, sizeof(addrs), 8);
   if (!addrs_gpu)
      return false;

   *out_root = d12_list_upload(list, &addrs_gpu, sizeof(addrs_gpu), 8);
   if (!*out_root)
      return false;

   unsigned last_texture = 0;
   for (unsigned i = 0; i < D12_MAX_RESOURCE_BINDINGS; i++) {
      if (table[i].resource && table[i].resource->desc.Dimension ==
                                  D3D12_RESOURCE_DIMENSION_TEXTURE2D)
         last_texture = i + 1;
   }

   *out_textures = 0;
   if (!last_texture)
      return true;

   static_assert(AGX_TEXTURE_LENGTH == D12_TEXTURE_DESC_SIZE,
                 "the front end's descriptor stride must match hardware");

   uint64_t heap_gpu = d12_list_upload_zero(
      list, (size_t)last_texture * AGX_TEXTURE_LENGTH, 64);
   if (!heap_gpu)
      return false;

   struct agx_texture_packed *descs =
      (struct agx_texture_packed *)d12_list_data_cpu(list, heap_gpu);

   for (unsigned i = 0; i < last_texture; i++) {
      struct d12_resource *res = table[i].resource;
      if (!res || res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
         continue;

      const struct ail_layout *layout = &res->layout;
      enum pipe_format pf = layout->format;
      const struct util_format_description *fdesc =
         util_format_description(pf);

      agx_pack(&descs[i], TEXTURE, cfg) {
         cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
         cfg.layout = agx_translate_layout(layout->tiling);
         cfg.channels = ail_pixel_format[pf].channels;
         cfg.type = ail_pixel_format[pf].type;
         cfg.srgb = util_format_is_srgb(pf);

         cfg.swizzle_r = agx_channel_from_pipe(fdesc->swizzle[0]);
         cfg.swizzle_g = agx_channel_from_pipe(fdesc->swizzle[1]);
         cfg.swizzle_b = agx_channel_from_pipe(fdesc->swizzle[2]);
         cfg.swizzle_a = agx_channel_from_pipe(fdesc->swizzle[3]);

         cfg.address = res->bo->va->addr;
         cfg.width = layout->width_px;
         cfg.height = layout->height_px;
         cfg.first_level = 0;
         cfg.last_level = 0;
         cfg.unk_mipmapped = layout->levels > 1;

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
         } else {
            cfg.depth = 1;
            cfg.page_aligned_layers = layout->page_aligned_layers;
         }
      }
   }

   if (getenv("D3D12AGX_DUMP_TEX")) {
      for (unsigned i = 0; i < last_texture; i++) {
         struct d12_resource *res = table[i].resource;
         if (!res)
            continue;
         const uint32_t *px = (const uint32_t *)agx_bo_map(res->bo);
         fprintf(stderr,
                 "tex[%u] va=0x%llx tiling=%u %ux%u stride=%u size=%llu "
                 "px[0]=0x%08x px[1]=0x%08x px[16]=0x%08x\n",
                 i, (unsigned long long)res->bo->va->addr, res->layout.tiling,
                 res->layout.width_px, res->layout.height_px,
                 ail_get_linear_stride_B(&res->layout, 0),
                 (unsigned long long)res->layout.size_B, px[0], px[1], px[16]);
         const uint8_t *d = (const uint8_t *)&descs[i];
         fprintf(stderr, "  desc:");
         for (unsigned b = 0; b < AGX_TEXTURE_LENGTH; b++)
            fprintf(stderr, " %02x", d[b]);
         fprintf(stderr, "\n");
      }
   }

   /* The heap needs a home for the same reason the root table does. */
   *out_textures = d12_list_upload(list, &heap_gpu, sizeof(heap_gpu), 8);

   if (getenv("D3D12AGX_DUMP_TEX") && *out_textures) {
      uint64_t home = *(uint64_t *)d12_list_data_cpu(list, *out_textures);
      fprintf(stderr,
              "chain: textures_gpu=0x%llx -> home holds 0x%llx "
              "(heap_gpu=0x%llx) %s\n",
              (unsigned long long)*out_textures, (unsigned long long)home,
              (unsigned long long)heap_gpu,
              home == heap_gpu ? "MATCH" : "MISMATCH");
   }

   return *out_textures != 0;
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
   uint64_t vertex_params;
   uint64_t geom_params;
   /* Address of the VS output MASK, not the mask itself: agx_usc_uniform
    * fills registers FROM memory, and load_vs_outputs_poly reads a 64-bit
    * value. The geometry body computes every vertex address in the output
    * buffer from this mask, so a wrong one is not a wrong value, it is a
    * wrong POINTER.
    */
   uint64_t vs_outputs;
};

/* Build the USC words for one compute launch; returns the USC offset, or
 * 0 on failure.
 */
static uint32_t
d12_list_build_compute_usc(struct d12_cmd_list *list, uint64_t code_va,
                           const struct agx_shader_info *info,
                           const struct d12_dispatch_uniforms *u,
                           uint8_t **out_cpu)
{
   struct d12_device *device = list->device;

   uint32_t usc;
   size_t usc_size = agx_usc_size(10);
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
   if (u->vertex_params) {
      agx_usc_uniform(&b, D12_UNIFORM_VERTEX_PARAMS, D12_UNIFORM_PARAM_REGS,
                      u->vertex_params);
   }
   if (u->geom_params) {
      agx_usc_uniform(&b, D12_UNIFORM_GEOM_PARAMS, D12_UNIFORM_PARAM_REGS,
                      u->geom_params);
   }
   if (u->vs_outputs) {
      agx_usc_uniform(&b, D12_UNIFORM_VS_OUTPUTS, D12_UNIFORM_PARAM_REGS,
                      u->vs_outputs);
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

   /* txf reads sampler state 0 even though HLSL's Load takes no sampler. */
   if (info->uses_txf)
      agx_usc_push_packed(&b, SAMPLER, device->dev.txf_sampler);

   agx_usc_shared_none(&b);

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

static void
d12_list_dispatch_pso(struct d12_cmd_list *list, unsigned x, unsigned y,
                      unsigned z)
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

   uint64_t root_gpu = 0, textures_gpu = 0;
   if (!d12_list_upload_bindings(list, true, &root_gpu, &textures_gpu))
      return;

   uint8_t *usc_cpu = NULL;
   uint32_t usc = d12_list_build_compute_usc(
      list, pso->code_va, &pso->info,
      &(struct d12_dispatch_uniforms){.root = root_gpu,
                                      .textures = textures_gpu},
      &usc_cpu);
   if (!usc)
      return;

   if (getenv("D3D12AGX_DUMP_USC")) {
      fprintf(stderr, "pso USC @%u:", usc);
      for (unsigned i = 0; i < 32; i++)
         fprintf(stderr, " %02x", usc_cpu[i]);
      fprintf(stderr, "\n  root_gpu=0x%llx textures_gpu=0x%llx\n",
              (unsigned long long)root_gpu,
              (unsigned long long)textures_gpu);
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

   /* D3D12 Dispatch counts thread GROUPS; AGX launches want global
    * threads, exactly as hk multiplies before launching.
    */
   struct agx_grid grid = agx_3d(x * wg.x, y * wg.y, z * wg.z);

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
                  uint64_t root_gpu, uint64_t textures_gpu,
                  uint64_t draw_gpu,
                  const struct d12_dispatch_uniforms *poly)
{
   struct d12_device *device = list->device;

   uint32_t usc;
   uint8_t *cpu = d12_list_alloc_usc(list, agx_usc_size(11), &usc);
   if (!cpu)
      return 0;

   struct agx_usc_builder b = agx_usc_builder(cpu, agx_usc_size(11));

   agx_usc_uniform(&b, D12_UNIFORM_ROOT, D12_UNIFORM_ROOT_REGS, root_gpu);
   if (textures_gpu) {
      agx_usc_uniform(&b, D12_UNIFORM_TEXTURES, D12_UNIFORM_TEXTURE_REGS,
                      textures_gpu);
   }

   /*
    * Constants the compiler promoted out of the instruction stream live in
    * the shader BO's rodata, and the shader reads them as uniforms above
    * its push range. Nothing else publishes them: skip this and those
    * uniforms hold whatever the last dispatch left there, so a shader
    * computes with stale numbers instead of its own constants.
    */
   if (st->info.rodata.size_16) {
      agx_usc_immediates(&b, &st->info.rodata, st->linked.bo->va->addr);
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
   if (poly) {
      if (poly->vertex_params) {
         agx_usc_uniform(&b, D12_UNIFORM_VERTEX_PARAMS,
                         D12_UNIFORM_PARAM_REGS, poly->vertex_params);
      }
      if (poly->geom_params) {
         agx_usc_uniform(&b, D12_UNIFORM_GEOM_PARAMS, D12_UNIFORM_PARAM_REGS,
                         poly->geom_params);
      }
      if (poly->vs_outputs) {
         agx_usc_uniform(&b, D12_UNIFORM_VS_OUTPUTS, D12_UNIFORM_PARAM_REGS,
                         poly->vs_outputs);
      }
   }

   if (draw_gpu) {
      agx_usc_uniform(&b, D12_UNIFORM_DRAW, D12_UNIFORM_DRAW_REGS,
                      draw_gpu);
   }

   /* txf reads sampler state 0 even though HLSL's Load takes no sampler. */
   if (st->info.uses_txf)
      agx_usc_push_packed(&b, SAMPLER, device->dev.txf_sampler);

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
      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code = agx_usc_addr(&device->dev, st->linked.bo->va->addr +
                                                  st->info.preamble_offset);
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
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
                     const struct d12_depth_stencil_state *ds, bool enabled)
{
   agx_ppp_push(ppp, FRAGMENT_STENCIL, cfg) {
      if (enabled) {
         cfg.compare = face->compare;
         cfg.read_mask = ds->stencil_read_mask;
         cfg.write_mask = ds->stencil_write_mask;
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

/*
 * Run a geometry shader's compute half and report what the rasterization
 * draw becomes.
 *
 * Only the direct, non-XFB path: the count, prefix-sum and pre-GS
 * programs exist for transform feedback and GS statistics queries, and hk
 * skips all three when neither is active (hk_cmd_draw.c:1535). PSO
 * creation already refused anything that needs them, so what is left is
 * two dispatches and a rewritten draw.
 *
 * The dispatches go in the PRE-GRAPHICS stream so they land before the
 * open render pass rather than ending it.
 */
static bool
d12_list_gs_prerast(struct d12_cmd_list *list, struct d12_pipeline_state *pso,
                    unsigned vertex_count, unsigned instance_count,
                    uint64_t root_gpu, uint64_t textures_gpu,
                    unsigned *out_verts, unsigned *out_instances,
                    struct d12_dispatch_uniforms *out_poly)
{
   struct d12_device *device = list->device;

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
   struct poly_vertex_params vp;
   poly_vertex_params_init(&vp, pso->sw_vs_outputs, wg_size);
   poly_vertex_params_set_draw(&vp, vertex_count, instance_count);

   size_t out_size = poly_tcs_in_size((uint64_t)vertex_count * instance_count,
                                      pso->sw_vs_outputs);
   vp.output_buffer = d12_list_upload_zero(list, MAX2(out_size, 16), 16);
   if (!vp.output_buffer)
      return false;

   struct poly_geometry_params gp;
   poly_geometry_params_init(&gp, list->topology, wg_size);
   poly_geometry_params_set_draw(&gp, list->topology, pso->gs_info.shape,
                                 pso->gs_info.max_indices, vertex_count,
                                 instance_count);

   /* Both blocks are read through 64-bit constant loads whose offsets the
    * shaders bake, so the alignment is not cosmetic.
    */
   uint64_t vp_gpu = d12_list_upload(list, &vp, sizeof(vp), 8);
   uint64_t gp_gpu = d12_list_upload(list, &gp, sizeof(gp), 8);
   if (!vp_gpu || !gp_gpu)
      return false;

   /* The mask lives in memory because the uniform is filled from memory. */
   uint64_t outputs_mask = pso->sw_vs_outputs;
   uint64_t outputs_gpu =
      d12_list_upload(list, &outputs_mask, sizeof(outputs_mask), 8);
   if (!outputs_gpu)
      return false;

   struct d12_dispatch_uniforms u = {
      .root = root_gpu,
      .textures = textures_gpu,
      .vertex_params = vp_gpu,
      .geom_params = gp_gpu,
      .vs_outputs = outputs_gpu,
   };

   struct d12_segment *seg = d12_list_get_pregfx(list);
   if (!seg)
      return false;

   /* Software VS, then the geometry body. Grids are in THREADS, not
    * workgroups -- unlike the API dispatch path, which multiplies.
    */
   const struct {
      uint64_t va;
      const struct agx_shader_info *info;
      struct agx_grid grid;
   } stages[] = {
      {pso->sw_vs_va, &pso->sw_vs_info, agx_3d(vp.grid[0], vp.grid[1], 1)},
      {pso->gs_main_va, &pso->gs_main_info, agx_3d(gp.grid[0], gp.grid[1], 1)},
   };

   for (unsigned i = 0; i < ARRAY_SIZE(stages); i++) {
      uint32_t usc =
         d12_list_build_compute_usc(list, stages[i].va, stages[i].info, &u,
                                    NULL);
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
         (uint32_t *)list->pregfx_cur, device->dev.chip, stages[i].grid, wg,
         launch, usc);

      /* The geometry body reads what the software VS wrote. */
      list->pregfx_cur = (uint8_t *)agx_cdm_barrier(
         (uint32_t *)list->pregfx_cur, device->dev.chip);
      seg->dispatches++;
   }

   /* The rasterization draw's extent is closed-form in the shape, not a
    * GPU-written count -- that is only needed for indirect draws.
    */
   *out_verts = gp.draw.index_count;
   *out_instances = gp.draw.instance_count;
   *out_poly = u;
   return true;
}

static void
d12_list_draw(struct d12_cmd_list *list, unsigned vertex_count,
              unsigned instance_count, unsigned first_vertex,
              unsigned first_instance)
{
   struct d12_device *device = list->device;
   struct d12_pipeline_state *pso = list->pso;

   if (list->state != D12_LIST_RECORDING)
      return;

   if (!pso || pso->is_compute) {
      mesa_loge("Draw without a graphics pipeline state");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (!list->render_open) {
      mesa_loge("Draw outside a render pass (clear a render target first)");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_segment *seg = &list->segments[list->segment_count - 1];

   /* A pass is opened by ClearRenderTargetView against a specific target.
    * If the application then bound a different one, drawing here would put
    * pixels in the wrong texture and report success -- fail instead.
    */
   if (list->bound_rt && seg->rt && list->bound_rt != seg->rt) {
      mesa_loge("Draw: OMSetRenderTargets bound a different target than the "
                "open render pass; refusing to draw into the wrong texture");
      list->state = D12_LIST_ERROR;
      return;
   }

   /* Root table, exactly as the dispatch path builds it -- from the
    * GRAPHICS bindings, which D3D12 keeps separate from the compute ones.
    */
   uint64_t root_gpu = 0, textures_gpu = 0;
   if (!d12_list_upload_bindings(list, false, &root_gpu, &textures_gpu))
      return;

   /* SM 6.8 draw parameters. agx_usc_uniform fills registers FROM
    * memory, so the values are uploaded and the uniform points at them.
    */
   uint32_t draw_params[2] = {first_vertex, first_instance};
   uint64_t draw_gpu =
      d12_list_upload(list, draw_params, sizeof(draw_params), 8);
   if (!draw_gpu)
      return;

   /* GEOMETRY. The compute half runs first, into the pre-graphics stream,
    * and hands back what the rasterization draw becomes. From here on the
    * "vertex shader" is the GS rasterization program, which reads the
    * emitted vertices rather than vertex attributes.
    */
   enum mesa_prim topology = list->topology;
   struct d12_dispatch_uniforms poly_u = {0};
   const struct d12_gfx_stage *vstage = &pso->vs;
   const struct agx_unlinked_uvs_layout *uvs = &pso->uvs;
   const struct agx_varyings_vs *varys = &pso->varyings;

   if (pso->has_gs) {
      if (!d12_list_gs_prerast(list, pso, vertex_count, instance_count,
                               root_gpu, textures_gpu, &vertex_count,
                               &instance_count, &poly_u))
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
   }

   uint32_t vs_usc =
      d12_gfx_stage_usc(list, vstage, false, &seg->tib, root_gpu,
                        textures_gpu, draw_gpu, pso->has_gs ? &poly_u : NULL);
   uint32_t fs_usc = d12_gfx_stage_usc(list, &pso->fs, true, &seg->tib,
                                       root_gpu, textures_gpu, draw_gpu, NULL);
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
      const bool z_write = has_ds && ds->depth_write;
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
         cfg.visibility_mode = AGX_VISIBILITY_MODE_NONE;
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

      d12_ppp_push_stencil(&ppp, &ds->front, ds, s_test);

      /* Back face: same depth state, its own stencil ops. */
      agx_ppp_push_merged(&ppp, FRAGMENT_FACE, cfg, face) {
         cfg.stencil_reference = list->stencil_ref;
      }
      agx_ppp_push_packed(&ppp, &face_2, FRAGMENT_FACE_2);
      d12_ppp_push_stencil(&ppp, &ds->back, ds, s_test);

      /* Output select is the MERGE of the vertex side (which says the
       * position is written) and the fragment side. Pushing only the
       * fragment half rasterizes nothing.
       */
      struct agx_output_select_packed osel = pso->uvs.osel;
      agx_ppp_push_merged_blobs(&ppp, AGX_OUTPUT_SELECT_LENGTH, &osel,
                                &pso->fs.linked.osel);

      agx_ppp_push_packed(&ppp, &pso->varyings.counts_32, VARYING_COUNTS);
      agx_ppp_push_packed(&ppp, &pso->varyings.counts_16, VARYING_COUNTS);

      agx_ppp_push(&ppp, CULL, cfg) {
         cfg.front_face_ccw = pso->raster.front_face_ccw;
         cfg.cull_front = pso->raster.cull_front;
         cfg.cull_back = pso->raster.cull_back;
         cfg.depth_clip = true;
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

   /* The draw */
   struct agx_draw draw = {
      .b = agx_3d(vertex_count, instance_count, 1),
      .start = first_vertex,
   };

   out = (uint8_t *)agx_vdm_draw((uint32_t *)out, device->dev.chip, draw,
                                 d12_agx_primitive(topology));

   list->stream_cur = out;
   seg->has_draws = true;
}

static void STDMETHODCALLTYPE
d12_list_DrawInstanced(ID3D12GraphicsCommandList *iface,
                       UINT vertex_count_per_instance, UINT instance_count,
                       UINT start_vertex, UINT start_instance)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!vertex_count_per_instance || !instance_count)
      return;

   d12_list_draw(list, vertex_count_per_instance, instance_count,
                 start_vertex, start_instance);
}

static void STDMETHODCALLTYPE
d12_list_IASetPrimitiveTopology(ID3D12GraphicsCommandList *iface,
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
      mesa_loge("IASetPrimitiveTopology: topology %d is not supported",
                (int)topology);
      list->state = D12_LIST_ERROR;
      break;
   }
}


static void STDMETHODCALLTYPE
d12_list_RSSetViewports(ID3D12GraphicsCommandList *iface, UINT count,
                        const D3D12_VIEWPORT *viewports)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (count >= 1 && viewports) {
      list->viewport = viewports[0];
      list->has_viewport = true;
   }
}

static void STDMETHODCALLTYPE
d12_list_RSSetScissorRects(ID3D12GraphicsCommandList *iface, UINT count,
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
d12_list_SetGraphicsRootSignature(ID3D12GraphicsCommandList *iface,
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
   if (rp->type != expected) {
      mesa_loge("root parameter %u is type %u, bound as type %u",
                root_param_index, rp->type, expected);
      list->state = D12_LIST_ERROR;
      return;
   }

   unsigned slot = d12_binding_map_find(&rs->map, d12_root_param_class(rp),
                                        rp->space, rp->base_register);
   if (slot == ~0u) {
      mesa_loge("root parameter %u names register %u in space %u, which the "
                "signature's binding map does not cover",
                root_param_index, rp->base_register, rp->space);
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_descriptor *table =
      compute ? list->compute_root_table : list->graphics_root_table;

   /* A root descriptor is a bare address: no resource behind it, so no
    * hardware texture descriptor can be built from one.
    */
   memset(&table[slot], 0, sizeof(table[slot]));
   table[slot].address = address;
   table[slot].kind = expected == D3D12_ROOT_PARAMETER_TYPE_SRV
                         ? D12_DESC_SRV
                         : (expected == D3D12_ROOT_PARAMETER_TYPE_UAV
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

   /* Our GPU descriptor handles are the CPU addresses of heap slots: the
    * shaders never dereference a descriptor, so no device-visible
    * descriptor memory has to exist for this model.
    */
   const struct d12_descriptor *slots = (const struct d12_descriptor *)base.ptr;
   if (!slots) {
      mesa_loge("descriptor table bound with a null GPU handle");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_descriptor *table =
      compute ? list->compute_root_table : list->graphics_root_table;

   unsigned cursor = 0;
   for (unsigned r = 0; r < rp->num_ranges; r++) {
      const struct d12_root_range *range = &rp->ranges[r];

      unsigned offset = range->offset_in_descriptors;
      if (offset == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
         offset = cursor;

      unsigned klass = d12_descriptor_range_class(range->type);
      if (klass >= D12_RES_CLASS_COUNT) {
         mesa_loge("descriptor range has unknown type %u", range->type);
         list->state = D12_LIST_ERROR;
         return;
      }

      /* An unbounded range is descriptor indexing, which this flattening
       * model cannot express: there is no count to flatten.
       */
      if (range->num_descriptors == UINT32_MAX) {
         mesa_loge("unbounded descriptor range (bindless) unsupported");
         list->state = D12_LIST_ERROR;
         return;
      }

      for (unsigned i = 0; i < range->num_descriptors; i++) {
         unsigned slot = d12_binding_map_find(&rs->map, klass, range->space,
                                              range->base_register + i);
         if (slot == ~0u) {
            mesa_loge("descriptor range reaches a register the signature's "
                      "binding map does not cover");
            list->state = D12_LIST_ERROR;
            return;
         }

         table[slot] = slots[offset + i];
      }

      cursor = offset + range->num_descriptors;
   }
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *iface,
                                        UINT root_param_index,
                                        D3D12_GPU_DESCRIPTOR_HANDLE base)
{
   d12_list_set_root_table(d12_cmd_list_from_iface(iface), false,
                           root_param_index, base);
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *iface,
                                       UINT root_param_index,
                                       D3D12_GPU_DESCRIPTOR_HANDLE base)
{
   d12_list_set_root_table(d12_cmd_list_from_iface(iface), true,
                           root_param_index, base);
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList *iface,
                                           UINT index,
                                           D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), false, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_SRV);
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootShaderResourceView(ID3D12GraphicsCommandList *iface,
                                          UINT index,
                                          D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), true, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_SRV);
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList *iface,
                                           UINT index,
                                           D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), false, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_CBV);
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootConstantBufferView(ID3D12GraphicsCommandList *iface,
                                          UINT index,
                                          D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), true, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_CBV);
}

static void STDMETHODCALLTYPE
d12_list_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList *iface,
                                            UINT index,
                                            D3D12_GPU_VIRTUAL_ADDRESS address)
{
   d12_list_set_root_descriptor(d12_cmd_list_from_iface(iface), false, index,
                                address, D3D12_ROOT_PARAMETER_TYPE_UAV);
}

static void STDMETHODCALLTYPE
d12_list_SetDescriptorHeaps(ID3D12GraphicsCommandList *iface, UINT count,
                            ID3D12DescriptorHeap *const *heaps)
{
   /* Descriptor tables are flattened into the root table at bind time, so
    * nothing depends on which heaps are "current". Recorded as accepted
    * rather than routed through the crash-safe stub, since an application
    * that calls this is not asking for anything we fail to do.
    */
}

static void STDMETHODCALLTYPE
d12_list_SetPipelineState(ID3D12GraphicsCommandList *iface,
                          ID3D12PipelineState *pso)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   list->pso = pso ? d12_pipeline_state_from_iface(pso) : NULL;
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootSignature(ID3D12GraphicsCommandList *iface,
                                 ID3D12RootSignature *root_sig)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   list->compute_rs = (struct d12_root_signature *)root_sig;
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList *iface,
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
d12_list_Dispatch(ID3D12GraphicsCommandList *iface, UINT x, UINT y, UINT z)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!x || !y || !z)
      return;

   d12_list_dispatch_pso(list, x, y, z);
}

static HRESULT STDMETHODCALLTYPE
d12_list_QueryInterface(ID3D12GraphicsCommandList *iface, REFIID riid,
                        void **out)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12CommandList) ||
       d12_guid_equal(riid, &IID_ID3D12GraphicsCommandList)) {
      p_atomic_inc(&list->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12GraphicsCommandList::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_list_AddRef(ID3D12GraphicsCommandList *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return p_atomic_inc_return(&list->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_list_Release(ID3D12GraphicsCommandList *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&list->refcount);

   if (refs == 0) {
      agx_bo_unreference(&list->device->dev, list->stream_bo);
      agx_bo_unreference(&list->device->dev, list->usc_bo);
      d12_device_release(list->device);
      free(list);
   }

   return refs;
}

static HRESULT STDMETHODCALLTYPE
d12_list_GetPrivateData(ID3D12GraphicsCommandList *iface, REFGUID guid,
                        UINT *size, void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_list_SetPrivateData(ID3D12GraphicsCommandList *iface, REFGUID guid,
                        UINT size, const void *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_list_SetPrivateDataInterface(ID3D12GraphicsCommandList *iface,
                                 REFGUID guid, const IUnknown *data)
{
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_list_SetName(ID3D12GraphicsCommandList *iface, LPCWSTR name)
{
   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_list_GetDevice(ID3D12GraphicsCommandList *iface, REFIID riid, void **out)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return ID3D12Device_QueryInterface(&list->device->iface, riid, out);
}

static D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE
d12_list_GetType(ID3D12GraphicsCommandList *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   return list->type;
}

static HRESULT STDMETHODCALLTYPE
d12_list_Close(ID3D12GraphicsCommandList *iface)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state == D12_LIST_ERROR) {
      mesa_loge("Close on errored command list");
      return E_FAIL;
   }
   if (list->state != D12_LIST_RECORDING)
      return E_FAIL;

   d12_list_close_cdm(list);
   list->state = D12_LIST_CLOSED;

   if (list->allocator && list->allocator->recording_lists)
      list->allocator->recording_lists--;

   return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d12_list_Reset(ID3D12GraphicsCommandList *iface,
               ID3D12CommandAllocator *allocator,
               ID3D12PipelineState *initial_state)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (!allocator)
      return E_INVALIDARG;
   if (initial_state)
      list->pso = d12_pipeline_state_from_iface(initial_state);

   /* Re-entering the recording state re-registers with the allocator. */
   if (list->state != D12_LIST_RECORDING) {
      list->allocator = d12_cmd_allocator_from_iface(allocator);
      list->allocator->recording_lists++;
   }

   d12_list_begin_recording(list);
   return S_OK;
}

static void STDMETHODCALLTYPE
d12_list_CopyBufferRegion(ID3D12GraphicsCommandList *iface,
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
      .dest = dstr->bo->va->addr + dst_offset,
      .src = srcr->bo->va->addr + src_offset,
   };

   d12_list_dispatch_precomp(list, LIBAGX_COPY_UCHAR, &args, sizeof(args),
                             agx_1d(num_bytes));
}

static void STDMETHODCALLTYPE
d12_list_CopyResource(ID3D12GraphicsCommandList *iface, ID3D12Resource *dst,
                      ID3D12Resource *src)
{
   struct d12_resource *dstr = d12_resource_from_iface(dst);
   struct d12_resource *srcr = d12_resource_from_iface(src);

   if (!dst || !src)
      return;

   if (dstr->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
       dstr->desc.Width != srcr->desc.Width) {
      mesa_loge("CopyResource: only equal-size buffers for now");
      d12_cmd_list_from_iface(iface)->state = D12_LIST_ERROR;
      return;
   }

   d12_list_CopyBufferRegion(iface, dst, 0, src, 0, dstr->desc.Width);
}

static void STDMETHODCALLTYPE
d12_list_ClearRenderTargetView(ID3D12GraphicsCommandList *iface,
                               D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle,
                               const FLOAT color[4], UINT num_rects,
                               const D3D12_RECT *rects)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING)
      return;

   struct d12_rtv *rtv = (struct d12_rtv *)rtv_handle.ptr;
   if (!rtv || !rtv->resource) {
      mesa_loge("ClearRenderTargetView: invalid RTV handle");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (num_rects != 0) {
      mesa_loge("ClearRenderTargetView: partial-rect clears not until G4");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_resource *rt = rtv->resource;

   d12_list_close_cdm(list);
   if (list->segment_count >= D12_MAX_SEGMENTS) {
      mesa_loge("command list segment limit reached");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_segment *seg = &list->segments[list->segment_count];
   memset(seg, 0, sizeof(*seg));
   seg->type = D12_SEG_RENDER;
   seg->width_px = rt->layout.width_px;
   seg->height_px = rt->layout.height_px;
   seg->rt = rt;

   enum pipe_format formats[AGX_MAX_RENDER_TARGETS] = {rt->layout.format};
   seg->tib = agx_build_tilebuffer_layout(formats, 1, 1, false);

   /* Reserve the pass's scissor array up front: entries have to be
    * contiguous and indexable, and draws fill them in as they come.
    */
   seg->scissor_base = d12_list_upload_zero(
      list, D12_MAX_SCISSORS * AGX_SCISSOR_LENGTH, 64);
   if (!seg->scissor_base)
      return;

   seg->bg_usc =
      d12_build_bg_eot_usc(list, rt, color, false, &seg->tib,
                           &seg->bg_counts);
   seg->eot_usc =
      d12_build_bg_eot_usc(list, rt, NULL, true, &seg->tib,
                           &seg->eot_counts);
   if (!seg->bg_usc || !seg->eot_usc)
      return; /* error state already set */

   /* The VDM stream: cache-invalidate barrier, then terminate. No geometry
    * — the clear happens because PROCESS_EMPTY_TILES makes every tile run
    * background (clear) + end-of-tile (store). Deliberately omits hk's PPP
    * prologue: with zero draws there is no rasterizer state to configure.
    * If hardware disagrees, this is the first place to revisit.
    */
   if (!d12_list_check_stream_space(list))
      return;

   seg->stream_start = list->stream_cur - d12_list_stream_map(list);

   agx_pack(list->stream_cur, VDM_BARRIER, cfg) {
      cfg.usc_cache_inval = true;
   }
   list->stream_cur += AGX_VDM_BARRIER_LENGTH;

   /* PPP prologue. A clear-only pass runs without it, but a pass that
    * carries geometry does not: this is the per-control-stream state
    * hk_cs_init_graphics establishes before any draw.
    */
   {
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
         return;

      struct agx_ppp_update ppp = agx_new_ppp_update(
         (struct agx_ptr){.cpu = cpu, .gpu = gpu}, size, &present);

      agx_ppp_push(&ppp, W_CLAMP, cfg)
         cfg.w_clamp = 1e-10;
      agx_ppp_push(&ppp, FRAGMENT_OCCLUSION_QUERY_2, cfg);
      agx_ppp_push(&ppp, OUTPUT_UNKNOWN, cfg);
      agx_ppp_push(&ppp, VARYING_2, cfg);

      agx_ppp_fini(&list->stream_cur, &ppp);
   }

   /* The pass stays OPEN so draws can append to this VDM stream; it is
    * terminated at Close or when another segment type begins.
    */
   list->segment_count++;
   list->render_open = true;
}

/*
 * Texture <-> buffer copy. This is how an application gets rendered pixels
 * back without knowing our internal layout, and therefore how any
 * independently written test grades our rendering.
 *
 * Both sides are linear, but their row strides differ: our textures use a
 * 64-byte-aligned stride while D3D12 footprints are 256-byte aligned. So the
 * copy runs one GPU dispatch per row. That is not the fastest possible
 * encoding, but it is correct for mismatched pitches and reuses the same
 * bounds-checked kernel as CopyBufferRegion.
 */
static void STDMETHODCALLTYPE
d12_list_CopyTextureRegion(ID3D12GraphicsCommandList *iface,
                           const D3D12_TEXTURE_COPY_LOCATION *dst,
                           UINT dst_x, UINT dst_y, UINT dst_z,
                           const D3D12_TEXTURE_COPY_LOCATION *src,
                           const D3D12_BOX *src_box)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   if (list->state != D12_LIST_RECORDING)
      return;

   if (!dst || !src || !dst->pResource || !src->pResource) {
      mesa_loge("CopyTextureRegion: null location");
      list->state = D12_LIST_ERROR;
      return;
   }

   if (dst_z != 0) {
      mesa_loge("CopyTextureRegion: 3D destination offset unsupported");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_resource *dres = d12_resource_from_iface(dst->pResource);
   struct d12_resource *sres = d12_resource_from_iface(src->pResource);

   /* Which direction? One side must be a placed footprint in a buffer and
    * the other a texture subresource.
    */
   bool tex_to_buf = src->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX &&
                     dst->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
   bool buf_to_tex = src->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT &&
                     dst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

   if (!tex_to_buf && !buf_to_tex) {
      mesa_loge("CopyTextureRegion: only texture<->buffer placed-footprint "
                "copies are supported");
      list->state = D12_LIST_ERROR;
      return;
   }

   const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *fp =
      tex_to_buf ? &dst->PlacedFootprint : &src->PlacedFootprint;
   struct d12_resource *tex = tex_to_buf ? sres : dres;
   struct d12_resource *buf = tex_to_buf ? dres : sres;

   if (tex->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       buf->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
      mesa_loge("CopyTextureRegion: expected a 2D texture and a buffer");
      list->state = D12_LIST_ERROR;
      return;
   }

   /* Region: the source box if given, else the whole footprint. */
   unsigned x0 = 0, y0 = 0;
   unsigned width = fp->Footprint.Width, height = fp->Footprint.Height;
   if (src_box) {
      x0 = src_box->left;
      y0 = src_box->top;
      width = src_box->right - src_box->left;
      height = src_box->bottom - src_box->top;
   }

   if (!width || !height)
      return;

   const unsigned bpp = d12_format_bytes_per_pixel(tex->desc.Format);
   if (!bpp) {
      mesa_loge("CopyTextureRegion: unsupported texture format");
      list->state = D12_LIST_ERROR;
      return;
   }
   const unsigned tex_stride = ail_get_linear_stride_B(&tex->layout, 0);
   const unsigned buf_stride = fp->Footprint.RowPitch;
   const uint64_t row_bytes = (uint64_t)width * bpp;

   uint64_t tex_base = tex->bo->va->addr;
   uint64_t buf_base = buf->bo->va->addr + fp->Offset;

   /* Bounds: refuse rather than scribble outside either allocation. */
   uint64_t tex_need = (uint64_t)(y0 + height - 1) * tex_stride +
                       (uint64_t)(x0 + width) * bpp;
   uint64_t buf_need = (uint64_t)(height - 1) * buf_stride + row_bytes;
   if (tex_need > tex->bo->size || fp->Offset + buf_need > buf->bo->size) {
      mesa_loge("CopyTextureRegion: region exceeds a resource");
      list->state = D12_LIST_ERROR;
      return;
   }

   for (unsigned row = 0; row < height; row++) {
      uint64_t tex_row =
         tex_base + (uint64_t)(y0 + row) * tex_stride + (uint64_t)x0 * bpp;
      uint64_t buf_row = buf_base + (uint64_t)(dst_y + row) * buf_stride +
                         (uint64_t)dst_x * bpp;

      struct libagx_copy_uchar_args args = {
         .dest = tex_to_buf ? buf_row : tex_row,
         .src = tex_to_buf ? tex_row : buf_row,
      };

      d12_list_dispatch_precomp(list, LIBAGX_COPY_UCHAR, &args, sizeof(args),
                                agx_1d(row_bytes));

      if (list->state == D12_LIST_ERROR)
         return;
   }
}

static void STDMETHODCALLTYPE
d12_list_ResourceBarrier(ID3D12GraphicsCommandList *iface, UINT num_barriers,
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
 * Attach a depth/stencil resource to the open render pass.
 *
 * Depth never goes through a background/end-of-tile USC program the way
 * colour does: the fixed-function ZLS unit loads and stores it, configured
 * from the render command. So attaching is bookkeeping here, and the actual
 * hardware setup happens in d12_fill_render_cmd.
 */
static bool
d12_list_attach_ds(struct d12_cmd_list *list, struct d12_resource *ds)
{
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
   return true;
}

static void STDMETHODCALLTYPE
d12_list_ClearDepthStencilView(ID3D12GraphicsCommandList *iface,
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

   if (num_rects != 0) {
      mesa_loge("ClearDepthStencilView: partial-rect clears unsupported");
      list->state = D12_LIST_ERROR;
      return;
   }

   struct d12_resource *ds = dsv->resource;

   if (!d12_list_attach_ds(list, ds))
      return;

   struct d12_segment *seg = &list->segments[list->segment_count - 1];

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
d12_list_OMSetStencilRef(ID3D12GraphicsCommandList *iface, UINT ref)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);
   list->stencil_ref = (uint8_t)ref;
}

static void STDMETHODCALLTYPE
d12_list_OMSetRenderTargets(ID3D12GraphicsCommandList *iface, UINT num_rtvs,
                            const D3D12_CPU_DESCRIPTOR_HANDLE *rtvs,
                            BOOL single_handle,
                            const D3D12_CPU_DESCRIPTOR_HANDLE *dsv)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   /* Record the binding. A render pass is still opened by
    * ClearRenderTargetView, so this cannot yet START a pass -- but it CAN
    * stop a draw from silently landing in a different texture than the
    * one the application bound, which is what happens if these disagree.
    */
   list->bound_rt = NULL;
   if (num_rtvs >= 1 && rtvs) {
      struct d12_rtv *rtv = (struct d12_rtv *)rtvs[0].ptr;
      if (rtv)
         list->bound_rt = rtv->resource;
   }

   if (num_rtvs > 1)
      mesa_logw("OMSetRenderTargets: only one render target is supported");

   /* Binding is the other way a pass acquires depth: an application may
    * bind a depth buffer it filled earlier and never clear it.
    */
   list->bound_ds = NULL;
   if (dsv) {
      struct d12_rtv *view = (struct d12_rtv *)dsv->ptr;
      if (view && view->resource && view->kind == D12_DESC_DSV) {
         list->bound_ds = view->resource;
         d12_list_attach_ds(list, view->resource);
      } else if (view) {
         mesa_loge("OMSetRenderTargets: DSV handle is not a depth/stencil "
                   "view");
         list->state = D12_LIST_ERROR;
      }
   }
}

static const ID3D12GraphicsCommandListVtbl d12_list_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12GraphicsCommandList_DEFAULTS,
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
   .CopyResource = d12_list_CopyResource,
   .CopyTextureRegion = d12_list_CopyTextureRegion,
   .ResourceBarrier = d12_list_ResourceBarrier,
   .OMSetRenderTargets = d12_list_OMSetRenderTargets,
   .OMSetStencilRef = d12_list_OMSetStencilRef,
   .ClearRenderTargetView = d12_list_ClearRenderTargetView,
   .ClearDepthStencilView = d12_list_ClearDepthStencilView,
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
   .SetGraphicsRootConstantBufferView =
      d12_list_SetGraphicsRootConstantBufferView,
   .SetComputeRootDescriptorTable = d12_list_SetComputeRootDescriptorTable,
   .SetGraphicsRootDescriptorTable = d12_list_SetGraphicsRootDescriptorTable,
   .SetDescriptorHeaps = d12_list_SetDescriptorHeaps,
   .Dispatch = d12_list_Dispatch,
   .SetGraphicsRootSignature = d12_list_SetGraphicsRootSignature,
   .IASetPrimitiveTopology = d12_list_IASetPrimitiveTopology,
   .RSSetViewports = d12_list_RSSetViewports,
   .RSSetScissorRects = d12_list_RSSetScissorRects,
   .DrawInstanced = d12_list_DrawInstanced,
   /* Everything unset is NULL: loud crash over silent lie. */
};

HRESULT
d12_cmd_list_create(struct d12_device *device, D3D12_COMMAND_LIST_TYPE type,
                    struct d12_cmd_allocator *allocator, REFIID riid,
                    void **out)
{
   if (!out)
      return E_POINTER;

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
   if (allocator)
      allocator->recording_lists++;
   d12_device_add_ref(device);

   /* D3D12 command lists are created in the recording state. */
   d12_list_begin_recording(list);

   HRESULT hr = d12_list_QueryInterface(&list->iface, riid, out);
   d12_list_Release(&list->iface);
   return hr;
}
