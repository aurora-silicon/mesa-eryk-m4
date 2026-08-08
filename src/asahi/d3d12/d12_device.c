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

   if (d12_guid_equal(riid, &IID_ID3D12Device15)) {
      d12_device_add_ref(device);
      *out = &device->iface15;
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
      d12_private_data_store_destroy(&device->private_data);
      agx_bo_unreference(&device->dev, device->oclqry_bo);
      agx_bo_unreference(&device->dev, device->rodata_bo);
      if (device->heap_bo)
         agx_bo_unreference(&device->dev, device->heap_bo);
      agx_scratch_fini(&device->scratch_vs);
      agx_scratch_fini(&device->scratch_fs);
      agx_scratch_fini(&device->scratch_cs);
      simple_mtx_destroy(&device->scratch_lock);
      simple_mtx_destroy(&device->descriptor_heap_lock);
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

D12_DEFINE_PRIVATE_DATA_METHODS(d12_device, ID3D12Device2,
                                d12_device_from_iface)

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

   /* Nothing may be created on a removed device. */
   if (device->removed)
      return DXGI_ERROR_DEVICE_REMOVED;

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

   struct d12_cmd_allocator *d12_allocator =
      d12_cmd_allocator_from_iface(allocator);
   if (d12_allocator->type != type)
      return E_INVALIDARG;
   /*
    * The INITIAL PIPELINE STATE. A list created with one is already bound
    * to it and may draw without any SetPipelineState call, which is
    * exactly what test_command_list_initial_pipeline_state does. This was
    * dropped with a note saying PSOs did not exist yet -- a comment that
    * went stale the moment they did, leaving a legal draw failing with
    * "Draw without a graphics pipeline state".
    *
    * d12_list_Reset already honours its own initial_state argument, so the
    * two entry points now agree.
    */
   HRESULT hr = d12_cmd_list_create(device, type,
                                    d12_allocator, riid, out);

   if (SUCCEEDED(hr) && initial_state && out && *out) {
      struct d12_cmd_list *list = (struct d12_cmd_list *)*out;
      list->pso = d12_pipeline_state_from_iface(initial_state);
   }

   return hr;
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

   /* A feature-query structure being understood is distinct from the
    * optional feature being implemented. Return a valid, zero-capability
    * answer for the structures this runtime exposes instead of E_INVALIDARG:
    * applications use the fields to choose their fallback paths.
    */
   case D3D12_FEATURE_D3D12_OPTIONS1: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS1 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;
      memset(opts, 0, sizeof(*opts));
      return S_OK;
   }

   /* These optional capability structures contain only output fields whose
    * zero spelling means unsupported/tier 0.  Understanding the query is not
    * a capability claim: returning E_INVALIDARG here prevents applications
    * from taking their specified fallback paths.  Keep the answers
    * conservative until the corresponding command/shader path is complete.
    */
#define D12_ZERO_OPTIONS(N)                                                   \
   case D3D12_FEATURE_D3D12_OPTIONS##N: {                                    \
      D3D12_FEATURE_DATA_D3D12_OPTIONS##N *opts = data;                      \
      if (data_size != sizeof(*opts))                                        \
         return E_INVALIDARG;                                                \
      memset(opts, 0, sizeof(*opts));                                        \
      return S_OK;                                                           \
   }

   D12_ZERO_OPTIONS(2)
   D12_ZERO_OPTIONS(3)
   D12_ZERO_OPTIONS(6)
   D12_ZERO_OPTIONS(8)
   D12_ZERO_OPTIONS(10)
   D12_ZERO_OPTIONS(14)
   D12_ZERO_OPTIONS(15)
   D12_ZERO_OPTIONS(16)
   D12_ZERO_OPTIONS(17)
   D12_ZERO_OPTIONS(18)
   D12_ZERO_OPTIONS(20)

