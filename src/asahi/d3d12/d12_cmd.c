/*
 * Copyright 2026 NTASP contributors
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

#define D12_STREAM_BO_SIZE (1024 * 1024)
#define D12_USC_BO_SIZE    (256 * 1024)
/* Worst-case control words per dispatch or render prologue, with slack. */
#define D12_DISPATCH_STREAM_MARGIN 0x300

/*
 * ID3D12CommandAllocator. The real allocations live on the list for now;
 * the allocator is lifetime bookkeeping to honor the API shape.
 */

static struct d12_cmd_allocator *
d12_cmd_allocator_from_iface(ID3D12CommandAllocator *iface)
{
   return (struct d12_cmd_allocator *)iface;
}

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
   return S_OK;
}

static const ID3D12CommandAllocatorVtbl d12_alloc_vtbl = {
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
   list->state = D12_LIST_RECORDING;
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

/* Close the currently-open CDM segment, if any (terminates its stream). */
static void
d12_list_close_cdm(struct d12_cmd_list *list)
{
   if (!list->cdm_open)
      return;

   struct d12_segment *seg = &list->segments[list->segment_count - 1];
   list->stream_cur =
      (uint8_t *)agx_cdm_terminate((uint32_t *)list->stream_cur);
   seg->stream_end = list->stream_cur - d12_list_stream_map(list);
   list->cdm_open = false;
}

/* Get an open CDM segment to append dispatches to, starting one if needed. */
static struct d12_segment *
d12_list_get_cdm(struct d12_cmd_list *list)
{
   if (list->cdm_open)
      return &list->segments[list->segment_count - 1];

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
         cfg.channels = ail_pixel_format[PIPE_FORMAT_R8G8B8A8_UNORM].channels;
         cfg.type = ail_pixel_format[PIPE_FORMAT_R8G8B8A8_UNORM].type;
         cfg.srgb = false;
         cfg.swizzle_r = 0;
         cfg.swizzle_g = 1;
         cfg.swizzle_b = 2;
         cfg.swizzle_a = 3;
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
 * Dispatch a pipeline compiled from the application's DXIL. Mirrors
 * hk_dispatch_with_usc: build USC words binding the shader, its registers
 * and our resource-address uniforms, then emit a CDM launch.
 */
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

   /* The root table of resource addresses, then a pointer to it: uniform
    * registers are filled FROM memory, so the pointer itself needs a home.
    */
   uint64_t addrs[D12_MAX_RESOURCE_BINDINGS];
   memcpy(addrs, list->compute_root_uav, sizeof(addrs));
   uint64_t addrs_gpu = d12_list_upload(list, addrs, sizeof(addrs), 8);
   if (!addrs_gpu)
      return;

   uint64_t root_gpu = d12_list_upload(list, &addrs_gpu, sizeof(addrs_gpu), 8);
   if (!root_gpu)
      return;

   uint32_t usc;
   size_t usc_size = agx_usc_size(8);
   uint8_t *usc_cpu = d12_list_alloc_usc(list, usc_size, &usc);
   if (!usc_cpu)
      return;

   struct agx_usc_builder b = agx_usc_builder(usc_cpu, usc_size);

   /* Four 16-bit uniform registers hold the 64-bit root pointer. */
   agx_usc_uniform(&b, D12_UNIFORM_ROOT, D12_UNIFORM_ROOT_REGS, root_gpu);

   agx_usc_shared_none(&b);

   agx_usc_pack(&b, SHADER, cfg) {
      cfg.code = agx_usc_addr(&device->dev,
                              pso->code_va + pso->info.main_offset);
      cfg.unk_2 = 3; /* compute */
   }

   agx_usc_pack(&b, REGISTERS, cfg)
      cfg.register_count = pso->info.nr_gprs;

   if (pso->info.has_preamble) {
      agx_usc_pack(&b, PRESHADER, cfg) {
         cfg.code = agx_usc_addr(
            &device->dev, pso->code_va + pso->info.preamble_offset);
      }
   } else {
      agx_usc_pack(&b, NO_PRESHADER, cfg)
         ;
   }

   struct agx_cdm_launch_word_0_packed launch;
   agx_pack(&launch, CDM_LAUNCH_WORD_0, cfg) {
      cfg.texture_state_register_count = pso->info.texture_state_count;
      cfg.sampler_state_register_count =
         agx_translate_sampler_state_count(pso->info.sampler_state_count,
                                           false);
      cfg.uniform_register_count = pso->info.push_count;
      cfg.preshader_register_count = pso->info.nr_preamble_gprs;
   }

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
   /* Our binding model needs nothing from the signature object yet. */
}

static void STDMETHODCALLTYPE
d12_list_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList *iface,
                                           UINT root_param_index,
                                           D3D12_GPU_VIRTUAL_ADDRESS address)
{
   struct d12_cmd_list *list = d12_cmd_list_from_iface(iface);

   /* A root UAV is a raw GPU virtual address, which is exactly what the
    * shader's binding ABI wants; root parameter index doubles as the
    * binding slot.
    */
   if (root_param_index >= D12_MAX_RESOURCE_BINDINGS) {
      mesa_loge("SetComputeRootUnorderedAccessView: slot %u out of range",
                root_param_index);
      list->state = D12_LIST_ERROR;
      return;
   }

   list->compute_root_uav[root_param_index] = address;
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
      mesa_logw("Reset: initial pipeline state ignored (no PSOs until G4)");

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

   enum pipe_format formats[AGX_MAX_RENDER_TARGETS] = {
      PIPE_FORMAT_R8G8B8A8_UNORM,
   };
   seg->tib = agx_build_tilebuffer_layout(formats, 1, 1, false);

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

   list->stream_cur =
      (uint8_t *)agx_vdm_terminate((uint32_t *)list->stream_cur);

   seg->stream_end = list->stream_cur - d12_list_stream_map(list);
   list->segment_count++;
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

static void STDMETHODCALLTYPE
d12_list_OMSetRenderTargets(ID3D12GraphicsCommandList *iface, UINT num_rtvs,
                            const D3D12_CPU_DESCRIPTOR_HANDLE *rtvs,
                            BOOL single_handle,
                            const D3D12_CPU_DESCRIPTOR_HANDLE *dsv)
{
   /* Recorded state becomes meaningful with draws (G4). Clears take the
    * RTV handle directly, so this is informational for now.
    */
   if (dsv)
      mesa_logw("OMSetRenderTargets: depth-stencil ignored until G4+");
}

static const ID3D12GraphicsCommandListVtbl d12_list_vtbl = {
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
   .ResourceBarrier = d12_list_ResourceBarrier,
   .OMSetRenderTargets = d12_list_OMSetRenderTargets,
   .ClearRenderTargetView = d12_list_ClearRenderTargetView,
   .SetPipelineState = d12_list_SetPipelineState,
   .SetComputeRootSignature = d12_list_SetComputeRootSignature,
   .SetComputeRootUnorderedAccessView =
      d12_list_SetComputeRootUnorderedAccessView,
   .Dispatch = d12_list_Dispatch,
   /* Everything unset is NULL: loud crash over silent lie. */
};

HRESULT
d12_cmd_list_create(struct d12_device *device, D3D12_COMMAND_LIST_TYPE type,
                    REFIID riid, void **out)
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
   d12_device_add_ref(device);

   /* D3D12 command lists are created in the recording state. */
   d12_list_begin_recording(list);

   HRESULT hr = d12_list_QueryInterface(&list->iface, riid, out);
   d12_list_Release(&list->iface);
   return hr;
}
