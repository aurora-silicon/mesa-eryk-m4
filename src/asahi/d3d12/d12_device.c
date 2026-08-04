/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * ID3D12Device on a real agx_device, plus the D3D12CreateDevice entry point.
 * Device discovery mirrors hk_physical_device.c: enumerate DRM devices, take
 * the render node whose kernel driver is "asahi".
 */

#include "d12_private.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>

/*
 * Report an unimplemented COM method. Logged once per method so a test
 * suite run produces a readable list of what is missing rather than a
 * flood, and so an unimplemented call is a recordable failure instead of
 * a jump to NULL.
 */
void
d12_stub_report(const char *what)
{
   static const char *seen[256];
   static unsigned num_seen;

   for (unsigned i = 0; i < num_seen; i++) {
      if (seen[i] == what)
         return;
   }

   if (num_seen < ARRAY_SIZE(seen))
      seen[num_seen++] = what;

   mesa_logw("unimplemented: %s", what);
}

void
d12_log_unknown_riid(const char *where, REFIID riid)
{
   const GUID *g = riid;
   mesa_loge("%s: unsupported riid "
             "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             where, g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1],
             g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
             g->Data4[7]);
}

/*
 * Find the asahi render node. Returns an open fd or -1.
 */
static int
d12_open_asahi_fd(void)
{
   drmDevicePtr devices[8];
   int count = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   int result = -1;

   for (int i = 0; i < count; i++) {
      if (!(devices[i]->available_nodes & (1 << DRM_NODE_RENDER)))
         continue;

      int fd = open(devices[i]->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
      if (fd < 0)
         continue;

      drmVersionPtr version = drmGetVersion(fd);
      bool is_asahi = version && !strcmp(version->name, "asahi");
      drmFreeVersion(version);

      if (is_asahi) {
         result = fd;
         break;
      }
      close(fd);
   }

   if (count > 0)
      drmFreeDevices(devices, count);

   return result;
}

/*
 * IUnknown + ID3D12Object
 */

static HRESULT STDMETHODCALLTYPE
d12_device_QueryInterface(ID3D12Device2 *iface, REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12Device) ||
       d12_guid_equal(riid, &IID_ID3D12Device1) ||
       d12_guid_equal(riid, &IID_ID3D12Device2)) {
      /* One object, one vtable: ID3D12Device2's is a superset, so the
       * newer IIDs return the same pointer. Mesh pipelines need
       * ID3D12Device2::CreatePipelineState.
       */
      d12_device_add_ref(device);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12Device::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_device_AddRef(ID3D12Device2 *iface)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return p_atomic_inc_return(&device->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_device_Release(ID3D12Device2 *iface)
{
   struct d12_device *device = d12_device_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&device->refcount);

   if (refs == 0) {
      mesa_logi("closing device %s", device->dev.name);
      agx_bo_unreference(&device->dev, device->oclqry_bo);
      agx_bo_unreference(&device->dev, device->rodata_bo);
      if (device->heap_bo)
         agx_bo_unreference(&device->dev, device->heap_bo);
      agx_bg_eot_cleanup(&device->bg_eot);
      agx_close_device(&device->dev);
      free(device);
   }

   return refs;
}

void
d12_device_add_ref(struct d12_device *device)
{
   d12_device_AddRef((ID3D12Device2 *)&device->iface);
}

void
d12_device_release(struct d12_device *device)
{
   d12_device_Release((ID3D12Device2 *)&device->iface);
}

static HRESULT STDMETHODCALLTYPE
d12_device_GetPrivateData(ID3D12Device2 *iface, REFGUID guid, UINT *size,
                          void *data)
{
   mesa_logw("ID3D12Device::GetPrivateData: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_device_SetPrivateData(ID3D12Device2 *iface, REFGUID guid, UINT size,
                          const void *data)
{
   mesa_logw("ID3D12Device::SetPrivateData: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_device_SetPrivateDataInterface(ID3D12Device2 *iface, REFGUID guid,
                                   const IUnknown *data)
{
   mesa_logw("ID3D12Device::SetPrivateDataInterface: not implemented");
   return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
d12_device_SetName(ID3D12Device2 *iface, LPCWSTR name)
{
   /* Accepted and ignored; nothing consumes debug names yet. */
   return S_OK;
}

/*
 * ID3D12Device
 */

static UINT STDMETHODCALLTYPE
d12_device_GetNodeCount(ID3D12Device2 *iface)
{
   return 1;
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommandQueue(ID3D12Device2 *iface,
                              const D3D12_COMMAND_QUEUE_DESC *desc,
                              REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_queue_create(device, desc, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommandAllocator(ID3D12Device2 *iface,
                                  D3D12_COMMAND_LIST_TYPE type, REFIID riid,
                                  void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_cmd_allocator_create(device, type, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommandList(ID3D12Device2 *iface, UINT node_mask,
                             D3D12_COMMAND_LIST_TYPE type,
                             ID3D12CommandAllocator *allocator,
                             ID3D12PipelineState *initial_state, REFIID riid,
                             void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   /* COM CONTRACT: the out-pointer is set to NULL BEFORE any validation.
    * A failing Create that leaves it untouched hands the caller back an
    * uninitialised stack variable, and a well-written application then
    * passes that garbage to the next call -- which is how a refusal here
    * turned into a segfault three calls later, in CreateRenderTargetView
    * and in Draw, with a resource pointer full of ASCII. Nulling first
    * turns every one of those into an ordinary failure.
    */
   if (out)
      *out = NULL;

   if (node_mask > 1)
      return E_INVALIDARG;
   if (!allocator)
      return E_INVALIDARG;
   if (initial_state)
      mesa_logw("CreateCommandList: initial pipeline state ignored (no PSOs "
                "until G4)");

   return d12_cmd_list_create(device, type,
                              d12_cmd_allocator_from_iface(allocator), riid,
                              out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CheckFeatureSupport(ID3D12Device2 *iface, D3D12_FEATURE feature,
                               void *data, UINT data_size)
{
   struct d12_device *device = d12_device_from_iface(iface);

   switch (feature) {
   case D3D12_FEATURE_D3D12_OPTIONS: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;

      memset(opts, 0, sizeof(*opts));
      /* Honest conservative shape; raised only when the backing exists. */
      opts->ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1;
      /* TIER_2 means any resource type in any heap. The hardware would
       * allow it (UMA), but CreateHeap and CreatePlacedResource do not
       * exist, so claiming it would be a capability we cannot honour.
       */
      opts->ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1;
      return S_OK;
   }

   case D3D12_FEATURE_D3D12_OPTIONS5: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS5 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;

      memset(opts, 0, sizeof(*opts));
      /*
       * Stated rather than left absent. The DXIL translator implements
       * ray queries as a traversal that visits nothing, which is the
       * correct answer only while no acceleration structure can exist --
       * and this is what guarantees that.
       */
      opts->RaytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
      opts->RenderPassesTier = D3D12_RENDER_PASS_TIER_0;
      opts->SRVOnlyTiledResourceTier3 = FALSE;
      return S_OK;
   }

   case D3D12_FEATURE_ARCHITECTURE: {
      D3D12_FEATURE_DATA_ARCHITECTURE *arch = data;
      if (data_size != sizeof(*arch))
         return E_INVALIDARG;
      if (arch->NodeIndex != 0)
         return E_INVALIDARG;

      arch->TileBasedRenderer = TRUE;
      arch->UMA = TRUE;
      arch->CacheCoherentUMA = TRUE;
      return S_OK;
   }

   case D3D12_FEATURE_ARCHITECTURE1: {
      D3D12_FEATURE_DATA_ARCHITECTURE1 *arch = data;
      if (data_size != sizeof(*arch))
         return E_INVALIDARG;
      if (arch->NodeIndex != 0)
         return E_INVALIDARG;

      arch->TileBasedRenderer = TRUE;
      arch->UMA = TRUE;
      arch->CacheCoherentUMA = TRUE;
      /* The GPU sits behind its own MMU (GPU VM / DART). */
      arch->IsolatedMMU = TRUE;
      return S_OK;
   }

   case D3D12_FEATURE_SHADER_MODEL: {
      D3D12_FEATURE_DATA_SHADER_MODEL *sm = data;
      if (data_size != sizeof(*sm))
         return E_INVALIDARG;

      /* Our DXIL front end targets SM 6.0. The query clamps DOWN to what
       * the driver supports, per the D3D12 contract.
       */
      if (sm->HighestShaderModel > D3D_SHADER_MODEL_6_0)
         sm->HighestShaderModel = D3D_SHADER_MODEL_6_0;
      return S_OK;
   }

   case D3D12_FEATURE_FEATURE_LEVELS: {
      D3D12_FEATURE_DATA_FEATURE_LEVELS *levels = data;
      if (data_size != sizeof(*levels))
         return E_INVALIDARG;
      if (!levels->pFeatureLevelsRequested || !levels->NumFeatureLevels)
         return E_INVALIDARG;

      D3D_FEATURE_LEVEL best = 0;
      for (UINT i = 0; i < levels->NumFeatureLevels; i++) {
         D3D_FEATURE_LEVEL fl = levels->pFeatureLevelsRequested[i];
         if (fl <= device->feature_level && fl > best)
            best = fl;
      }

      if (!best)
         return E_INVALIDARG;

      levels->MaxSupportedFeatureLevel = best;
      return S_OK;
   }

   default:
      mesa_logw("ID3D12Device::CheckFeatureSupport: feature %d not "
                "implemented",
                (int)feature);
      return E_INVALIDARG;
   }
}

/*
 * The binding assignment a pipeline is compiled against. It has to be the
 * one stored on the root signature: the command list fills the root table
 * through the same assignment at bind time, and two sides deriving their
 * own would only agree by luck.
 */
static const struct d12_binding_map *
d12_pso_binding_map(ID3D12RootSignature *rs)
{
   /* No signature is legal only for a shader that binds nothing. The
    * translator reports it by name if the shader does declare a resource,
    * rather than silently resolving it to slot 0.
    */
   static const struct d12_binding_map empty;

   return rs ? &((struct d12_root_signature *)rs)->map : &empty;
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateComputePipelineState(
   ID3D12Device2 *iface, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
   REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   /* COM CONTRACT: the out-pointer is set to NULL BEFORE any validation.
    * A failing Create that leaves it untouched hands the caller back an
    * uninitialised stack variable, and a well-written application then
    * passes that garbage to the next call -- which is how a refusal here
    * turned into a segfault three calls later, in CreateRenderTargetView
    * and in Draw, with a resource pointer full of ASCII. Nulling first
    * turns every one of those into an ordinary failure.
    */
   if (out)
      *out = NULL;

   if (!desc || !desc->CS.pShaderBytecode || !desc->CS.BytecodeLength)
      return E_INVALIDARG;

   /* A pipeline is compiled against its root signature's binding
    * assignment. Without one there is nothing to bind resources through,
    * so a shader that declares any would resolve to no slot at all.
    */
   const struct d12_binding_map *map =
      d12_pso_binding_map(desc->pRootSignature);

   return d12_pipeline_state_create_compute(device, desc->CS.pShaderBytecode,
                                            desc->CS.BytecodeLength, map,
                                            riid, out);
}

/*
 * ID3D12Device2::CreatePipelineState — the STREAM descriptor.
 *
 * Instead of a fixed struct, the caller hands over a byte stream of
 * tagged subobjects: each is a D3D12_PIPELINE_STATE_SUBOBJECT_TYPE
 * followed by its payload, and the whole entry is aligned to void*.
 * Mesh pipelines exist only in this form -- there is no
 * D3D12_MESH_PIPELINE_STATE_DESC.
 *
 * Unknown subobject types are refused rather than skipped: the stream
 * gives no length for an entry, so a type this driver does not know is
 * also a type it cannot step over. Guessing the size would silently
 * misparse everything after it.
 */
struct d12_pso_stream {
   const D3D12_SHADER_BYTECODE *vs, *ps, *ms, *as, *cs, *gs, *hs, *ds;
   ID3D12RootSignature *root_sig;
   const D3D12_RASTERIZER_DESC *raster;
   const D3D12_INPUT_LAYOUT_DESC *input_layout;
   const D3D12_DEPTH_STENCIL_DESC *ds_state;
   struct D3D12_RT_FORMAT_ARRAY rt_formats;
   DXGI_FORMAT dsv_format;
   bool have_rt_formats;
};

static bool
d12_parse_pso_stream(const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                     struct d12_pso_stream *out)
{
   const uint8_t *p = (const uint8_t *)desc->pPipelineStateSubobjectStream;
   const uint8_t *end = p + desc->SizeInBytes;

   memset(out, 0, sizeof(*out));

   while (p + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= end) {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
      memcpy(&type, p, sizeof(type));

      /* The payload begins at the next void*-aligned offset. */
      const uint8_t *body =
         p + ALIGN_POT(sizeof(type), sizeof(void *));
      size_t body_size;

#define SUBOBJ(TY, field, ctype)                                                case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_##TY:                                  out->field = (const ctype *)body;                                            body_size = sizeof(ctype);                                                   break

      switch (type) {
         SUBOBJ(VS, vs, D3D12_SHADER_BYTECODE);
         SUBOBJ(PS, ps, D3D12_SHADER_BYTECODE);
         SUBOBJ(MS, ms, D3D12_SHADER_BYTECODE);
         SUBOBJ(AS, as, D3D12_SHADER_BYTECODE);
         SUBOBJ(CS, cs, D3D12_SHADER_BYTECODE);
         SUBOBJ(GS, gs, D3D12_SHADER_BYTECODE);
         SUBOBJ(HS, hs, D3D12_SHADER_BYTECODE);
         SUBOBJ(DS, ds, D3D12_SHADER_BYTECODE);
         SUBOBJ(RASTERIZER, raster, D3D12_RASTERIZER_DESC);
         SUBOBJ(INPUT_LAYOUT, input_layout, D3D12_INPUT_LAYOUT_DESC);
         SUBOBJ(DEPTH_STENCIL, ds_state, D3D12_DEPTH_STENCIL_DESC);

      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
         memcpy(&out->root_sig, body, sizeof(out->root_sig));
         body_size = sizeof(ID3D12RootSignature *);
         break;

      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
         memcpy(&out->rt_formats, body, sizeof(out->rt_formats));
         out->have_rt_formats = true;
         body_size = sizeof(struct D3D12_RT_FORMAT_ARRAY);
         break;

      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
         memcpy(&out->dsv_format, body, sizeof(out->dsv_format));
         body_size = sizeof(DXGI_FORMAT);
         break;

      /* Accepted and ignored: none of these change what we build. */
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
         body_size = sizeof(UINT);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
         body_size = sizeof(D3D12_PIPELINE_STATE_FLAGS);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
         body_size = sizeof(UINT);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
         body_size = sizeof(DXGI_SAMPLE_DESC);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
         body_size = sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
         body_size = sizeof(D3D12_BLEND_DESC);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
         body_size = sizeof(D3D12_CACHED_PIPELINE_STATE);
         break;

      default:
         mesa_loge("CreatePipelineState: unknown stream subobject %d; the "
                   "stream carries no length so it cannot be stepped over",
                   (int)type);
         return false;
      }
#undef SUBOBJ

      p = body + ALIGN_POT(body_size, sizeof(void *));
   }

   return true;
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreatePipelineState(ID3D12Device2 *iface,
                               const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                               REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (!out)
      return E_POINTER;

   /* See the COM contract note above: null before validating. */
   *out = NULL;

   if (!desc || !desc->pPipelineStateSubobjectStream || !desc->SizeInBytes)
      return E_INVALIDARG;

   struct d12_pso_stream st;
   if (!d12_parse_pso_stream(desc, &st))
      return E_INVALIDARG;

   const struct d12_binding_map *map = d12_pso_binding_map(st.root_sig);

   enum pipe_format rt_format = PIPE_FORMAT_NONE;
   if (st.have_rt_formats && st.rt_formats.NumRenderTargets >= 1)
      rt_format = d12_format_to_pipe(st.rt_formats.RTFormats[0]);

   /* A compute pipeline expressed as a stream. */
   if (st.cs && st.cs->pShaderBytecode) {
      return d12_pipeline_state_create_compute(
         device, st.cs->pShaderBytecode, st.cs->BytecodeLength, map, riid,
         out);
   }

   if (st.ms && st.ms->pShaderBytecode) {
      if (!st.ps || !st.ps->pShaderBytecode) {
         mesa_loge("CreatePipelineState: a mesh pipeline needs a pixel "
                   "shader");
         return E_INVALIDARG;
      }
      return d12_pipeline_state_create_mesh(
         device, st.ms->pShaderBytecode, st.ms->BytecodeLength,
         st.as ? st.as->pShaderBytecode : NULL,
         st.as ? st.as->BytecodeLength : 0, st.ps->pShaderBytecode,
         st.ps->BytecodeLength, rt_format, st.ds_state, st.dsv_format,
         st.raster, map, riid, out);
   }

   if (st.vs && st.vs->pShaderBytecode && st.ps && st.ps->pShaderBytecode) {
      return d12_pipeline_state_create_graphics(
         device, st.vs->pShaderBytecode, st.vs->BytecodeLength,
         st.ps->pShaderBytecode, st.ps->BytecodeLength,
         st.gs ? st.gs->pShaderBytecode : NULL,
         st.gs ? st.gs->BytecodeLength : 0,
         st.hs ? st.hs->pShaderBytecode : NULL,
         st.hs ? st.hs->BytecodeLength : 0,
         st.ds ? st.ds->pShaderBytecode : NULL,
         st.ds ? st.ds->BytecodeLength : 0, rt_format, st.ds_state,
         st.dsv_format, st.raster, st.input_layout, map, riid, out);
   }

   mesa_loge("CreatePipelineState: stream declares no usable pipeline");
   return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateGraphicsPipelineState(
   ID3D12Device2 *iface, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
   REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   /* COM CONTRACT: the out-pointer is set to NULL BEFORE any validation.
    * A failing Create that leaves it untouched hands the caller back an
    * uninitialised stack variable, and a well-written application then
    * passes that garbage to the next call -- which is how a refusal here
    * turned into a segfault three calls later, in CreateRenderTargetView
    * and in Draw, with a resource pointer full of ASCII. Nulling first
    * turns every one of those into an ordinary failure.
    */
   if (out)
      *out = NULL;

   if (!desc || !desc->VS.pShaderBytecode || !desc->PS.pShaderBytecode)
      return E_INVALIDARG;

   /* Tessellation needs BOTH stages. One without the other would fall
    * through the tessellation path and draw the control points
    * untessellated -- which draws something, and something reads as a
    * pass. Refuse by name.
    */
   bool has_hs = desc->HS.pShaderBytecode && desc->HS.BytecodeLength;
   bool has_ds = desc->DS.pShaderBytecode && desc->DS.BytecodeLength;
   if (has_hs != has_ds) {
      mesa_loge("CreateGraphicsPipelineState: a %s shader is bound without "
                "a %s shader; tessellation needs both.",
                has_hs ? "hull" : "domain", has_hs ? "domain" : "hull");
      return E_INVALIDARG;
   }

   /* The pipeline's declared render-target format now drives the
    * tilebuffer layout instead of an assumed RGBA8.
    */
   enum pipe_format rt_format = PIPE_FORMAT_NONE;
   if (desc->NumRenderTargets >= 1)
      rt_format = d12_format_to_pipe(desc->RTVFormats[0]);

   const struct d12_binding_map *map =
      d12_pso_binding_map(desc->pRootSignature);

   return d12_pipeline_state_create_graphics(
      device, desc->VS.pShaderBytecode, desc->VS.BytecodeLength,
      desc->PS.pShaderBytecode, desc->PS.BytecodeLength,
      desc->GS.pShaderBytecode, desc->GS.BytecodeLength,
      desc->HS.pShaderBytecode, desc->HS.BytecodeLength,
      desc->DS.pShaderBytecode, desc->DS.BytecodeLength, rt_format,
      &desc->DepthStencilState, desc->DSVFormat, &desc->RasterizerState,
      &desc->InputLayout, map, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateRootSignature(ID3D12Device2 *iface, UINT node_mask,
                               const void *blob, SIZE_T blob_size,
                               REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   /* COM CONTRACT: the out-pointer is set to NULL BEFORE any validation.
    * A failing Create that leaves it untouched hands the caller back an
    * uninitialised stack variable, and a well-written application then
    * passes that garbage to the next call -- which is how a refusal here
    * turned into a segfault three calls later, in CreateRenderTargetView
    * and in Draw, with a resource pointer full of ASCII. Nulling first
    * turns every one of those into an ordinary failure.
    */
   if (out)
      *out = NULL;

   if (node_mask > 1)
      return E_INVALIDARG;

   return d12_root_signature_create(device, blob, blob_size, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateDescriptorHeap(ID3D12Device2 *iface,
                                const D3D12_DESCRIPTOR_HEAP_DESC *desc,
                                REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_descriptor_heap_create(device, desc, riid, out);
}

static void STDMETHODCALLTYPE
d12_device_CreateRenderTargetView(ID3D12Device2 *iface,
                                  ID3D12Resource *resource,
                                  const D3D12_RENDER_TARGET_VIEW_DESC *desc,
                                  D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_rtv *slot = (struct d12_rtv *)dest.ptr;

   if (!slot)
      return;

   if (!resource) {
      /* Null RTVs are legal placeholders; record an empty slot. */
      slot->resource = NULL;
      return;
   }

   struct d12_resource *res = d12_resource_from_iface(resource);

   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
       !d12_format_is_renderable(res->desc.Format)) {
      mesa_loge("CreateRenderTargetView: format %d is not a renderable 2D "
                "target", (int)res->desc.Format);
      slot->resource = NULL;
      return;
   }

   if (desc && desc->ViewDimension != D3D12_RTV_DIMENSION_TEXTURE2D) {
      mesa_loge("CreateRenderTargetView: view dimension %d unsupported",
                (int)desc->ViewDimension);
      slot->resource = NULL;
      return;
   }

   slot->resource = res;
   slot->format = res->desc.Format;
   slot->kind = D12_DESC_RTV;
   slot->address = res->bo->va->addr;
}

static void STDMETHODCALLTYPE
d12_device_CreateDepthStencilView(ID3D12Device2 *iface,
                                  ID3D12Resource *resource,
                                  const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
                                  D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_rtv *slot = (struct d12_rtv *)dest.ptr;

   if (!slot)
      return;

   if (!resource) {
      /* Null DSVs are legal placeholders; record an empty slot. */
      slot->resource = NULL;
      return;
   }

   struct d12_resource *res = d12_resource_from_iface(resource);

   if (!res->is_depth_stencil) {
      mesa_loge("CreateDepthStencilView: resource format %d is not a "
                "depth/stencil format", (int)res->desc.Format);
      slot->resource = NULL;
      return;
   }

   if (desc && desc->ViewDimension != D3D12_DSV_DIMENSION_TEXTURE2D) {
      mesa_loge("CreateDepthStencilView: view dimension %d unsupported",
                (int)desc->ViewDimension);
      slot->resource = NULL;
      return;
   }

   /* A read-only DSV would need the depth test to run with stores masked
    * off at the ZLS level; accepting the flag and writing anyway would
    * corrupt a buffer the app is simultaneously sampling.
    */
   if (desc && desc->Flags) {
      mesa_loge("CreateDepthStencilView: read-only DSV flags 0x%x "
                "unsupported", (unsigned)desc->Flags);
      slot->resource = NULL;
      return;
   }

   slot->resource = res;
   slot->format = desc && desc->Format != DXGI_FORMAT_UNKNOWN
                     ? desc->Format
                     : res->desc.Format;
   slot->kind = D12_DESC_DSV;
   slot->address = res->bo->va->addr;
}

/*
 * Buffer views. A descriptor here is just the address the shader will
 * reach the resource through, resolved once at view creation.
 *
 * Only buffer views exist so far: a texture SRV needs an AGX texture
 * descriptor and a sampler, which is the texturing milestone. Refusing a
 * texture view is better than recording an address a shader would then
 * read as if it were linear untiled data.
 */
static bool
d12_fill_buffer_descriptor(struct d12_descriptor *slot,
                           struct d12_resource *res, uint64_t first_element,
                           uint32_t num_elements, uint32_t stride_B,
                           const char *what)
{
   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
      mesa_loge("%s: only buffer views are supported so far", what);
      slot->kind = D12_DESC_NONE;
      slot->resource = NULL;
      return false;
   }

   uint64_t offset = first_element * (uint64_t)MAX2(stride_B, 1);
   uint64_t span = (uint64_t)num_elements * MAX2(stride_B, 1);

   if (offset + span > res->bo->size) {
      mesa_loge("%s: view spans past the end of the resource", what);
      slot->kind = D12_DESC_NONE;
      slot->resource = NULL;
      return false;
   }

   slot->resource = res;
   slot->address = res->bo->va->addr + offset;
   slot->size_B = (uint32_t)span;
   slot->stride_B = stride_B;
   return true;
}

static void STDMETHODCALLTYPE
d12_device_CreateShaderResourceView(ID3D12Device2 *iface,
                                    ID3D12Resource *resource,
                                    const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
                                    D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_descriptor *slot = (struct d12_descriptor *)dest.ptr;

   if (!slot)
      return;

   memset(slot, 0, sizeof(*slot));

   if (!resource)
      return; /* a null SRV is a legal placeholder */

   struct d12_resource *res = d12_resource_from_iface(resource);

   /* A 2D texture view records the resource; the hardware descriptor is
    * packed at draw time, when the heap it lives in is built.
    */
   if (desc && desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D) {
      if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
         mesa_loge("CreateShaderResourceView: TEXTURE2D view of a "
                   "non-texture resource");
         return;
      }

      slot->kind = D12_DESC_SRV;
      slot->resource = res;
      slot->address = res->bo->va->addr;
      slot->format = desc->Format != DXGI_FORMAT_UNKNOWN ? desc->Format
                                                         : res->desc.Format;
      return;
   }

   if (desc && desc->ViewDimension != D3D12_SRV_DIMENSION_BUFFER) {
      mesa_loge("CreateShaderResourceView: view dimension %d unsupported",
                (int)desc->ViewDimension);
      return;
   }

   uint64_t first = desc ? desc->Buffer.FirstElement : 0;
   uint32_t count = desc ? desc->Buffer.NumElements
                         : (uint32_t)(res->bo->size / 4);
   uint32_t stride = desc ? desc->Buffer.StructureByteStride : 4;

   /* A raw (ByteAddressBuffer) view addresses 4-byte words. */
   if (desc && (desc->Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW))
      stride = 4;
   if (!stride)
      stride = 4;

   if (d12_fill_buffer_descriptor(slot, res, first, count, stride,
                                  "CreateShaderResourceView")) {
      slot->kind = D12_DESC_SRV;
      slot->format = desc ? desc->Format : DXGI_FORMAT_UNKNOWN;
   }
}

static void STDMETHODCALLTYPE
d12_device_CreateUnorderedAccessView(
   ID3D12Device2 *iface, ID3D12Resource *resource, ID3D12Resource *counter,
   const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
   D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_descriptor *slot = (struct d12_descriptor *)dest.ptr;

   if (!slot)
      return;

   memset(slot, 0, sizeof(*slot));

   if (!resource)
      return;

   struct d12_resource *res = d12_resource_from_iface(resource);

   if (desc && desc->ViewDimension != D3D12_UAV_DIMENSION_BUFFER) {
      mesa_loge("CreateUnorderedAccessView: view dimension %d unsupported",
                (int)desc->ViewDimension);
      return;
   }

   /* A UAV counter is a separate allocation the shader reaches through
    * bufferUpdateCounter. Its address rides in the descriptor
    * (counter_address, part of the shader-visible ABI) and in the root
    * table's second array; the CounterOffsetInBytes applies on top of
    * the counter resource's base, per the D3D12 spec.
    */
   if (counter) {
      struct d12_resource *cres = d12_resource_from_iface(counter);
      uint64_t off = desc ? desc->Buffer.CounterOffsetInBytes : 0;
      slot->counter_address = cres->bo->va->addr + off;
   }

   uint64_t first = desc ? desc->Buffer.FirstElement : 0;
   uint32_t count = desc ? desc->Buffer.NumElements
                         : (uint32_t)(res->bo->size / 4);
   uint32_t stride = desc ? desc->Buffer.StructureByteStride : 4;

   if (desc && (desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW))
      stride = 4;
   if (!stride)
      stride = 4;

   if (d12_fill_buffer_descriptor(slot, res, first, count, stride,
                                  "CreateUnorderedAccessView")) {
      slot->kind = D12_DESC_UAV;
      slot->format = desc ? desc->Format : DXGI_FORMAT_UNKNOWN;
   }
}

static void STDMETHODCALLTYPE
d12_device_CreateConstantBufferView(
   ID3D12Device2 *iface, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
   D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_descriptor *slot = (struct d12_descriptor *)dest.ptr;

   if (!slot)
      return;

   memset(slot, 0, sizeof(*slot));

   if (!desc || !desc->BufferLocation)
      return;

   /* A CBV names a GPU address directly rather than a resource. */
   slot->kind = D12_DESC_CBV;
   slot->address = desc->BufferLocation;
   slot->size_B = desc->SizeInBytes;
}

static UINT STDMETHODCALLTYPE
d12_device_GetDescriptorHandleIncrementSize(
   ID3D12Device2 *iface, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   /* sizeof(struct d12_descriptor). Apps must treat this as opaque; the
    * value only has to be stable and truthful about our layout.
    */
   switch (type) {
   case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
   case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
   case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
   case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
      return sizeof(struct d12_descriptor);
   default:
      return 0;
   }
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommittedResource(ID3D12Device2 *iface,
                                   const D3D12_HEAP_PROPERTIES *heap_props,
                                   D3D12_HEAP_FLAGS heap_flags,
                                   const D3D12_RESOURCE_DESC *desc,
                                   D3D12_RESOURCE_STATES initial_state,
                                   const D3D12_CLEAR_VALUE *clear_value,
                                   REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_resource_create_committed(device, heap_props, desc, riid, out);
}

/*
 * Describe how a resource's subresources sit in a buffer. This is the API's
 * own answer to row alignment, and it is what lets an application (or an
 * independently written test) copy a texture out without knowing anything
 * about our internal layout.
 */
static void STDMETHODCALLTYPE
d12_device_GetCopyableFootprints(ID3D12Device2 *iface,
                                 const D3D12_RESOURCE_DESC *desc,
                                 UINT first_subresource,
                                 UINT num_subresources, UINT64 base_offset,
                                 D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
                                 UINT *num_rows, UINT64 *row_size_bytes,
                                 UINT64 *total_bytes)
{
   UINT64 offset = base_offset;
   UINT64 total = 0;

   if (!desc) {
      if (total_bytes)
         *total_bytes = 0;
      return;
   }

   for (UINT i = 0; i < num_subresources; i++) {
      UINT sub = first_subresource + i;
      UINT width, height, rows, pitch;
      UINT64 row_size;

      if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
         width = (UINT)desc->Width;
         height = 1;
         rows = 1;
         row_size = desc->Width;
         pitch = (UINT)ALIGN_POT(desc->Width,
                                 D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
      } else {
         /* The texel size is the FORMAT's, not four. This function is
          * how an application sizes the buffer it reads a texture back
          * into, so a wrong answer here is not a failed call -- it is a
          * readback that overruns or interleaves rows, in the caller's
          * own memcpy loop, with S_OK everywhere.
          */
         unsigned bpp = d12_format_bytes_per_pixel(desc->Format);
         if (!bpp) {
            mesa_loge("GetCopyableFootprints: format %d has no known texel "
                      "size", (int)desc->Format);
            return;
         }

         width = (UINT)desc->Width;
         height = desc->Height;
         rows = height;
         row_size = (UINT64)width * bpp;
         pitch = (UINT)ALIGN_POT(row_size,
                                 D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
      }

      if (layouts) {
         layouts[i].Offset = offset;
         layouts[i].Footprint.Format = desc->Format;
         layouts[i].Footprint.Width = width;
         layouts[i].Footprint.Height = height;
         layouts[i].Footprint.Depth = 1;
         layouts[i].Footprint.RowPitch = pitch;
      }
      if (num_rows)
         num_rows[i] = rows;
      if (row_size_bytes)
         row_size_bytes[i] = row_size;

      /* TotalBytes has to cover what the LAYOUT spans, including the
       * inter-subresource padding `offset` advances by. Summing the
       * unpadded sizes reports less than layouts[last].Offset + its size,
       * so an application sizing an upload heap from it writes past the
       * end of its own buffer.
       */
      UINT64 sub_size = (UINT64)pitch * rows;
      offset += ALIGN_POT(sub_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
      total = offset;

      (void)sub;
   }

   if (total_bytes)
      *total_bytes = total;
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateFence(ID3D12Device2 *iface, UINT64 initial_value,
                       D3D12_FENCE_FLAGS flags, REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_fence_create(device, initial_value, flags, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_GetDeviceRemovedReason(ID3D12Device2 *iface)
{
   struct d12_device *device = d12_device_from_iface(iface);

   /* Real answer: a failed submission removes the device. */
   return device->removed ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
}

static LUID STDMETHODCALLTYPE
d12_device_GetAdapterLuid(ID3D12Device2 *iface)
{
   /* A zero LUID is a valid-looking value that collides with every other
    * adapter. One fixed non-zero value is honest for a single-adapter
    * driver and lets callers key on it.
    */
   LUID luid = {.LowPart = 0xA6A00001u, .HighPart = 0};
   return luid;
}

/* ID3D12Device2's vtable is a superset of ID3D12Device's, so the object
 * carries one vtable and QueryInterface hands out the newer IIDs against
 * the same pointer. Mesh pipelines are created through
 * ID3D12Device2::CreatePipelineState, which takes a STREAM descriptor
 * rather than the fixed GRAPHICS/COMPUTE structs.
 */
static HRESULT STDMETHODCALLTYPE
d12_device_CreateQueryHeap(ID3D12Device2 *iface,
                           const D3D12_QUERY_HEAP_DESC *desc, REFIID riid,
                           void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_query_heap_create(device, desc, riid, out);
}

static const ID3D12Device2Vtbl d12_device_vtbl = {
   /* every unimplemented slot fails safely */
   D12_ID3D12Device2_DEFAULTS,
   .QueryInterface = d12_device_QueryInterface,
   .AddRef = d12_device_AddRef,
   .Release = d12_device_Release,
   .GetPrivateData = d12_device_GetPrivateData,
   .SetPrivateData = d12_device_SetPrivateData,
   .SetPrivateDataInterface = d12_device_SetPrivateDataInterface,
   .SetName = d12_device_SetName,
   .GetNodeCount = d12_device_GetNodeCount,
   .CreateCommandQueue = d12_device_CreateCommandQueue,
   .CreateCommandAllocator = d12_device_CreateCommandAllocator,
   .CreateCommandList = d12_device_CreateCommandList,
   .CheckFeatureSupport = d12_device_CheckFeatureSupport,
   .CreateGraphicsPipelineState = d12_device_CreateGraphicsPipelineState,
   .CreatePipelineState = d12_device_CreatePipelineState,
   .CreateComputePipelineState = d12_device_CreateComputePipelineState,
   .CreateRootSignature = d12_device_CreateRootSignature,
   .CreateDescriptorHeap = d12_device_CreateDescriptorHeap,
   .CreateRenderTargetView = d12_device_CreateRenderTargetView,
   .CreateDepthStencilView = d12_device_CreateDepthStencilView,
   .CreateShaderResourceView = d12_device_CreateShaderResourceView,
   .CreateUnorderedAccessView = d12_device_CreateUnorderedAccessView,
   .CreateConstantBufferView = d12_device_CreateConstantBufferView,
   .GetDescriptorHandleIncrementSize =
      d12_device_GetDescriptorHandleIncrementSize,
   .CreateCommittedResource = d12_device_CreateCommittedResource,
   .GetCopyableFootprints = d12_device_GetCopyableFootprints,
   .CreateQueryHeap = d12_device_CreateQueryHeap,
   .CreateFence = d12_device_CreateFence,
   .GetDeviceRemovedReason = d12_device_GetDeviceRemovedReason,
   .GetAdapterLuid = d12_device_GetAdapterLuid,
   /* Everything unset is NULL: a call is a loud crash, not silent lies. */
};

/*
 * Entry points
 */

PUBLIC HRESULT WINAPI
D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
                  REFIID riid, void **out)
{
   /* We support up to 11_0 for now; raised as milestones land. */
   const D3D_FEATURE_LEVEL max_fl = D3D_FEATURE_LEVEL_11_0;

   if (adapter) {
      mesa_logw("D3D12CreateDevice: ignoring explicit adapter, using the "
                "asahi render node");
   }

   /* D3D12 has no levels below 11_0; accepting 9_x/10_x would claim
    * support for a contract that does not exist here.
    */
   if (minimum_feature_level < D3D_FEATURE_LEVEL_11_0 ||
       minimum_feature_level > max_fl) {
      mesa_loge("D3D12CreateDevice: feature level 0x%x unsupported (this "
                "driver is 0x%x only)",
                minimum_feature_level, max_fl);
      return E_INVALIDARG;
   }

   /* Answer the capability probe before opening anything, so the result
    * does not depend on whether a GPU node happens to be present.
    */
   if (!out)
      return S_FALSE;

   int fd = d12_open_asahi_fd();
   if (fd < 0) {
      mesa_loge("D3D12CreateDevice: no asahi render node found");
      return E_FAIL;
   }

   struct d12_device *device = calloc(1, sizeof(*device));
   if (!device) {
      close(fd);
      return E_OUTOFMEMORY;
   }

   device->dev.fd = fd;
   if (!agx_open_device(NULL, &device->dev)) {
      mesa_loge("D3D12CreateDevice: agx_open_device failed");
      close(fd);
      free(device);
      return E_FAIL;
   }

   /* Precompiled internal kernels (copy/fill engines) */
   agx_bg_eot_init(&device->bg_eot, &device->dev);
   if (!device->bg_eot.ht) {
      mesa_loge("D3D12CreateDevice: agx_bg_eot_init failed");
      agx_close_device(&device->dev);
      free(device);
      return E_FAIL;
   }

   /* Render commands need a valid occlusion-query scratch base. */
   device->oclqry_bo =
      agx_bo_create(&device->dev, 0x4000, 0, 0, "d3d12 oclqry scratch");
   if (!device->oclqry_bo) {
      agx_bg_eot_cleanup(&device->bg_eot);
      agx_close_device(&device->dev);
      free(device);
      return E_OUTOFMEMORY;
   }

   /*
    * A texel fetch still goes through the sampler hardware on AGX: the
    * backend lowers txf to `smpi ... ss0`, which reads sampler state 0
    * from whatever the USC's SAMPLER word points at. HLSL's
    * Texture2D.Load takes no sampler, so nothing in the application will
    * ever supply one -- the driver must, exactly as Honeykrisp does
    * (hk_device.c) with a device-wide read-only sampler.
    *
    * Omitting it is not a hard failure. Sampler state 0 is simply
    * undefined, and undefined here reads as zeroes: the texture
    * descriptor, the address and the coordinates are all correct and the
    * fetch still returns nothing.
    */
   /* Sampler state, then the tessellation heap descriptor. */
   device->rodata_bo =
      agx_bo_create(&device->dev,
                    ALIGN_POT(AGX_SAMPLER_LENGTH, 16) +
                       sizeof(struct poly_heap),
                    0, 0, "d3d12 rodata");
   if (!device->rodata_bo) {
      agx_bo_unreference(&device->dev, device->oclqry_bo);
      agx_bg_eot_cleanup(&device->bg_eot);
      agx_close_device(&device->dev);
      free(device);
      return E_OUTOFMEMORY;
   }

   agx_pack_txf_sampler(
      (struct agx_sampler_packed *)agx_bo_map(device->rodata_bo));

   agx_pack(&device->dev.txf_sampler, USC_SAMPLER, cfg) {
      cfg.start = 0;
      cfg.count = 1;
      cfg.buffer = device->rodata_bo->va->addr;
   }

   /*
    * The tessellation heap. The tessellator bump-allocates domain points
    * from it and the prefix sum carves the final index buffer out of it,
    * and that index buffer is then bound as the WHOLE heap with an
    * element offset -- so it has to be one contiguous allocation.
    *
    * 32 MiB rather than hk's 128: this driver runs one command list at a
    * time and `bottom` is reset per list, so the live set is a single
    * draw's tessellation output. Overflow is not silent -- the allocator
    * aborts on the GPU -- so a too-small heap fails loudly.
    */
   device->heap_bo = agx_bo_create(&device->dev, 32 * 1024 * 1024, 0, 0,
                                   "d3d12 tessellation heap");
   if (!device->heap_bo) {
      agx_bo_unreference(&device->dev, device->rodata_bo);
      agx_bo_unreference(&device->dev, device->oclqry_bo);
      agx_bg_eot_cleanup(&device->bg_eot);
      agx_close_device(&device->dev);
      free(device);
      return E_OUTOFMEMORY;
   }

   size_t heap_off = ALIGN_POT(AGX_SAMPLER_LENGTH, 16);
   device->heap_desc_va = device->rodata_bo->va->addr + heap_off;
   device->heap_desc_cpu =
      (struct poly_heap *)((uint8_t *)agx_bo_map(device->rodata_bo) +
                           heap_off);

   *device->heap_desc_cpu = (struct poly_heap){
      .base = (DEVICE(uchar))device->heap_bo->va->addr,
      .bottom = 0,
      .size = (uint32_t)device->heap_bo->size,
   };

   device->iface.lpVtbl = (ID3D12DeviceVtbl *)&d12_device_vtbl;
   device->refcount = 1;
   device->feature_level = max_fl;

   mesa_logi("opened %s (chip 0x%x) as D3D12 device, feature level 0x%x",
             device->dev.name, device->dev.params.chip_id,
             device->feature_level);

   HRESULT hr =
      d12_device_QueryInterface((ID3D12Device2 *)&device->iface, riid, out);
   /* Drop the construction ref; QueryInterface holds the caller's. */
   d12_device_Release((ID3D12Device2 *)&device->iface);
   return hr;
}

PUBLIC HRESULT WINAPI
D3D12GetDebugInterface(REFIID riid, void **out)
{
   /* No debug layer. Callers treat this as optional. */
   if (out)
      *out = NULL;
   return E_NOINTERFACE;
}

/*
 * Remaining module-level entry points.
 *
 * These are exported so a conformance suite can LINK and RUN against us:
 * a missing symbol fails to resolve and kills the process before a single
 * test reports, whereas an honest failure here is a recordable result.
 * They are deliberately not faked — each returns a real failure until the
 * corresponding feature exists.
 */

PUBLIC HRESULT WINAPI
D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *desc,
                            D3D_ROOT_SIGNATURE_VERSION version,
                            ID3DBlob **blob, ID3DBlob **error_blob)
{
   if (error_blob)
      *error_blob = NULL;
   if (!blob)
      return E_POINTER;

   *blob = NULL;

   if (version != D3D_ROOT_SIGNATURE_VERSION_1_0 &&
       version != D3D_ROOT_SIGNATURE_VERSION_1_1) {
      mesa_loge("D3D12SerializeRootSignature: version 0x%x unsupported",
                (unsigned)version);
      return E_INVALIDARG;
   }

   /* Emitted as 1.0 whatever was asked for: the 1.1 additions are
    * volatility promises that only enable optimizations we do not make.
    * The parser accepts both, so a 1.1 blob from elsewhere still loads.
    */
   void *data = NULL;
   size_t size = 0;
   HRESULT hr = d12_serialize_root_signature(desc, &data, &size);
   if (FAILED(hr))
      return hr;

   *blob = d12_blob_create(data, size);
   free(data);

   return *blob ? S_OK : E_OUTOFMEMORY;
}

PUBLIC HRESULT WINAPI
D3D12SerializeVersionedRootSignature(
   const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob,
   ID3DBlob **error_blob)
{
   d12_stub_report("D3D12SerializeVersionedRootSignature");
   if (blob)
      *blob = NULL;
   if (error_blob)
      *error_blob = NULL;
   return E_NOTIMPL;
}

PUBLIC HRESULT WINAPI
D3D12CreateRootSignatureDeserializer(const void *src, SIZE_T size,
                                     REFIID riid, void **out)
{
   d12_stub_report("D3D12CreateRootSignatureDeserializer");
   if (out)
      *out = NULL;
   return E_NOTIMPL;
}

PUBLIC HRESULT WINAPI
D3D12CreateVersionedRootSignatureDeserializer(const void *src, SIZE_T size,
                                              REFIID riid, void **out)
{
   d12_stub_report("D3D12CreateVersionedRootSignatureDeserializer");
   if (out)
      *out = NULL;
   return E_NOTIMPL;
}

PUBLIC HRESULT WINAPI
D3D12GetInterface(REFCLSID rclsid, REFIID riid, void **out)
{
   d12_stub_report("D3D12GetInterface");
   if (out)
      *out = NULL;
   return E_NOINTERFACE;
}

PUBLIC HRESULT WINAPI
D3D12EnableExperimentalFeatures(UINT num_features, const IID *iids,
                                void *configs, UINT *configs_sizes)
{
   d12_stub_report("D3D12EnableExperimentalFeatures");
   return E_NOINTERFACE;
}