#undef D12_ZERO_OPTIONS

   case D3D12_FEATURE_D3D12_OPTIONS4: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS4 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;

      memset(opts, 0, sizeof(*opts));
      /* Apple AGX has native 16-bit floating-point ALU support and both the
       * DXIL front end and AGX compiler retain f16 arithmetic. */
      opts->Native16BitShaderOpsSupported = TRUE;
      return S_OK;
   }

   case D3D12_FEATURE_D3D12_OPTIONS12: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS12 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;

      memset(opts, 0, sizeof(*opts));
      /* This counter convention is not known because mesh shaders and their
       * pipeline statistics are not exposed.  UNKNOWN is the conservative
       * public spelling, unlike zero/FALSE which would assert a convention.
       */
      opts->MSPrimitivesPipelineStatisticIncludesCulledPrimitives =
         D3D12_TRI_STATE_UNKNOWN;
      return S_OK;
   }

   case D3D12_FEATURE_D3D12_OPTIONS7: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS7 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;
      memset(opts, 0, sizeof(*opts));
      opts->MeshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
      opts->SamplerFeedbackTier = D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
      return S_OK;
   }

   case D3D12_FEATURE_D3D12_OPTIONS9: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS9 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;
      memset(opts, 0, sizeof(*opts));
      return S_OK;
   }

   case D3D12_FEATURE_D3D12_OPTIONS11: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS11 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;
      memset(opts, 0, sizeof(*opts));
      return S_OK;
   }

   case D3D12_FEATURE_D3D12_OPTIONS13: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS13 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;
      memset(opts, 0, sizeof(*opts));
      return S_OK;
   }

   case D3D12_FEATURE_D3D12_OPTIONS19: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS19 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;

      /* We understand the query but do not yet expose RasterizerDesc2.
       * A zero capability answer makes applications take the supported
       * legacy rasterizer path instead of mistaking E_INVALIDARG for an
       * unknown runtime interface.
       */
      memset(opts, 0, sizeof(*opts));
      return S_OK;
   }

   case D3D12_FEATURE_PREDICATION:
      if (data_size != sizeof(D3D12_FEATURE_DATA_PREDICATION))
         return E_INVALIDARG;
      return E_NOTIMPL;

   case D3D12_FEATURE_HARDWARE_COPY:
      if (data_size != sizeof(D3D12_FEATURE_DATA_HARDWARE_COPY))
         return E_INVALIDARG;
      return E_NOTIMPL;

   case D3D12_FEATURE_APPLICATION_SPECIFIC_DRIVER_STATE: {
      D3D12_FEATURE_DATA_APPLICATION_SPECIFIC_DRIVER_STATE *state = data;
      if (data_size != sizeof(*state))
         return E_INVALIDARG;
      state->Supported = FALSE;
      return S_OK;
   }

   case D3D12_FEATURE_BYTECODE_BYPASS_HASH_SUPPORTED: {
      D3D12_FEATURE_DATA_BYTECODE_BYPASS_HASH_SUPPORTED *bypass = data;
      if (data_size != sizeof(*bypass))
         return E_INVALIDARG;
      bypass->Supported = FALSE;
      return S_OK;
   }

   case D3D12_FEATURE_SHADER_CACHE_ABI_SUPPORT:
      /* The Windows ABI's WCHAR is 16-bit, while the Linux DirectX-Headers
       * stub currently typedefs it to 32-bit wchar_t. This query returns no
       * data when unsupported, so accept either public structure spelling.
       */
      if (data_size != sizeof(D3D12_FEATURE_DATA_SHADERCACHE_ABI_SUPPORT) &&
          data_size != 128 * sizeof(uint16_t) + 4 * sizeof(uint64_t))
         return E_INVALIDARG;
      return E_FAIL;

   case D3D12_FEATURE_D3D12_OPTIONS22: {
      D3D12_FEATURE_DATA_D3D12_OPTIONS22 *opts = data;
      if (data_size != sizeof(*opts))
         return E_INVALIDARG;
      memset(opts, 0, sizeof(*opts));
      opts->Max1DDispatchSize =
         D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
      return S_OK;
   }

   case D3D12_FEATURE_BARRIER_LAYOUT: {
      D3D12_FEATURE_DATA_BARRIER_LAYOUT *layout = data;
      if (data_size != sizeof(*layout))
         return E_INVALIDARG;

      bool direct = layout->CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT;
      bool compute =
         layout->CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE;

      /* D3D12_BARRIER_LAYOUT_VIDEO_QUEUE_COMMON (value 30) was added after
       * the DirectX-Headers version this build uses. Keep the public value
       * here so callers built against the current header get the right
       * queue/layout validation answer.
       */
      if ((unsigned)layout->Layout == 30) {
         layout->Supported =
            layout->CommandListType == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE ||
            layout->CommandListType == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS ||
            layout->CommandListType == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
         return S_OK;
      }

      switch (layout->Layout) {
      case D3D12_BARRIER_LAYOUT_COMMON:
      case D3D12_BARRIER_LAYOUT_UNDEFINED:
         layout->Supported = TRUE;
         break;

      case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON:
      case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:
      case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS:
      case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE:
      case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE:
      case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST:
      case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
      case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
      case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
      case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
      case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
      case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:
         layout->Supported = direct;
         break;

      case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON:
      case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ:
      case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS:
      case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE:
      case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE:
      case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST:
         layout->Supported = compute;
         break;

      case D3D12_BARRIER_LAYOUT_GENERIC_READ:
      case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
      case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
      case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
      case D3D12_BARRIER_LAYOUT_COPY_DEST:
         layout->Supported = direct || compute;
         break;

      case D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_READ:
      case D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_WRITE:
         layout->Supported = layout->CommandListType ==
                             D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS;
         break;

      case D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_READ:
      case D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_WRITE:
         layout->Supported = layout->CommandListType ==
                             D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
         break;

      case D3D12_BARRIER_LAYOUT_VIDEO_DECODE_READ:
      case D3D12_BARRIER_LAYOUT_VIDEO_DECODE_WRITE:
         layout->Supported = layout->CommandListType ==
                             D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
         break;

      default:
         layout->Supported = FALSE;
         break;
      }

      return S_OK;
   }

   case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT: {
      D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT *va = data;
      if (data_size != sizeof(*va))
         return E_INVALIDARG;

      /* AGX addresses are 64-bit; agx_bo VAs come out of a 40-bit space,
       * and a resource cannot exceed what one allocation can cover. */
      va->MaxGPUVirtualAddressBitsPerResource = 40;
      va->MaxGPUVirtualAddressBitsPerProcess = 40;
      return S_OK;
   }

   case D3D12_FEATURE_ROOT_SIGNATURE: {
      D3D12_FEATURE_DATA_ROOT_SIGNATURE *rs = data;
      if (data_size != sizeof(*rs))
         return E_INVALIDARG;

      /*
       * This is a REQUEST/REPLY field: the caller writes the highest
       * version it can use and the runtime writes back the highest it
       * supports, capped by that. d12_root_sig parses both 1.0 and 1.1
       * blobs (the version-2 range offset is handled there), so 1.1 is
       * honest.
       */
      if (rs->HighestVersion != D3D_ROOT_SIGNATURE_VERSION_1_0 &&
          rs->HighestVersion != D3D_ROOT_SIGNATURE_VERSION_1_1) {
         /* An unrecognised version is refused and the field is LEFT
          * UNTOUCHED -- the suite checks that 0 and 0xdeadbeef both come
          * back exactly as passed. Clamping them would look like a
          * successful negotiation of a version the caller never named. */
         return E_INVALIDARG;
      }

      /* Both 1.0 and 1.1 parse (d12_root_sig handles the version-2 range
       * layout), so whatever the caller can use, we support. */
      return S_OK;
   }

   case D3D12_FEATURE_FORMAT_SUPPORT: {
      D3D12_FEATURE_DATA_FORMAT_SUPPORT *support = data;
      if (data_size != sizeof(*support))
         return E_INVALIDARG;

      support->Support1 = D3D12_FORMAT_SUPPORT1_NONE;
      support->Support2 = D3D12_FORMAT_SUPPORT2_NONE;

      /* UNKNOWN is the untyped/raw buffer spelling, which this driver
       * supports. It is not an image format to look up in the format table.
       */
      if (support->Format == DXGI_FORMAT_UNKNOWN) {
         support->Support1 = D3D12_FORMAT_SUPPORT1_BUFFER;
         return S_OK;
      }

      /* Processing the query successfully is distinct from supporting any
       * particular use of the format: Support1/Support2 are the capability
       * answer. Keep those bits conservative until each is derived from its
       * resource/view path, but do not report a well-formed query itself as
       * invalid merely because the answer is "no supported uses".
       *
       * Values 116..129 are the reserved hole in DXGI_FORMAT. Values 133..188
       * are the ASTC range understood by current D3D runtimes even though the
       * public enum leaves them unnamed, so they remain valid negative
       * queries here.
       */
      if ((UINT)support->Format > (UINT)DXGI_FORMAT_A4B4G4R4_UNORM ||
          (support->Format > DXGI_FORMAT_B4G4R4A4_UNORM &&
           support->Format < DXGI_FORMAT_P208))
         return E_INVALIDARG;

      return S_OK;
   }

   case D3D12_FEATURE_FORMAT_INFO: {
      D3D12_FEATURE_DATA_FORMAT_INFO *info = data;
      if (data_size != sizeof(*info))
         return E_INVALIDARG;

      /*
       * PLANE COUNT. Every format is one plane except a depth format that
       * also carries stencil -- on AGX those are physically separate
       * allocations, which is precisely why d12_format_stencil_plane
       * exists, so the answer is derived from the same source the resource
       * path uses rather than from a second table.
       *
       * DXGI_FORMAT_UNKNOWN is legal here and answers 1; a format we cannot
       * express at all is E_INVALIDARG, since a plane count for it would be
       * an invention.
       */
      if (info->Format == DXGI_FORMAT_UNKNOWN) {
         info->PlaneCount = 1;
         return S_OK;
      }

      /* 66 is an unassigned hole in the DXGI enum -- not a format with no
       * planes, but no format at all, so it is refused rather than
       * answered. */
      if (info->Format == (DXGI_FORMAT)66)
         return E_INVALIDARG;

      /*
       * The PLANE-VIEW formats of a depth/stencil pair. These name one
       * plane of a two-plane resource -- R32_FLOAT_X8X24_TYPELESS is its
       * depth half, X32_TYPELESS_G8X24_UINT its stencil half -- so they
       * describe a 2-plane format even though neither is itself a
       * depth/stencil format this driver can allocate. They are not in
       * d12_format_stencil_plane's table for exactly that reason, so they
       * are listed here rather than by widening a table used for
       * allocation.
       */
      switch (info->Format) {
      case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
      case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
      case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
      case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
         info->PlaneCount = 2;
         return S_OK;
      default:
         break;
      }

      /*
       * Everything else is single-plane, INCLUDING formats this driver
       * cannot allocate. Plane count is a property of the FORMAT, not of
       * our support for it -- refusing the query for a format we happen not
       * to implement was the wrong model, and the suite caught it by asking
       * about formats we do not map and expecting 1.
       */
      if (d12_format_stencil_plane(info->Format) != PIPE_FORMAT_NONE)
         info->PlaneCount = 2;
      else
         info->PlaneCount = 1;

      return S_OK;
   }

   case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS: {
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS *levels = data;
      if (data_size != sizeof(*levels))
         return E_INVALIDARG;

      levels->NumQualityLevels = 0;

      /* A zero sample count is a malformed query.  One sample is the
       * ordinary non-MSAA layout and therefore has one quality level for
       * every DXGI format spelling, independent of image-format support.
       * AGX and the resource path currently expose the hardware's 2x and
       * 4x modes; larger powers of two remain conservatively unsupported.
       */
      if (!levels->SampleCount)
         return E_FAIL;

      if (levels->SampleCount == 1) {
         levels->NumQualityLevels = 1;
      } else if (levels->Format != DXGI_FORMAT_UNKNOWN &&
                 (levels->SampleCount == 2 || levels->SampleCount == 4) &&
                 (d12_format_to_pipe(levels->Format) != PIPE_FORMAT_NONE ||
                  d12_format_depth_plane(levels->Format) != PIPE_FORMAT_NONE)) {
         levels->NumQualityLevels = 1;
      }

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

/* The root-signature parser writes a canonical, zero-initialized layout, so
 * comparing the populated arrays is both padding-safe and stricter than just
 * comparing the binding map. The latter would miss flags and static-sampler
 * state that participate in pipeline compatibility.
 */
static bool
d12_root_layout_equal(const struct d12_root_layout *a,
                      const struct d12_root_layout *b)
{
   if (a->num_params != b->num_params ||
       a->num_static_samplers != b->num_static_samplers ||
       a->flags != b->flags ||
       memcmp(a->params, b->params, sizeof(a->params)))
      return false;

   return !a->num_static_samplers ||
          !memcmp(a->static_samplers, b->static_samplers,
                  a->num_static_samplers * sizeof(*a->static_samplers));
}

/* When the caller leaves pRootSignature NULL, D3D12 derives the pipeline
 * layout from a root signature embedded in any participating shader. More
 * than one shader may carry the signature, but all embedded copies must
 * describe the same layout. Stages without an RTS0 part are allowed, and if
 * no stage embeds one the existing empty-map behavior remains available to
 * resource-free shaders.
 */
static HRESULT
d12_embedded_pso_binding_map(const D3D12_SHADER_BYTECODE *const *stages,
                             unsigned stage_count,
                             struct d12_root_layout *owned_layout,
                             struct d12_binding_map *owned_map,
                             const struct d12_binding_map **out_map)
{
   memset(owned_layout, 0, sizeof(*owned_layout));
   memset(owned_map, 0, sizeof(*owned_map));
   bool found = false;

   for (unsigned i = 0; i < stage_count; i++) {
      const D3D12_SHADER_BYTECODE *stage = stages[i];
      if (!stage || !stage->pShaderBytecode || !stage->BytecodeLength ||
          !d12_root_signature_has_rts0(stage->pShaderBytecode,
                                       stage->BytecodeLength))
         continue;

      struct d12_root_layout parsed;
      if (!d12_root_signature_parse(stage->pShaderBytecode,
                                    stage->BytecodeLength, &parsed)) {
         d12_root_layout_cleanup(owned_layout);
         return E_INVALIDARG;
      }

      if (!found) {
         *owned_layout = parsed;
         found = true;
      } else {
         bool matches = d12_root_layout_equal(owned_layout, &parsed);
         d12_root_layout_cleanup(&parsed);
         if (!matches) {
            mesa_loge("pipeline shaders contain incompatible embedded root "
                      "signatures");
            d12_root_layout_cleanup(owned_layout);
            return E_INVALIDARG;
         }
      }
   }

   if (found) {
      d12_root_layout_build_map(owned_layout, owned_map);
      *out_map = owned_map;
   } else {
      *out_map = d12_pso_binding_map(NULL);
   }

   return S_OK;
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
   const struct d12_binding_map *map;
   struct d12_root_layout embedded_layout = {0};
   struct d12_binding_map embedded_map = {0};

   const struct d12_root_layout *layout;
   if (desc->pRootSignature) {
      map = d12_pso_binding_map(desc->pRootSignature);
      layout = &((struct d12_root_signature *)desc->pRootSignature)->layout;
   } else {
      const D3D12_SHADER_BYTECODE *stages[] = {&desc->CS};
      HRESULT hr = d12_embedded_pso_binding_map(
         stages, ARRAY_SIZE(stages), &embedded_layout, &embedded_map, &map);
      if (FAILED(hr))
         return hr;
      layout = &embedded_layout;
   }

   /*
    * A cached PSO blob is driver-private.  Keep it deliberately transparent:
    * an identifying header followed by the original shader bytes and the
    * canonical parsed root layout.  Exact byte comparison rejects a cache
    * paired with different shader code or a different root signature without
    * relying on collision-prone hashes.  We still recompile after validation;
    * the blob is a correctness contract first, an optimization later.
    */
   struct d12_compute_cache_header {
      char magic[8];
      uint32_t version;
      uint32_t shader_size;
      uint32_t num_params;
      uint32_t num_static_samplers;
      uint32_t flags;
   } header = {
      .magic = {'D', '1', '2', 'A', 'G', 'X', 'C', 'S'},
      .version = 1,
      .shader_size = desc->CS.BytecodeLength,
      .num_params = layout->num_params,
      .num_static_samplers = layout->num_static_samplers,
      .flags = layout->flags,
   };

   if (desc->CS.BytecodeLength > UINT32_MAX ||
       layout->num_params > D12_MAX_ROOT_PARAMS) {
      d12_root_layout_cleanup(&embedded_layout);
      return E_INVALIDARG;
   }

   size_t params_size = layout->num_params * sizeof(layout->params[0]);
   size_t samplers_size =
      layout->num_static_samplers * sizeof(layout->static_samplers[0]);
   if (desc->CS.BytecodeLength >
       SIZE_MAX - sizeof(header) - params_size - samplers_size) {
      d12_root_layout_cleanup(&embedded_layout);
      return E_OUTOFMEMORY;
   }

   size_t cache_size = sizeof(header) + desc->CS.BytecodeLength +
                       params_size + samplers_size;
   uint8_t *cache = malloc(cache_size);
   if (!cache) {
      d12_root_layout_cleanup(&embedded_layout);
      return E_OUTOFMEMORY;
   }

   uint8_t *cursor = cache;
   memcpy(cursor, &header, sizeof(header));
   cursor += sizeof(header);
   memcpy(cursor, desc->CS.pShaderBytecode, desc->CS.BytecodeLength);
   cursor += desc->CS.BytecodeLength;
   memcpy(cursor, layout->params, params_size);
   cursor += params_size;
   if (samplers_size)
      memcpy(cursor, layout->static_samplers, samplers_size);

   if (desc->CachedPSO.CachedBlobSizeInBytes &&
       (!desc->CachedPSO.pCachedBlob ||
        desc->CachedPSO.CachedBlobSizeInBytes != cache_size ||
        memcmp(desc->CachedPSO.pCachedBlob, cache, cache_size))) {
      free(cache);
      d12_root_layout_cleanup(&embedded_layout);
      return E_INVALIDARG;
   }

   HRESULT hr = d12_pipeline_state_create_compute(
      device, desc->CS.pShaderBytecode, desc->CS.BytecodeLength, map, riid,
      out);
   if (SUCCEEDED(hr) && out && *out) {
      struct d12_pipeline_state *pso = *(void **)out;
      pso->cached_blob = cache;
      pso->cached_blob_size = cache_size;
      cache = NULL;
   }
   free(cache);
   d12_root_layout_cleanup(&embedded_layout);
   return hr;
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
   const D3D12_BLEND_DESC *blend;
   const D3D12_INPUT_LAYOUT_DESC *input_layout;
   const D3D12_STREAM_OUTPUT_DESC *stream_output;
   const D3D12_DEPTH_STENCIL_DESC *ds_state;
   struct D3D12_RT_FORMAT_ARRAY rt_formats;
   DXGI_SAMPLE_DESC sample_desc;
   UINT sample_mask;
   DXGI_FORMAT dsv_format;
   D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology;
   D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut;
   bool have_rt_formats;
   bool have_sample_desc;
   bool have_sample_mask;
};

static bool
d12_parse_pso_stream(const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                     struct d12_pso_stream *out)
{
   const uint8_t *p = (const uint8_t *)desc->pPipelineStateSubobjectStream;
   const uint8_t *end = p + desc->SizeInBytes;
   uint64_t seen = 0;

   memset(out, 0, sizeof(*out));

   while (p + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= end) {
      D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
      memcpy(&type, p, sizeof(type));

      if ((unsigned)type >= 64 || (seen & BITFIELD64_BIT((unsigned)type))) {
         mesa_loge("CreatePipelineState: duplicate or invalid stream "
                   "subobject %d", (int)type);
         return false;
      }
      seen |= BITFIELD64_BIT((unsigned)type);

      const uint8_t *body = NULL;
      size_t body_offset = 0;
      size_t body_size;

#define BODY(ctype)                                                            \
   do {                                                                        \
      body_offset = ALIGN_POT(sizeof(type), _Alignof(ctype));                  \
      body = p + body_offset;                                                  \
      body_size = sizeof(ctype);                                               \
      if (body > end || (size_t)(end - body) < body_size)                     \
         return false;                                                         \
   } while (0)
#define SUBOBJ(TY, field, ctype)                                                \
   case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_##TY:                              \
      BODY(ctype);                                                             \
      out->field = (const ctype *)body;                                        \
      break

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

      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
         /* DESC1 appends DepthBoundsTestEnable; the prefix consumed by the
          * current raster path is byte-for-byte DESC. */
         BODY(D3D12_DEPTH_STENCIL_DESC1);
         out->ds_state = (const D3D12_DEPTH_STENCIL_DESC *)body;
         break;

      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
         BODY(ID3D12RootSignature *);
         memcpy(&out->root_sig, body, sizeof(out->root_sig));
         break;

      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
         BODY(struct D3D12_RT_FORMAT_ARRAY);
         memcpy(&out->rt_formats, body, sizeof(out->rt_formats));
         out->have_rt_formats = true;
         break;

      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
         BODY(DXGI_FORMAT);
         memcpy(&out->dsv_format, body, sizeof(out->dsv_format));
         break;

      /* Accepted and ignored: none of these change what we build. */
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
         BODY(UINT);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
         BODY(D3D12_PIPELINE_STATE_FLAGS);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
         BODY(UINT);
         memcpy(&out->sample_mask, body, sizeof(out->sample_mask));
         out->have_sample_mask = true;
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
         BODY(DXGI_SAMPLE_DESC);
         memcpy(&out->sample_desc, body, sizeof(out->sample_desc));
         out->have_sample_desc = true;
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
         BODY(D3D12_PRIMITIVE_TOPOLOGY_TYPE);
         memcpy(&out->primitive_topology, body,
                sizeof(out->primitive_topology));
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
         BODY(D3D12_STREAM_OUTPUT_DESC);
         out->stream_output = (const D3D12_STREAM_OUTPUT_DESC *)body;
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
         BODY(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);
         memcpy(&out->strip_cut, body, sizeof(out->strip_cut));
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
         BODY(D3D12_VIEW_INSTANCING_DESC);
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
         BODY(D3D12_BLEND_DESC);
         out->blend = (const D3D12_BLEND_DESC *)body;
         break;
      case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
         BODY(D3D12_CACHED_PIPELINE_STATE);
         break;

      default:
         mesa_loge("CreatePipelineState: unknown stream subobject %d; the "
                   "stream carries no length so it cannot be stepped over",
                   (int)type);
         return false;
      }
#undef SUBOBJ
#undef BODY

      p += ALIGN_POT(body_offset + body_size, sizeof(void *));
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

   /* A compute stream may carry ignored graphics defaults, but a vertex
    * shader selects the graphics pipeline class and cannot coexist with CS. */
   if (st.cs && st.cs->pShaderBytecode &&
       st.vs && st.vs->pShaderBytecode)
      return E_INVALIDARG;

   enum pipe_format rt_format = PIPE_FORMAT_NONE;
   if (st.have_rt_formats && st.rt_formats.NumRenderTargets >= 1 &&
       d12_format_is_renderable(st.rt_formats.RTFormats[0]))
      rt_format = d12_format_to_pipe(st.rt_formats.RTFormats[0]);

   DXGI_FORMAT dsv_format = st.dsv_format;
   if (dsv_format != DXGI_FORMAT_UNKNOWN &&
       !d12_format_is_depth_stencil(dsv_format))
      dsv_format = DXGI_FORMAT_UNKNOWN;

   unsigned nr_samples = st.have_sample_desc ? st.sample_desc.Count : 1;
   UINT sample_mask = st.have_sample_mask ? st.sample_mask : ~0u;
   if ((nr_samples != 1 && nr_samples != 2 && nr_samples != 4) ||
       (st.have_sample_desc && st.sample_desc.Quality != 0))
      return E_INVALIDARG;

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
         st.ps->BytecodeLength, rt_format,
         st.have_rt_formats ? st.rt_formats.NumRenderTargets : 0,
         st.rt_formats.RTFormats, nr_samples, sample_mask, st.ds_state,
         dsv_format,
         st.raster,
         st.blend, map, riid, out);
   }

   if (st.vs && st.vs->pShaderBytecode) {
      return d12_pipeline_state_create_graphics(
         device, st.vs->pShaderBytecode, st.vs->BytecodeLength,
         st.ps ? st.ps->pShaderBytecode : NULL,
         st.ps ? st.ps->BytecodeLength : 0,
         st.gs ? st.gs->pShaderBytecode : NULL,
         st.gs ? st.gs->BytecodeLength : 0,
         st.hs ? st.hs->pShaderBytecode : NULL,
         st.hs ? st.hs->BytecodeLength : 0,
         st.ds ? st.ds->pShaderBytecode : NULL,
         st.ds ? st.ds->BytecodeLength : 0, rt_format,
         st.have_rt_formats ? st.rt_formats.NumRenderTargets : 0,
         st.rt_formats.RTFormats, nr_samples, sample_mask, st.ds_state,
         dsv_format,
         st.raster,
         st.blend, st.input_layout, st.stream_output, st.primitive_topology,
         st.strip_cut, map, riid, out);
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

   if (!desc || !desc->VS.pShaderBytecode)
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

   if (desc->NumRenderTargets > ARRAY_SIZE(desc->RTVFormats))
      return E_INVALIDARG;

   if ((desc->SampleDesc.Count != 1 && desc->SampleDesc.Count != 2 &&
        desc->SampleDesc.Count != 4) || desc->SampleDesc.Quality != 0)
      return E_INVALIDARG;

   /* Logic operations use one common output-merger operation. They cannot be
    * combined with independent per-target blend state on an active target.
    */
   if (desc->BlendState.IndependentBlendEnable) {
      for (unsigned i = 0; i < desc->NumRenderTargets; i++) {
         if (desc->BlendState.RenderTarget[i].LogicOpEnable)
            return E_INVALIDARG;
      }
   }

   /* Inactive RTV slots are not spare metadata storage: D3D12 requires them
    * to retain the UNKNOWN spelling beyond NumRenderTargets.
    */
   for (unsigned i = desc->NumRenderTargets;
        i < ARRAY_SIZE(desc->RTVFormats); i++) {
      if (desc->RTVFormats[i] != DXGI_FORMAT_UNKNOWN)
         return E_INVALIDARG;
   }

   /* Stream-output declarations are legal only when the root signature opts
    * in. The execution path remains explicitly unsupported, but accepting a
    * PSO that the signature itself forbids is an API validation bug.
    */
   if (desc->StreamOutput.NumEntries &&
       (!desc->pRootSignature ||
        !(((struct d12_root_signature *)desc->pRootSignature)->layout.flags &
          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT)))
      return E_INVALIDARG;

   /* The pipeline's declared render-target format now drives the
    * tilebuffer layout instead of an assumed RGBA8.
   */
   enum pipe_format rt_format = PIPE_FORMAT_NONE;
   if (desc->NumRenderTargets >= 1 &&
       d12_format_is_renderable(desc->RTVFormats[0]))
      rt_format = d12_format_to_pipe(desc->RTVFormats[0]);

   DXGI_FORMAT dsv_format = desc->DSVFormat;
   if (dsv_format != DXGI_FORMAT_UNKNOWN &&
       !d12_format_is_depth_stencil(dsv_format))
      dsv_format = DXGI_FORMAT_UNKNOWN;

   const struct d12_binding_map *map;
   struct d12_root_layout embedded_layout = {0};
   struct d12_binding_map embedded_map = {0};

   if (desc->pRootSignature) {
      map = d12_pso_binding_map(desc->pRootSignature);
   } else {
      const D3D12_SHADER_BYTECODE *stages[] = {
         &desc->VS, &desc->PS, &desc->GS, &desc->HS, &desc->DS,
      };
      HRESULT hr = d12_embedded_pso_binding_map(
         stages, ARRAY_SIZE(stages), &embedded_layout, &embedded_map, &map);
      if (FAILED(hr))
         return hr;
   }

   HRESULT hr = d12_pipeline_state_create_graphics(
      device, desc->VS.pShaderBytecode, desc->VS.BytecodeLength,
      desc->PS.pShaderBytecode, desc->PS.BytecodeLength,
      desc->GS.pShaderBytecode, desc->GS.BytecodeLength,
      desc->HS.pShaderBytecode, desc->HS.BytecodeLength,
      desc->DS.pShaderBytecode, desc->DS.BytecodeLength, rt_format,
      desc->NumRenderTargets, desc->RTVFormats, desc->SampleDesc.Count,
      desc->SampleMask, &desc->DepthStencilState,
      dsv_format, &desc->RasterizerState, &desc->BlendState,
      &desc->InputLayout, &desc->StreamOutput, desc->PrimitiveTopologyType,
      desc->IBStripCutValue, map, riid, out);
   d12_root_layout_cleanup(&embedded_layout);
   return hr;
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

/*
 * D3D12_RESOURCE_ALLOCATION_INFO for a set of resource descriptions.
 *
 * This is a pure query -- no allocation happens -- but it has to agree with
 * what CreateCommittedResource would actually allocate, so it runs the same
 * ail layout maths rather than a second estimate that could drift.
 *
 * It was the single largest one-blocker cluster in the suite: 11 tests where
 * an unimplemented GetResourceAllocationInfo was the ONLY thing standing
 * between them and a verdict. The stub returned {0, 0}, and a zero size is
 * not a refusal an application can see -- it silently produces a heap too
 * small for the resource it was sized for.
 *
 * Alignment follows D3D12's documented defaults: 4 MB for multisampled
 * resources, 64 KB otherwise. Buffers are their own byte length, aligned to
 * 64 KB. Sizes accumulate with each resource aligned to its own requirement,
 * which is what a caller placing them consecutively in one heap needs.
 */
/*
 * ABI NOTE, and this cost a segfault to find. Aggregates larger than a
 * register are returned through an explicit RetVal POINTER here, not by
 * value: the callee writes the struct through arg 1 and returns that same
 * pointer. That is widl's WIDL_EXPLICIT_AGGREGATE_RETURNS convention, which
 * the vkd3d test binary is built with (_agxtest/gen/vkd3d_d3d12.h defines
 * it) and which matches what MSVC does on Windows for a struct this size.
 *
 * DirectX-Headers, which this driver compiles against, declares the
 * non-_WIN32 branch as returning by value, so writing the obvious C
 * function produces a vtable slot the caller cannot use: the caller reads
 * the return value as a pointer and dereferences it. With SizeInBytes = 0
 * in x0 that is a null dereference, which is exactly how
 * test_resource_allocation_info crashed -- inside the TEST, one instruction
 * after the call (`ldp x3, x21, [x0]`), which is why it looked like a test
 * bug rather than ours.
 *
 * Only GetResourceAllocationInfo and GetCustomHeapProperties take this
 * form among the methods we implement; GetDesc and the descriptor-handle
 * getters really are by-value, which is why they have always worked. The
 * slot is assigned with a cast because the header's prototype disagrees.
 */
static D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE
d12_device_GetResourceAllocationInfo(ID3D12Device2 *iface,
                                     D3D12_RESOURCE_ALLOCATION_INFO *ret,
                                     UINT visible_mask, UINT count,
                                     const D3D12_RESOURCE_DESC *descs)
{
   D3D12_RESOURCE_ALLOCATION_INFO info = {0, 0};

   /*
    * An EMPTY query is not an invalid one. D3D12 answers count == 0 with
    * {SizeInBytes = 0, Alignment = 1}, which test_resource_allocation_info
    * asserts directly. UINT64_MAX is the answer for a description that
    * cannot be allocated, not for asking about nothing -- I had those two
    * cases conflated.
    */
   if (!count) {
      info.SizeInBytes = 0;
      info.Alignment = 1;
      if (ret)
         *ret = info;
      return ret;
   }

   if (!descs) {
      info.SizeInBytes = UINT64_MAX;
      info.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      if (ret)
         *ret = info;
      return ret;
   }

   uint64_t total = 0;
   uint64_t max_align = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

   for (UINT i = 0; i < count; i++) {
      const D3D12_RESOURCE_DESC *d = &descs[i];
      uint64_t align = d->SampleDesc.Count > 1
                          ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT
                          : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      uint64_t size;
      uint64_t slice0_B = 0;

      /*
       * desc->Alignment is an INPUT: the caller may REQUEST an alignment,
       * and D3D12 refuses one it cannot honour by answering
       * {UINT64_MAX, default}. 0 means "pick for me".
       *
       * test_resource_allocation_info asserts this directly, including that
       * a small-alignment (4 KB) request is refused for a texture too large
       * to be placed that way. Ignoring the field -- which is what this did
       * at first -- turns every one of those refusals into a plausible
       * number the caller then trusts.
       */
      bool want_small =
         d->Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT &&
         d->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER &&
         d->SampleDesc.Count <= 1 &&
         !(d->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                       D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));

      /*
       * MSAA defaults to 4 MB placement, but an application MAY explicitly
       * ask for the ordinary 64 KB and D3D12 honours it -- that is the
       * "workaround" test_placed_msaa_alignment_workaround checks, and it
       * asserts both halves: Alignment == 0 gives 4 MB, an explicit 64 KB
       * request gives 64 KB. Treating 4 MB as mandatory refuses a legal
       * query.
       */
      if (d->SampleDesc.Count > 1 &&
          d->Alignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
         align = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

      /* Any other explicit request we cannot honour is refused outright.
       * The small-alignment request is decided AFTER the size is known,
       * because whether it qualifies depends on the size (below).
       */
      if (d->Alignment != 0 && d->Alignment != align && !want_small) {
         info.SizeInBytes = UINT64_MAX;
         info.Alignment = align;
         if (ret)
            *ret = info;
         return ret;
      }

      if (d->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
         size = d->Width;
      } else {
         /* Same layout the resource would really get. A format we cannot
          * express has no honest answer, so say so rather than inventing
          * a number the caller would then trust.
          */
         enum pipe_format pipe = d12_format_is_depth_stencil(d->Format)
                                    ? d12_format_depth_plane(d->Format)
                                    : d12_format_to_pipe(d->Format);
         if (pipe == PIPE_FORMAT_NONE) {
            /* A refusal still has to report a LEGAL alignment: the caller
             * feeds it straight into align(), which asserts on zero. The
             * test checks `Alignment >= D3D12_DEFAULT_RESOURCE_PLACEMENT_
             * ALIGNMENT` even on the UINT64_MAX path. */
            info.SizeInBytes = UINT64_MAX;
            info.Alignment = align;
            if (ret)
               *ret = info;
            return ret;
         }

         uint32_t base_w = (uint32_t)MAX2(d->Width, 1);
         uint32_t base_h = MAX2(d->Height, 1);
         uint32_t full_mip_levels = util_logbase2(MAX2(base_w, base_h)) + 1;

         struct ail_layout layout = {
            .width_px = (uint32_t)MAX2(d->Width, 1),
            .height_px = MAX2(d->Height, 1),
            .depth_px = MAX2(d->DepthOrArraySize, 1),
            .sample_count_sa = MAX2(d->SampleDesc.Count, 1),
            /* MipLevels == 0 means the FULL chain in D3D12, not one
             * level. Treating it as 1 under-reports the size of every
             * resource an application asks to be fully mipped. */
            .levels = d->MipLevels ? d->MipLevels : full_mip_levels,
            .tiling = AIL_TILING_TWIDDLED,
            .format = pipe,
            .writeable_image =
               d->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
         };
         ail_make_miptree(&layout);
         size = layout.size_B;

         /* Block-aware size of ONE SLICE OF LEVEL 0; the small-placement
          * test below is on this, not on the total. */
         slice0_B = (uint64_t)util_format_get_nblocksx(pipe, base_w) *
                    util_format_get_nblocksy(pipe, base_h) *
                    util_format_get_blocksize(pipe);

         /* A depth format with a separate stencil plane costs both. */
         if (d12_format_is_depth_stencil(d->Format)) {
            enum pipe_format sfmt = d12_format_stencil_plane(d->Format);
            if (sfmt != PIPE_FORMAT_NONE) {
               struct ail_layout sl = layout;
               sl.format = sfmt;
               ail_make_miptree(&sl);
               size += sl.size_B;
            }
         }
      }

      /*
       * SMALL RESOURCE PLACEMENT. 4 KB alignment is only legal when the
       * resource actually fits the small-resource budget; D3D12 refuses the
       * request otherwise rather than quietly upgrading it. Deciding this
       * on the real layout size is what the guess-by-dimensions version got
       * wrong -- the suite checks a texture that is small in width and
       * height but too large once its format and mip chain are counted.
       */
      if (want_small) {
         /*
          * The 4 KB threshold is on ONE SLICE OF LEVEL 0, block-aware --
          * not on the total. The suite pins this precisely: 256x512 BC1 is
          * exactly 64 KB per slice and qualifies even with 10 mips or 64
          * array layers, while 260x512 BC1 (66.5 KB) and 1024x1024 RGBA8
          * do not. Testing the total size instead refuses the 64-layer and
          * 1024-layer cases, which is what the first version did.
          */
         if (slice0_B <= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) {
            align = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
         } else {
            info.SizeInBytes = UINT64_MAX;
            info.Alignment = align;
            if (ret)
               *ret = info;
            return ret;
         }
      }

      total = align64(total, align) + size;
      max_align = MAX2(max_align, align);
   }

   info.SizeInBytes = align64(total, max_align);
   info.Alignment = max_align;
   if (ret)
         *ret = info;
      return ret;
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateHeap(ID3D12Device2 *iface, const D3D12_HEAP_DESC *desc,
                      REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (out)
      *out = NULL;

   return d12_heap_create(device, desc, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreatePlacedResource(ID3D12Device2 *iface, ID3D12Heap *heap_iface,
                                UINT64 heap_offset,
                                const D3D12_RESOURCE_DESC *desc,
                                D3D12_RESOURCE_STATES initial_state,
                                const D3D12_CLEAR_VALUE *optimized_clear,
                                REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (out)
      *out = NULL;

   if (!heap_iface || !desc)
      return E_INVALIDARG;

   /* Resource STATES are not tracked: this driver has no transition
    * barriers to honour, and UMA gives no separate layouts to move
    * between. The optimized clear value is a hint we do not need. */
   return d12_resource_create_placed(device, d12_memheap_from_iface(heap_iface),
                                     heap_offset, desc, initial_state,
                                     optimized_clear, riid, out);
}

static struct d12_command_signature *
d12_command_signature_from_iface(ID3D12CommandSignature *iface)
{
   return (struct d12_command_signature *)iface;
}

static HRESULT STDMETHODCALLTYPE
d12_command_signature_QueryInterface(ID3D12CommandSignature *iface,
                                     REFIID riid, void **out)
{
   struct d12_command_signature *signature =
      d12_command_signature_from_iface(iface);

   if (!out)
      return E_POINTER;

   if (d12_guid_equal(riid, &IID_IUnknown) ||
       d12_guid_equal(riid, &IID_ID3D12Object) ||
       d12_guid_equal(riid, &IID_ID3D12DeviceChild) ||
       d12_guid_equal(riid, &IID_ID3D12Pageable) ||
       d12_guid_equal(riid, &IID_ID3D12CommandSignature)) {
      p_atomic_inc(&signature->refcount);
      *out = iface;
      return S_OK;
   }

   d12_log_unknown_riid("ID3D12CommandSignature::QueryInterface", riid);
   *out = NULL;
   return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d12_command_signature_AddRef(ID3D12CommandSignature *iface)
{
   return p_atomic_inc_return(
      &d12_command_signature_from_iface(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE
d12_command_signature_Release(ID3D12CommandSignature *iface)
{
   struct d12_command_signature *signature =
      d12_command_signature_from_iface(iface);
   uint32_t refs = p_atomic_dec_return(&signature->refcount);

   if (!refs) {
      d12_private_data_store_destroy(&signature->private_data);
      d12_device_release(signature->device);
      free(signature->arguments);
      free(signature);
   }

   return refs;
}

D12_DEFINE_PRIVATE_DATA_METHODS(d12_command_signature,
                                ID3D12CommandSignature,
                                d12_command_signature_from_iface)

static HRESULT STDMETHODCALLTYPE
d12_command_signature_GetDevice(ID3D12CommandSignature *iface, REFIID riid,
                                void **out)
{
   struct d12_command_signature *signature =
      d12_command_signature_from_iface(iface);
   return ID3D12Device_QueryInterface(&signature->device->iface, riid, out);
}

static const ID3D12CommandSignatureVtbl d12_command_signature_vtbl = {
   .QueryInterface = d12_command_signature_QueryInterface,
   .AddRef = d12_command_signature_AddRef,
   .Release = d12_command_signature_Release,
   .GetPrivateData = d12_command_signature_GetPrivateData,
   .SetPrivateData = d12_command_signature_SetPrivateData,
   .SetPrivateDataInterface = d12_command_signature_SetPrivateDataInterface,
   .SetName = d12_command_signature_SetName,
   .GetDevice = d12_command_signature_GetDevice,
};

/*
 * Command signatures retain their validated public description even though
 * ExecuteIndirect is not implemented yet.  This is a real pageable D3D12
 * object, not a success-only token: COM identity, device ownership, private
 * data, and argument lifetime all follow the ordinary object contract.
 *
 * DRAW, DRAW_INDEXED and DISPATCH are terminal.  A signature may place root
 * arguments before the one terminal command, but the terminal command must be
 * last and unique.
 */
static HRESULT STDMETHODCALLTYPE
d12_device_CreateCommandSignature(ID3D12Device2 *iface,
                                  const D3D12_COMMAND_SIGNATURE_DESC *desc,
                                  ID3D12RootSignature *root_signature,
                                  REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (!out)
      return E_POINTER;

   *out = NULL;

   if (!desc || !desc->NumArgumentDescs || !desc->pArgumentDescs)
      return E_INVALIDARG;

   bool saw_terminal = false;
   for (UINT i = 0; i < desc->NumArgumentDescs; i++) {
      D3D12_INDIRECT_ARGUMENT_TYPE type = desc->pArgumentDescs[i].Type;
      bool terminal = type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW ||
                      type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED ||
                      type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
      if (terminal) {
         if (saw_terminal || i + 1 != desc->NumArgumentDescs)
            return E_INVALIDARG;
         saw_terminal = true;
      }
   }

   if (!saw_terminal)
      return E_INVALIDARG;

   size_t packed_size = 0;
   for (UINT i = 0; i < desc->NumArgumentDescs; i++) {
      D3D12_INDIRECT_ARGUMENT_TYPE type = desc->pArgumentDescs[i].Type;

      bool terminal = type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW ||
                      type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED ||
                      type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

      if (terminal) {
         packed_size += type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
                           ? sizeof(D3D12_DRAW_ARGUMENTS)
                        : type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
                           ? sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)
                           : sizeof(D3D12_DISPATCH_ARGUMENTS);
      } else if (type == D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT) {
         if (!root_signature ||
             !desc->pArgumentDescs[i].Constant.Num32BitValuesToSet)
            return E_INVALIDARG;
         if (desc->pArgumentDescs[i].Constant.Num32BitValuesToSet >
             (SIZE_MAX - packed_size) / 4)
            return E_INVALIDARG;
         packed_size +=
            (size_t)desc->pArgumentDescs[i].Constant.Num32BitValuesToSet * 4;
      } else {
         /* Keep creation honest until ExecuteIndirect implements the other
          * mutable-state argument classes. */
         return E_NOTIMPL;
      }
   }

   if (desc->ByteStride < packed_size || (desc->ByteStride & 3))
      return E_INVALIDARG;

   struct d12_command_signature *signature = calloc(1, sizeof(*signature));
   if (!signature)
      return E_OUTOFMEMORY;

   signature->arguments =
      malloc(sizeof(*signature->arguments) * desc->NumArgumentDescs);
   if (!signature->arguments) {
      free(signature);
      return E_OUTOFMEMORY;
   }

   memcpy(signature->arguments, desc->pArgumentDescs,
          sizeof(*signature->arguments) * desc->NumArgumentDescs);
   signature->iface.lpVtbl =
      (ID3D12CommandSignatureVtbl *)&d12_command_signature_vtbl;
   signature->refcount = 1;
   signature->device = device;
   signature->desc = *desc;
   signature->desc.pArgumentDescs = signature->arguments;
   d12_private_data_store_init(&signature->private_data);
   d12_device_add_ref(device);

   HRESULT hr = d12_command_signature_QueryInterface(&signature->iface, riid,
                                                      out);
   d12_command_signature_Release(&signature->iface);
   return hr;
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

   memset(slot, 0, sizeof(*slot));

   if (!resource) {
      /* Null RTVs are legal placeholders; record an empty slot. */
      return;
   }

   struct d12_resource *res = d12_resource_from_iface(resource);

   /*
    * The VIEW's format wins over the resource's. That is not a preference:
    * a TYPELESS resource carries no interpretation at all, and D3D12
    * requires the view to supply one, so checking the resource format
    * rejects every typeless render target no matter what the application
    * asked for. DXGI_FORMAT_UNKNOWN in the desc means "inherit", and a
    * NULL desc means the same.
    */
   DXGI_FORMAT view_format = (desc && desc->Format != DXGI_FORMAT_UNKNOWN)
                                ? desc->Format
                                : res->desc.Format;

   /* Report WHICH half failed. Reporting the format for a dimension
    * mismatch sent a debugging session after a format table that was
    * already correct.
    */
   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
      mesa_loge("CreateRenderTargetView: dimension %d unsupported (need 2D), "
                "format %d", (int)res->desc.Dimension, (int)view_format);
      slot->resource = NULL;
      return;
   }

   if (!d12_format_is_renderable(view_format)) {
      mesa_loge("CreateRenderTargetView: format %d is not renderable",
                (int)view_format);
      slot->resource = NULL;
      return;
   }

   const unsigned samples = res->desc.SampleDesc.Count;
   if (samples != 1 && samples != 2 && samples != 4) {
      mesa_loge("CreateRenderTargetView: %u samples unsupported", samples);
      slot->resource = NULL;
      return;
   }

   /*
    * ARRAY SLICES. A TEXTURE2DARRAY view selects a consecutive layer range;
    * ail lays layers out contiguously at layer_stride_B, so the first slice
    * is just an offset and the view retains the range count. Refusing the
    * view dimension made every array test fail at the view even once the
    * resource itself was legal.
    */
   uint64_t slice_offset_B = 0;
   uint32_t view_layers = MAX2(res->desc.DepthOrArraySize, 1);

   if (desc) {
      if (samples > 1) {
         /* The multisample descriptor has no mip field: MSAA resources are
          * necessarily single-level. Keep arrays closed until their layer
          * addressing is validated independently. */
         if (desc->ViewDimension != D3D12_RTV_DIMENSION_TEXTURE2DMS) {
            mesa_loge("CreateRenderTargetView: multisample view dimension %d "
                      "unsupported", (int)desc->ViewDimension);
            slot->resource = NULL;
            return;
         }
      } else if (desc->ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY) {
         uint32_t first = desc->Texture2DArray.FirstArraySlice;
         uint32_t layers = MAX2(res->desc.DepthOrArraySize, 1);
         uint32_t count = desc->Texture2DArray.ArraySize;

         if (count == UINT32_MAX)
            count = layers - MIN2(first, layers);

         if (desc->Texture2DArray.MipSlice != 0 || first >= layers ||
             !count || count > layers - first) {
            mesa_loge("CreateRenderTargetView: array range %u+%u/%u mip %u "
                      "out of range", first, count, layers,
                      desc->Texture2DArray.MipSlice);
            slot->resource = NULL;
            return;
         }

         slice_offset_B = (uint64_t)first * res->layout.layer_stride_B;
         view_layers = count;
      } else if (desc->ViewDimension != D3D12_RTV_DIMENSION_TEXTURE2D) {
         mesa_loge("CreateRenderTargetView: view dimension %d unsupported",
                   (int)desc->ViewDimension);
         slot->resource = NULL;
         return;
      } else if (desc->Texture2D.MipSlice != 0) {
         mesa_loge("CreateRenderTargetView: mip slice %u unsupported",
                   desc->Texture2D.MipSlice);
         slot->resource = NULL;
         return;
      }

      if (desc->ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D ||
          desc->ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DMS)
         view_layers = 1;
   }

   slot->resource = res;
   /* The interpretation the shader and the clear path must both use --
    * storing the resource's typeless format here would push the same
    * "no interpretation" problem downstream to every consumer.
    */
   slot->format = view_format;
   slot->kind = D12_DESC_RTV;
   slot->address = res->addr + slice_offset_B;
   /* RTV descriptors are never shader-visible. Reuse the buffer-size field
    * for the number of consecutive array layers selected by the view. */
   slot->size_B = view_layers;
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

   memset(slot, 0, sizeof(*slot));

   if (!resource) {
      /* Null DSVs are legal placeholders; record an empty slot. */
      slot->resource = NULL;
      return;
   }

   struct d12_resource *res = d12_resource_from_iface(resource);

   const unsigned samples = MAX2(res->desc.SampleDesc.Count, 1);
   if (samples != 1 && samples != 2 && samples != 4) {
      mesa_loge("CreateDepthStencilView: invalid sample count %u", samples);
      slot->resource = NULL;
      return;
   }

   if (!res->is_depth_stencil) {
      mesa_loge("CreateDepthStencilView: resource format %d is not a "
                "depth/stencil format", (int)res->desc.Format);
      slot->resource = NULL;
      return;
   }

   const uint32_t resource_layers = MAX2(res->desc.DepthOrArraySize, 1);
   uint32_t first_layer = 0;
   /* A NULL descriptor requests the default view over every array slice. */
   uint32_t view_layers = desc ? 1 : resource_layers;
   if (desc) {
      if (samples > 1) {
         if (desc->ViewDimension != D3D12_DSV_DIMENSION_TEXTURE2DMS) {
            mesa_loge("CreateDepthStencilView: view dimension %d does not "
                      "match %u-sample resource",
                      (int)desc->ViewDimension, samples);
            slot->resource = NULL;
            return;
         }
      } else if (desc->ViewDimension ==
                 D3D12_DSV_DIMENSION_TEXTURE2DARRAY) {
         first_layer = desc->Texture2DArray.FirstArraySlice;
         view_layers = desc->Texture2DArray.ArraySize;
         if (view_layers == UINT32_MAX)
            view_layers = resource_layers - MIN2(first_layer,
                                                  resource_layers);

         if (desc->Texture2DArray.MipSlice != 0 ||
             first_layer >= resource_layers || !view_layers ||
             view_layers > resource_layers - first_layer) {
            mesa_loge("CreateDepthStencilView: array range %u+%u/%u mip %u "
                      "out of range", first_layer, view_layers,
                      resource_layers, desc->Texture2DArray.MipSlice);
            slot->resource = NULL;
            return;
         }
      } else if (desc->ViewDimension != D3D12_DSV_DIMENSION_TEXTURE2D) {
         mesa_loge("CreateDepthStencilView: view dimension %d does not match "
                   "single-sample resource", (int)desc->ViewDimension);
         slot->resource = NULL;
         return;
      } else if (desc->Texture2D.MipSlice != 0) {
         mesa_loge("CreateDepthStencilView: mip slice %u unsupported",
                   desc->Texture2D.MipSlice);
         slot->resource = NULL;
         return;
      }
   }

   const D3D12_DSV_FLAGS flags = desc ? desc->Flags : D3D12_DSV_FLAG_NONE;
   const D3D12_DSV_FLAGS valid = D3D12_DSV_FLAG_READ_ONLY_DEPTH |
                                 D3D12_DSV_FLAG_READ_ONLY_STENCIL;
   if (flags & ~valid) {
      mesa_loge("CreateDepthStencilView: invalid flags 0x%x",
                (unsigned)flags);
      slot->resource = NULL;
      return;
   }
   if ((flags & D3D12_DSV_FLAG_READ_ONLY_STENCIL) && !res->stencil_bo) {
      mesa_loge("CreateDepthStencilView: READ_ONLY_STENCIL on a format with "
                "no stencil plane");
      slot->resource = NULL;
      return;
   }

   slot->resource = res;
   slot->format = desc && desc->Format != DXGI_FORMAT_UNKNOWN
                     ? desc->Format
                     : res->desc.Format;
   slot->kind = D12_DESC_DSV;
   slot->dsv_flags = flags;
   slot->address =
      res->addr + (uint64_t)first_layer * res->layout.layer_stride_B;
   slot->counter_address = res->stencil_bo
      ? res->stencil_addr +
           (uint64_t)first_layer * res->stencil_layout.layer_stride_B
      : 0;
   /* CPU-only DSV descriptors reuse size_B for their selected layer count,
    * matching the established RTV representation. */
   slot->size_B = view_layers;
}

static enum agx_wrap
d12_sampler_wrap(D3D12_TEXTURE_ADDRESS_MODE mode)
{
   switch (mode) {
   case D3D12_TEXTURE_ADDRESS_MODE_WRAP:        return AGX_WRAP_REPEAT;
   case D3D12_TEXTURE_ADDRESS_MODE_MIRROR:      return AGX_WRAP_MIRRORED_REPEAT;
   case D3D12_TEXTURE_ADDRESS_MODE_CLAMP:       return AGX_WRAP_CLAMP_TO_EDGE;
   case D3D12_TEXTURE_ADDRESS_MODE_BORDER:      return AGX_WRAP_CLAMP_TO_BORDER;
   case D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE: return AGX_WRAP_MIRRORED_CLAMP_TO_EDGE;
   default:                                     return AGX_WRAP_CLAMP_TO_EDGE;
   }
}

static enum agx_compare_func
d12_sampler_compare(D3D12_COMPARISON_FUNC func)
{
   switch (func) {
   case D3D12_COMPARISON_FUNC_NEVER:         return AGX_COMPARE_FUNC_NEVER;
   case D3D12_COMPARISON_FUNC_LESS:          return AGX_COMPARE_FUNC_LESS;
   case D3D12_COMPARISON_FUNC_EQUAL:         return AGX_COMPARE_FUNC_EQUAL;
   case D3D12_COMPARISON_FUNC_LESS_EQUAL:    return AGX_COMPARE_FUNC_LEQUAL;
   case D3D12_COMPARISON_FUNC_GREATER:       return AGX_COMPARE_FUNC_GREATER;
   case D3D12_COMPARISON_FUNC_NOT_EQUAL:     return AGX_COMPARE_FUNC_NOT_EQUAL;
   case D3D12_COMPARISON_FUNC_GREATER_EQUAL: return AGX_COMPARE_FUNC_GEQUAL;
   case D3D12_COMPARISON_FUNC_ALWAYS:        return AGX_COMPARE_FUNC_ALWAYS;
   default:                                  return AGX_COMPARE_FUNC_NEVER;
   }
}

static enum agx_border_colour
d12_sampler_border(const D3D12_SAMPLER_DESC2 *desc)
{
   if (desc->Flags & D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR) {
      const UINT *color = desc->UintBorderColor;
      if (color[0] == 0 && color[1] == 0 && color[2] == 0)
         return color[3] == 1 ? AGX_BORDER_COLOUR_OPAQUE_BLACK
                              : AGX_BORDER_COLOUR_TRANSPARENT_BLACK;
      if (color[0] == 1 && color[1] == 1 && color[2] == 1 && color[3] == 1)
         return AGX_BORDER_COLOUR_OPAQUE_WHITE;
      return AGX_BORDER_COLOUR_TRANSPARENT_BLACK;
   }

   const FLOAT *color = desc->FloatBorderColor;
   if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f) {
      return color[3] == 1.0f ? AGX_BORDER_COLOUR_OPAQUE_BLACK
                              : AGX_BORDER_COLOUR_TRANSPARENT_BLACK;
   }

   if (color[0] == 1.0f && color[1] == 1.0f && color[2] == 1.0f &&
       color[3] == 1.0f)
      return AGX_BORDER_COLOUR_OPAQUE_WHITE;

   /* The exact value remains in sampler_border for a future custom-border
    * table.  Until that table is bound, use a deterministic safe fallback. */
   return AGX_BORDER_COLOUR_TRANSPARENT_BLACK;
}

void
d12_pack_sampler_descriptor(struct d12_descriptor *slot,
                            const D3D12_SAMPLER_DESC2 *desc)
{
   if (!slot || !desc)
      return;

   memset(slot, 0, sizeof(*slot));

   unsigned anisotropy = 1;
   if (D3D12_DECODE_IS_ANISOTROPIC_FILTER(desc->Filter)) {
      unsigned requested = CLAMP(desc->MaxAnisotropy, 1, 16);
      while (anisotropy < requested)
         anisotropy <<= 1;
   }

   const bool comparison = D3D12_DECODE_IS_COMPARISON_FILTER(desc->Filter);
   agx_pack(&slot->sampler, SAMPLER, cfg) {
      cfg.minimum_lod = desc->MinLOD;
      cfg.maximum_lod = desc->MaxLOD;
      cfg.magnify = D3D12_DECODE_MAG_FILTER(desc->Filter) ==
                       D3D12_FILTER_TYPE_LINEAR
                       ? AGX_FILTER_LINEAR
                       : AGX_FILTER_NEAREST;
      cfg.minify = D3D12_DECODE_MIN_FILTER(desc->Filter) ==
                      D3D12_FILTER_TYPE_LINEAR
                      ? AGX_FILTER_LINEAR
                      : AGX_FILTER_NEAREST;
      cfg.mip_filter = D3D12_DECODE_MIP_FILTER(desc->Filter) ==
                          D3D12_FILTER_TYPE_LINEAR
                          ? AGX_MIP_FILTER_LINEAR
                          : AGX_MIP_FILTER_NEAREST;
      cfg.wrap_s = d12_sampler_wrap(desc->AddressU);
      cfg.wrap_t = d12_sampler_wrap(desc->AddressV);
      cfg.wrap_r = d12_sampler_wrap(desc->AddressW);
      cfg.pixel_coordinates =
         desc->Flags & D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES;
      cfg.maximum_anisotropy = anisotropy;
      cfg.border_colour = d12_sampler_border(desc);
      cfg.compare_enable = comparison;
      cfg.compare_func = d12_sampler_compare(desc->ComparisonFunc);
   }

   memcpy(slot->sampler_border, desc->FloatBorderColor,
          sizeof(slot->sampler_border));
   slot->sampler_lod_bias = desc->MipLODBias;
   slot->sampler_flags = desc->Flags;
   slot->sampler_kind = D12_DESC_SAMPLER;
}

static void STDMETHODCALLTYPE
d12_device_CreateSampler(ID3D12Device2 *iface,
                         const D3D12_SAMPLER_DESC *desc,
                         D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_device *device = d12_device_from_iface(iface);
   struct d12_descriptor *slot = (struct d12_descriptor *)dest.ptr;
   if (!slot || !desc)
      return;

   D3D12_SAMPLER_DESC2 desc2 = {
      .Filter = desc->Filter,
      .AddressU = desc->AddressU,
      .AddressV = desc->AddressV,
      .AddressW = desc->AddressW,
      .MipLODBias = desc->MipLODBias,
      .MaxAnisotropy = desc->MaxAnisotropy,
      .ComparisonFunc = desc->ComparisonFunc,
      .MinLOD = desc->MinLOD,
      .MaxLOD = desc->MaxLOD,
      .Flags = D3D12_SAMPLER_FLAG_NONE,
   };
   memcpy(desc2.FloatBorderColor, desc->BorderColor,
          sizeof(desc2.FloatBorderColor));
   d12_pack_sampler_descriptor(slot, &desc2);
   d12_descriptor_heap_update_slot(device, slot);
}

/*
 * Buffer views. A descriptor here is just the address the shader will
 * reach the resource through, resolved once at view creation.
 *
 * Texture SRVs are recorded separately below; this helper is deliberately
 * buffer-only so a tiled resource can never be interpreted as linear data.
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
   slot->address = res->addr + offset;
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
   struct d12_device *device = d12_device_from_iface(iface);
   struct d12_descriptor *slot = (struct d12_descriptor *)dest.ptr;

   if (!slot)
      return;

   memset(slot, 0, sizeof(*slot));
   d12_descriptor_heap_update_slot(device, slot);

   if (!resource) {
      /*
       * A null descriptor must READ AS ZERO, not fault. Leaving address 0
       * here (which zeroing the slot did) makes the shader dereference
       * NULL: the GPU reports `address: 0x0, reason: Unmapped, read: true`
       * and times out the queue -- and because the compositor shares this
       * GPU, that freezes the desktop too.
       *
       * Bisecting the suite localised the timeout burst to exactly the
       * test_null_* cluster: names.txt 1..278 gave +0, 279..418 gave +69.
       */
      slot->address = device->null_va;
      slot->size_B = D12_NULL_REGION_SIZE;
      return;
   }

   struct d12_resource *res = d12_resource_from_iface(resource);

   /* A 2D texture view records the resource; the hardware descriptor is
    * packed at draw time, when the heap it lives in is built.
    */
   if ((desc && (desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D ||
                 desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DMS)) ||
       (!desc &&
        res->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)) {
      if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
         mesa_loge("CreateShaderResourceView: TEXTURE2D view of a "
                   "non-texture resource");
         return;
      }

      const unsigned samples = MAX2(res->desc.SampleDesc.Count, 1);
      const bool msaa = samples > 1;
      if ((samples != 1 && samples != 2 && samples != 4) ||
          res->desc.DepthOrArraySize != 1 || res->desc.MipLevels != 1 ||
          (desc && msaa &&
           desc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2DMS) ||
          (desc && !msaa &&
           desc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D)) {
         mesa_loge("CreateShaderResourceView: implicit texture view shape "
                   "unsupported (mips %u, layers %u, samples %u)",
                   res->desc.MipLevels, res->desc.DepthOrArraySize,
                   res->desc.SampleDesc.Count);
         return;
      }

      const unsigned plane = desc && !msaa ? desc->Texture2D.PlaneSlice : 0;
      if (plane > 1 || (plane == 1 && !res->stencil_bo)) {
         mesa_loge("CreateShaderResourceView: texture plane %u unsupported",
                   plane);
         return;
      }

      slot->kind = D12_DESC_SRV;
      slot->resource = res;
      slot->address = plane ? res->stencil_addr : res->addr;
      slot->format = desc && desc->Format != DXGI_FORMAT_UNKNOWN
                        ? desc->Format
                        : res->desc.Format;
      slot->texture_component_mapping =
         desc ? desc->Shader4ComponentMapping
              : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      slot->texture_plane = plane;
      d12_descriptor_heap_update_slot(device, slot);
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
   if (desc && (desc->Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW)) {
      stride = 4;
   } else if (!stride && desc && desc->Format != DXGI_FORMAT_UNKNOWN) {
      /* A typed Buffer<T> has StructureByteStride == 0. Its element size
       * comes from the view format, not from the structured-buffer field.
       */
      stride = d12_format_bytes_per_pixel(desc->Format);
      if (!stride) {
         mesa_loge("CreateShaderResourceView: typed buffer format %u "
                   "has no supported element size", (unsigned)desc->Format);
         return;
      }
   }
   if (!stride)
      stride = 4;

   if (d12_fill_buffer_descriptor(slot, res, first, count, stride,
                                  "CreateShaderResourceView")) {
      slot->kind = D12_DESC_SRV;
      slot->format = desc ? desc->Format : DXGI_FORMAT_UNKNOWN;
      slot->buffer_kind =
         desc && (desc->Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW)
            ? D12_BUFFER_VIEW_RAW
         : desc && desc->Buffer.StructureByteStride
            ? D12_BUFFER_VIEW_STRUCTURED
            : D12_BUFFER_VIEW_TYPED;
      d12_descriptor_heap_update_slot(device, slot);
   }
}

static void STDMETHODCALLTYPE
d12_device_CreateUnorderedAccessView(
   ID3D12Device2 *iface, ID3D12Resource *resource, ID3D12Resource *counter,
   const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
   D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_device *device = d12_device_from_iface(iface);
   struct d12_descriptor *slot = (struct d12_descriptor *)dest.ptr;

   if (!slot)
      return;

   memset(slot, 0, sizeof(*slot));
   d12_descriptor_heap_update_slot(device, slot);

   if (!resource) {
      /* Same reasoning as the null SRV above.  Leave counter_address zero:
       * command recording replaces absent counters with a distinct mapped
       * zeroed cell per binding slot, separate from this read-as-zero region.
       */
      slot->address = device->null_va;
      slot->size_B = D12_NULL_REGION_SIZE;
      return;
   }

   struct d12_resource *res = d12_resource_from_iface(resource);

   D3D12_UAV_DIMENSION dimension = desc ? desc->ViewDimension
                                        : D3D12_UAV_DIMENSION_UNKNOWN;

   if (res->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
      const unsigned samples = MAX2(res->desc.SampleDesc.Count, 1);
      const uint32_t resource_layers = MAX2(res->desc.DepthOrArraySize, 1);
      const uint32_t resource_levels = MAX2(res->desc.MipLevels, 1);
      uint32_t level = 0, first_layer = 0, layers = 1;
      bool array = false;

      if (samples != 1) {
         mesa_loge("CreateUnorderedAccessView: multisample textures cannot "
                   "have UAVs");
         return;
      }

      if (!desc) {
         array = resource_layers > 1;
         layers = resource_layers;
      } else if (dimension == D3D12_UAV_DIMENSION_TEXTURE2D) {
         level = desc->Texture2D.MipSlice;
         if (resource_layers != 1) {
            mesa_loge("CreateUnorderedAccessView: TEXTURE2D view requires a "
                      "single-layer resource");
            return;
         }
      } else if (dimension == D3D12_UAV_DIMENSION_TEXTURE2DARRAY) {
         array = true;
         level = desc->Texture2DArray.MipSlice;
         first_layer = desc->Texture2DArray.FirstArraySlice;
         layers = desc->Texture2DArray.ArraySize;
         if (layers == UINT32_MAX)
            layers = resource_layers - MIN2(first_layer, resource_layers);
      } else {
         mesa_loge("CreateUnorderedAccessView: texture view dimension %d "
                   "unsupported", (int)dimension);
         return;
      }

      if (level >= resource_levels || first_layer >= resource_layers ||
          !layers || layers > resource_layers - first_layer) {
         mesa_loge("CreateUnorderedAccessView: texture range level %u/%u "
                   "layers %u+%u/%u is out of range", level,
                   resource_levels, first_layer, layers, resource_layers);
         return;
      }

      DXGI_FORMAT view_format =
         desc && desc->Format != DXGI_FORMAT_UNKNOWN ? desc->Format
                                                     : res->desc.Format;
      enum pipe_format pf = d12_format_to_pipe(view_format);
      if (pf == PIPE_FORMAT_NONE || !ail_is_valid_pixel_format(pf) ||
          !ail_pixel_format[pf].texturable ||
          util_format_is_depth_or_stencil(pf)) {
         mesa_loge("CreateUnorderedAccessView: texture format %u is not "
                   "readable by AGX", (unsigned)view_format);
         return;
      }

      slot->resource = res;
      slot->address =
         res->addr + (uint64_t)first_layer * res->layout.layer_stride_B;
      slot->texture_layers = layers;
      slot->texture_level = level;
      slot->format = view_format;
      slot->kind = D12_DESC_UAV;
      slot->texture_array = array;
      d12_descriptor_heap_update_slot(device, slot);
      return;
   }

   if (res->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
       (desc && dimension != D3D12_UAV_DIMENSION_BUFFER)) {
      mesa_loge("CreateUnorderedAccessView: view dimension %d unsupported",
                (int)dimension);
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
      slot->counter_address = cres->addr + off;
   }

   uint64_t first = desc ? desc->Buffer.FirstElement : 0;
   uint32_t count = desc ? desc->Buffer.NumElements
                         : (uint32_t)(res->bo->size / 4);
   uint32_t stride = desc ? desc->Buffer.StructureByteStride : 4;

   if (desc && (desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW)) {
      stride = 4;
   } else if (!stride && desc && desc->Format != DXGI_FORMAT_UNKNOWN) {
      stride = d12_format_bytes_per_pixel(desc->Format);
      if (!stride) {
         mesa_loge("CreateUnorderedAccessView: typed buffer format %u "
                   "has no supported element size", (unsigned)desc->Format);
         return;
      }
   }
   if (!stride)
      stride = 4;

   if (d12_fill_buffer_descriptor(slot, res, first, count, stride,
                                  "CreateUnorderedAccessView")) {
      slot->kind = D12_DESC_UAV;
      slot->format = desc ? desc->Format : DXGI_FORMAT_UNKNOWN;
      slot->buffer_kind =
         desc && (desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW)
            ? D12_BUFFER_VIEW_RAW
         : desc && desc->Buffer.StructureByteStride
            ? D12_BUFFER_VIEW_STRUCTURED
            : D12_BUFFER_VIEW_TYPED;
      d12_descriptor_heap_update_slot(device, slot);
   }
}

static void STDMETHODCALLTYPE
d12_device_CreateConstantBufferView(
   ID3D12Device2 *iface, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
   D3D12_CPU_DESCRIPTOR_HANDLE dest)
{
   struct d12_device *device = d12_device_from_iface(iface);
   struct d12_descriptor *slot = (struct d12_descriptor *)dest.ptr;

   if (!slot)
      return;

   memset(slot, 0, sizeof(*slot));
   d12_descriptor_heap_update_slot(device, slot);

   if (!desc || !desc->BufferLocation)
      return;

   /* A CBV names a GPU address directly rather than a resource. */
   slot->kind = D12_DESC_CBV;
   slot->address = desc->BufferLocation;
   slot->size_B = desc->SizeInBytes;
   d12_descriptor_heap_update_slot(device, slot);
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

static void STDMETHODCALLTYPE
d12_device_CopyDescriptorsSimple(
   ID3D12Device2 *iface, UINT count, D3D12_CPU_DESCRIPTOR_HANDLE dest,
   D3D12_CPU_DESCRIPTOR_HANDLE src, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   struct d12_device *device = d12_device_from_iface(iface);
   UINT stride = d12_device_GetDescriptorHandleIncrementSize(iface, type);

   if (!count || !stride || !dest.ptr || !src.ptr)
      return;

   /* D3D12 forbids overlap between source and destination ranges. */
   memcpy((void *)(uintptr_t)dest.ptr, (const void *)(uintptr_t)src.ptr,
          (size_t)count * stride);
   for (UINT i = 0; i < count; i++) {
      struct d12_descriptor *slot =
         (struct d12_descriptor *)(uintptr_t)(dest.ptr + (SIZE_T)i * stride);
      d12_descriptor_heap_update_slot(device, slot);
   }
}

static void STDMETHODCALLTYPE
d12_device_CopyDescriptors(
   ID3D12Device2 *iface, UINT num_dest_ranges,
   const D3D12_CPU_DESCRIPTOR_HANDLE *dest_starts,
   const UINT *dest_sizes, UINT num_src_ranges,
   const D3D12_CPU_DESCRIPTOR_HANDLE *src_starts, const UINT *src_sizes,
   D3D12_DESCRIPTOR_HEAP_TYPE type)
{
   struct d12_device *device = d12_device_from_iface(iface);
   UINT stride = d12_device_GetDescriptorHandleIncrementSize(iface, type);

   if (!stride || !dest_starts || !src_starts)
      return;

   UINT dest_range = 0, src_range = 0;
   UINT dest_offset = 0, src_offset = 0;

   /* The API presents two flattened descriptor streams split across
    * independently-sized ranges. Walk both streams together, skipping empty
    * ranges and copying the largest contiguous run available on each side.
    * A NULL size array means one descriptor per range.
    */
   while (dest_range < num_dest_ranges && src_range < num_src_ranges) {
      UINT dest_count = dest_sizes ? dest_sizes[dest_range] : 1;
      UINT src_count = src_sizes ? src_sizes[src_range] : 1;

      if (dest_offset >= dest_count) {
         dest_range++;
         dest_offset = 0;
         continue;
      }
      if (src_offset >= src_count) {
         src_range++;
         src_offset = 0;
         continue;
      }

      UINT count = MIN2(dest_count - dest_offset, src_count - src_offset);
      uintptr_t dest = dest_starts[dest_range].ptr +
                       (uintptr_t)dest_offset * stride;
      uintptr_t src = src_starts[src_range].ptr +
                      (uintptr_t)src_offset * stride;

      if (!dest || !src)
         return;

      memcpy((void *)dest, (const void *)src, (size_t)count * stride);
      for (UINT i = 0; i < count; i++) {
         struct d12_descriptor *slot =
            (struct d12_descriptor *)(dest + (uintptr_t)i * stride);
         d12_descriptor_heap_update_slot(device, slot);
      }
      dest_offset += count;
      src_offset += count;
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

   /* On failure the out pointer must be NULLED. The suite deliberately
    * seeds it with 0xdeadbeef and checks it comes back NULL. */
   if (out)
      *out = NULL;

   return d12_resource_create_committed(device, heap_props, heap_flags, desc,
                                        initial_state, clear_value, riid, out);
}

static unsigned
d12_copyable_plane_count(DXGI_FORMAT format)
{
   return d12_format_stencil_plane(format) != PIPE_FORMAT_NONE ? 2 : 1;
}

static enum pipe_format
d12_copyable_plane_pipe(DXGI_FORMAT format, unsigned plane)
{
   if (d12_format_is_depth_stencil(format)) {
      return plane == 0 ? d12_format_depth_plane(format)
                        : d12_format_stencil_plane(format);
   }

   return plane == 0 ? d12_format_to_pipe(format) : PIPE_FORMAT_NONE;
}

static DXGI_FORMAT
d12_copyable_plane_format(DXGI_FORMAT format, unsigned plane,
                          unsigned plane_count)
{
   /* D3D exposes depth/stencil copies as independent typeless planes, even
    * though the resource itself has a packed or depth-typed DXGI format.
    */
   if (plane_count == 2)
      return plane == 0 ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R8_TYPELESS;

   return format;
}

/*
 * Describe how a resource's subresources sit in a buffer.  Offsets are
 * relative to BaseOffset, while TotalBytes is the exact span of the selected
 * footprints and deliberately excludes that base.  Rows and depth slices
 * include padding only between neighbours; the final row/slice ends at its
 * real byte size.
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
   (void)iface;

   if (layouts)
      memset(layouts, 0xff, (size_t)num_subresources * sizeof(*layouts));
   if (num_rows)
      memset(num_rows, 0xff, (size_t)num_subresources * sizeof(*num_rows));
   if (row_size_bytes)
      memset(row_size_bytes, 0xff,
             (size_t)num_subresources * sizeof(*row_size_bytes));

   UINT64 total = UINT64_MAX;
   bool buffer;
   unsigned planes, layers, mips;

   if (!desc)
      goto out;

   buffer = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
   mips = desc->MipLevels;
   layers = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
               ? 1
               : desc->DepthOrArraySize;
   planes = buffer ? 1 : d12_copyable_plane_count(desc->Format);

   /* Reject malformed resource descriptions without partially populating
    * the caller's arrays.  D3D specifies all-ones sentinels for this void
    * API's invalid-input result.
    */
   if (!desc->Width || !desc->Height || !desc->DepthOrArraySize || !mips ||
       !desc->SampleDesc.Count ||
       desc->Dimension < D3D12_RESOURCE_DIMENSION_BUFFER ||
       desc->Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D)
      goto out;

   if (buffer) {
      if (desc->Height != 1 || desc->DepthOrArraySize != 1 || mips != 1 ||
          desc->Format != DXGI_FORMAT_UNKNOWN ||
          desc->SampleDesc.Count != 1 ||
          desc->Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR)
         goto out;
   } else {
      if (desc->Format == DXGI_FORMAT_UNKNOWN ||
          (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D &&
           (desc->Height != 1 || desc->SampleDesc.Count != 1)))
         goto out;

      for (unsigned plane = 0; plane < planes; plane++) {
         if (d12_copyable_plane_pipe(desc->Format, plane) == PIPE_FORMAT_NONE)
            goto out;
      }
   }

   uint64_t subresource_count = (uint64_t)mips * layers * planes;
   if (first_subresource >= subresource_count ||
       num_subresources > subresource_count - first_subresource)
      goto out;

   UINT64 offset = 0;
   total = 0;

   for (UINT i = 0; i < num_subresources; i++) {
      UINT sub = first_subresource + i;
      unsigned mip = sub % mips;
      unsigned plane = sub / ((uint64_t)mips * layers);
      enum pipe_format pipe = buffer ? PIPE_FORMAT_R8_UINT
                                     : d12_copyable_plane_pipe(desc->Format,
                                                               plane);
      unsigned block_w = util_format_get_blockwidth(pipe);
      unsigned block_h = util_format_get_blockheight(pipe);
      unsigned block_size = util_format_get_blocksize(pipe);
      uint64_t width64 = buffer ? desc->Width : MAX2(desc->Width >> mip, 1);
      UINT height = buffer ? 1 : MAX2(desc->Height >> mip, 1);
      UINT depth = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                      ? MAX2(desc->DepthOrArraySize >> mip, 1)
                      : 1;

      width64 = ALIGN_POT(width64, block_w);
      height = ALIGN_POT(height, block_h);
      if (width64 > UINT32_MAX)
         goto invalid;

      UINT width = width64;
      UINT rows = height / block_h;
      UINT64 row_size = (width64 / block_w) * block_size;
      UINT row_alignment =
         D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * planes;
      UINT64 pitch64 = ALIGN_POT(row_size, row_alignment);
      if (pitch64 > UINT32_MAX)
         goto invalid;
      UINT pitch = pitch64;

      if (layouts) {
         layouts[i].Offset = base_offset + offset;
         layouts[i].Footprint.Format =
            buffer ? DXGI_FORMAT_UNKNOWN
                   : d12_copyable_plane_format(desc->Format, plane, planes);
         layouts[i].Footprint.Width = width;
         layouts[i].Footprint.Height = height;
         layouts[i].Footprint.Depth = depth;
         layouts[i].Footprint.RowPitch = pitch;
      }
      if (num_rows)
         num_rows[i] = rows;
      if (row_size_bytes)
         row_size_bytes[i] = row_size;

      UINT64 slice_size = (uint64_t)(rows - 1) * pitch + row_size;
      UINT64 slice_stride = ALIGN_POT(slice_size, row_alignment);
      UINT64 sub_size = (uint64_t)(depth - 1) * slice_stride + slice_size;
      total = offset + sub_size;
      offset = ALIGN_POT(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
   }

out:
   if (total_bytes)
      *total_bytes = total;
   return;

invalid:
   /* A late arithmetic/range failure has the same all-ones contract as an
    * invalid descriptor; erase any earlier partial results first.
    */
   if (layouts)
      memset(layouts, 0xff, (size_t)num_subresources * sizeof(*layouts));
   if (num_rows)
      memset(num_rows, 0xff, (size_t)num_subresources * sizeof(*num_rows));
   if (row_size_bytes)
      memset(row_size_bytes, 0xff,
             (size_t)num_subresources * sizeof(*row_size_bytes));
   total = UINT64_MAX;
   goto out;
}

static HRESULT STDMETHODCALLTYPE
d12_device_CreateFence(ID3D12Device2 *iface, UINT64 initial_value,
                       D3D12_FENCE_FLAGS flags, REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_fence_create(device, initial_value, flags, riid, out);
}

static HRESULT STDMETHODCALLTYPE
d12_device_SetEventOnMultipleFenceCompletion(
   ID3D12Device2 *iface, ID3D12Fence *const *fences,
   const UINT64 *values, UINT count,
   D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags, HANDLE event)
{
   struct d12_device *device = d12_device_from_iface(iface);
   return d12_fence_set_event_multiple(device, fences, values, count, flags,
                                       event);
}

static HRESULT STDMETHODCALLTYPE
d12_device_GetDeviceRemovedReason(ID3D12Device2 *iface)
{
   struct d12_device *device = d12_device_from_iface(iface);

   if (!device->removed)
      return S_OK;

   /* The recorded cause, not a blanket DEVICE_REMOVED: an application that
    * submitted a recording command list needs to see INVALID_CALL to know
    * the fault was its own. */
   return device->removed_reason ? device->removed_reason
                                 : DXGI_ERROR_DEVICE_REMOVED;
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

static struct d12_device *
d12_device_from_iface15(ID3D12Device15 *iface)
{
   return container_of(iface, struct d12_device, iface15);
}

static HRESULT STDMETHODCALLTYPE
d12_device15_QueryInterface(ID3D12Device15 *iface, REFIID riid, void **out)
{
   struct d12_device *device = d12_device_from_iface15(iface);
   return d12_device_QueryInterface((ID3D12Device2 *)&device->iface,
                                    riid, out);
}

static ULONG STDMETHODCALLTYPE
d12_device15_AddRef(ID3D12Device15 *iface)
{
   return d12_device_AddRef(
      (ID3D12Device2 *)&d12_device_from_iface15(iface)->iface);
}

static ULONG STDMETHODCALLTYPE
d12_device15_Release(ID3D12Device15 *iface)
{
   return d12_device_Release(
      (ID3D12Device2 *)&d12_device_from_iface15(iface)->iface);
}

static HRESULT STDMETHODCALLTYPE
d12_device15_CreateQueryHeap1(ID3D12Device15 *iface,
                              const D3D12_QUERY_HEAP_DESC *desc,
                              D3D12_QUERY_HEAP_FLAGS flags, REFIID riid,
                              void **out)
{
   return d12_query_heap_create_flags(d12_device_from_iface15(iface), desc,
                                      flags, riid, out);
}

static bool
d12_cpu_query_type_matches(const struct d12_query_heap *heap,
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
      return type == D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0;
   default:
      return false;
   }
}

static HRESULT STDMETHODCALLTYPE
d12_device15_ResolveQueryData(ID3D12Device15 *iface,
                              ID3D12QueryHeap *query_heap,
                              D3D12_QUERY_TYPE type, UINT start,
                              UINT count, void *resolved)
{
   struct d12_device *device = d12_device_from_iface15(iface);
   struct d12_query_heap *heap = (struct d12_query_heap *)query_heap;

   if (!heap || !resolved || heap->device != device ||
       !(heap->flags & D3D12_QUERY_HEAP_FLAG_CPU_RESOLVE) ||
       !d12_cpu_query_type_matches(heap, type) ||
       start > heap->desc.Count || count > heap->desc.Count - start)
      return E_INVALIDARG;

   const uint8_t *src = agx_bo_map(heap->bo);
   uint8_t *dst = resolved;
   for (UINT i = 0; i < count; i++) {
      const uint8_t *value = src + (size_t)(start + i) * heap->stride;
      if (type == D3D12_QUERY_TYPE_BINARY_OCCLUSION) {
         uint64_t sample;
         memcpy(&sample, value, sizeof(sample));
         sample = sample != 0;
         memcpy(dst + (size_t)i * sizeof(sample), &sample, sizeof(sample));
      } else {
         memcpy(dst + (size_t)i * heap->stride, value, heap->stride);
      }
   }

   return S_OK;
}

static const ID3D12Device15Vtbl d12_device15_vtbl = {
   .QueryInterface = d12_device15_QueryInterface,
   .AddRef = d12_device15_AddRef,
   .Release = d12_device15_Release,
   .CreateQueryHeap1 = d12_device15_CreateQueryHeap1,
   .ResolveQueryData = d12_device15_ResolveQueryData,
};

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
   .CreateCommandSignature = d12_device_CreateCommandSignature,
   .CreateHeap = d12_device_CreateHeap,
   .CreatePlacedResource = d12_device_CreatePlacedResource,
   .GetResourceAllocationInfo = (void *)d12_device_GetResourceAllocationInfo,
   .CreateRenderTargetView = d12_device_CreateRenderTargetView,
   .CreateDepthStencilView = d12_device_CreateDepthStencilView,
   .CreateSampler = d12_device_CreateSampler,
   .CreateShaderResourceView = d12_device_CreateShaderResourceView,
   .CreateUnorderedAccessView = d12_device_CreateUnorderedAccessView,
   .CreateConstantBufferView = d12_device_CreateConstantBufferView,
   .GetDescriptorHandleIncrementSize =
      d12_device_GetDescriptorHandleIncrementSize,
   .CopyDescriptors = d12_device_CopyDescriptors,
   .CopyDescriptorsSimple = d12_device_CopyDescriptorsSimple,
   .CreateCommittedResource = d12_device_CreateCommittedResource,
   .GetCopyableFootprints = d12_device_GetCopyableFootprints,
   .CreateQueryHeap = d12_device_CreateQueryHeap,
   .CreateFence = d12_device_CreateFence,
   .SetEventOnMultipleFenceCompletion =
      d12_device_SetEventOnMultipleFenceCompletion,
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
   /*
    * Sampler state, the tessellation heap descriptor, then the NULL
    * descriptor region.
    *
    * D3D12 requires a null descriptor to read as zero and to discard
    * writes. Recording address 0 instead -- which is what zeroing the
    * descriptor slot did -- makes the shader dereference NULL, and the GPU
    * reports `address: 0x0, reason: Unmapped, read: true` and times out the
    * queue. Bisecting the suite put the fault burst exactly at the
    * test_null_* cluster (names.txt 279+, +69 timeouts; lines 1..278 gave
    * +0), which is what identified this.
    *
    * It lives inside rodata_bo rather than in a BO of its own so it
    * inherits the existing device lifetime and every teardown path already
    * written for it.
    */
   device->rodata_bo =
      agx_bo_create(&device->dev,
                    ALIGN_POT(AGX_SAMPLER_LENGTH, 16) +
                       sizeof(struct poly_heap) + D12_NULL_REGION_SIZE,
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
   /*
    * 128 MiB, matching hk_init_heap() in the Vulkan driver on this same
    * hardware. We were at 32 MiB, four times under-sized, and the geometry
    * heap is a bump allocator (struct poly_heap: base/bottom/size) shared
    * by tessellation and geometry shaders.
    *
    * Overflowing it is what produced the GPU-timeout bursts that froze the
    * desktop all of 2026-08-05. Bisection put the first fault at
    * test_quad_tessellation_dxil, reading address 0xbb1ae27b80 -- a real
    * address just past a live allocation, `reason: Unmapped`. Our own G7
    * suite passes because its tessellation factors fit inside 32 MiB.
    *
    * poly_heap_alloc_offs does bounds-check, but it lives in src/poly and is
    * shared with the Vulkan driver, and it reacts by printf+abort from the
    * shader -- so the honest fix here is to stop overflowing rather than to
    * change a check other drivers depend on.
    */
   device->heap_bo = agx_bo_create(&device->dev, 128 * 1024 * 1024, 0, 0,
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

   /* The null region follows the heap descriptor. Zeroed explicitly: a
    * null descriptor must READ AS ZERO, so whatever the allocator happened
    * to leave here is not good enough.
    */
   size_t null_off = heap_off + sizeof(struct poly_heap);
   device->null_va = device->rodata_bo->va->addr + null_off;
   memset((uint8_t *)agx_bo_map(device->rodata_bo) + null_off, 0,
          D12_NULL_REGION_SIZE);
   device->heap_desc_cpu =
      (struct poly_heap *)((uint8_t *)agx_bo_map(device->rodata_bo) +
                           heap_off);

   *device->heap_desc_cpu = (struct poly_heap){
      .base = (DEVICE(uchar))device->heap_bo->va->addr,
      .bottom = 0,
      .size = (uint32_t)device->heap_bo->size,
   };

   simple_mtx_init(&device->scratch_lock, mtx_plain);
   simple_mtx_init(&device->descriptor_heap_lock, mtx_plain);
   agx_scratch_init(&device->dev, &device->scratch_vs);
   agx_scratch_init(&device->dev, &device->scratch_fs);
   agx_scratch_init(&device->dev, &device->scratch_cs);

   device->iface.lpVtbl = (ID3D12DeviceVtbl *)&d12_device_vtbl;
   device->iface15.lpVtbl = (ID3D12Device15Vtbl *)&d12_device15_vtbl;
   device->refcount = 1;
   device->feature_level = max_fl;
   d12_private_data_store_init(&device->private_data);

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

   /* The legacy entry point only accepts the legacy description.  Native
    * also leaves the caller's output pointer untouched for this version
    * error, which applications use to distinguish argument validation.
    */
   if (version != D3D_ROOT_SIGNATURE_VERSION_1_0) {
      mesa_loge("D3D12SerializeRootSignature: version 0x%x unsupported",
                (unsigned)version);
      return E_INVALIDARG;
   }

   *blob = NULL;

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
   if (error_blob)
      *error_blob = NULL;
   if (!blob)
      return E_POINTER;

   *blob = NULL;

   void *data = NULL;
   size_t size = 0;
   HRESULT hr =
      d12_serialize_versioned_root_signature(desc, &data, &size);
   if (FAILED(hr))
      return hr;

   *blob = d12_blob_create(data, size);
   free(data);

   return *blob ? S_OK : E_OUTOFMEMORY;
}

PUBLIC HRESULT WINAPI
D3D12CreateRootSignatureDeserializer(const void *src, SIZE_T size,
                                     REFIID riid, void **out)
{
   return d12_root_signature_deserializer_create(src, size, riid, out);
}

PUBLIC HRESULT WINAPI
D3D12CreateVersionedRootSignatureDeserializer(const void *src, SIZE_T size,
                                              REFIID riid, void **out)
{
   return d12_versioned_root_signature_deserializer_create(src, size, riid,
                                                           out);
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
