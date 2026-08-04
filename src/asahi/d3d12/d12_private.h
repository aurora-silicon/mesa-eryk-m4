/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * d3d12agx: experimental native Direct3D 12 user-mode driver for Apple AGX
 * GPUs, built directly on the Mesa/Asahi substrate (agx_device and friends,
 * the same layer Honeykrisp sits on). There is deliberately no Vulkan and no
 * SPIR-V anywhere in this driver.
 *
 * API ABI: microsoft/DirectX-Headers (MIT). D3D12 behavioral contract:
 * Microsoft public documentation. Hardware truth: src/asahi at the pinned
 * Mesa tag, and nothing else.
 *
 * Campaign front door: ntasp repo, docs/d3d12agx-campaign.md.
 */

#pragma once

#define COBJMACROS
#include <wsl/winadapter.h>
#include <directx/d3d12.h>

#include <stdbool.h>
#include <stdint.h>

#include "agx_bg_eot.h"
#include "agx_compile.h"
#include "agx_device.h"
#include "agx_linker.h"
#include "agx_tilebuffer.h"
#include "agx_helpers.h"
#include "agx_uvs.h"
#include "dxil/d12_dxil.h"
#include "layout.h"

#undef MESA_LOG_TAG
#define MESA_LOG_TAG "d3d12agx"
#include "util/log.h"
#include "util/macros.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"

/*
 * COM plumbing. Every d12 object embeds its COM interface as the FIRST
 * member so the interface pointer and the object pointer are the same
 * address, exactly like vtable-first layouts elsewhere in Mesa.
 */

static inline bool
d12_guid_equal(REFIID a, REFIID b)
{
   return memcmp(a, b, sizeof(GUID)) == 0;
}

/* Crash-safe vtable defaults (generated). Every slot we do not implement
 * points at a typed stub that reports and fails, so an unimplemented call
 * is a recordable failure instead of a jump to NULL.
 */
#include "d12_stubs.h"

/* Log an unknown riid so we learn what callers actually want. */
void d12_log_unknown_riid(const char *where, REFIID riid);

struct d12_device {
   ID3D12Device iface; /* must be first */
   uint32_t refcount;

   /* The real GPU. Owned: closed on final release. */
   struct agx_device dev;

   /* Precompiled libagx kernel cache (copy/fill engines etc.), the same
    * mechanism Honeykrisp uses for its internal dispatches.
    */
   struct agx_bg_eot_cache bg_eot;

   /* Occlusion-query scratch: cmd_render.isp_oclqry_base must point at
    * valid memory even when no queries run (mirrors hk pointing it at the
    * device query heap).
    */
   struct agx_bo *oclqry_bo;

   /* One AGX_SAMPLER: the sampler state a texel fetch reads as ss0. */
   struct agx_bo *rodata_bo;

   D3D_FEATURE_LEVEL feature_level;

   /* Set when a submission fails. D3D12 models this as device removal, and
    * without it a later Signal would advance a fence past work that never
    * ran, making every result downstream a lie.
    */
   bool removed;
};

/* Committed resource: one agx_bo. UMA means every heap type is the same
 * memory; Map is just the BO's CPU mapping. Buffers are flat; textures
 * carry an ail_layout (G3: 2D single-level RGBA8, LINEAR tiling so the CPU
 * can verify pixels directly).
 */
struct d12_resource {
   ID3D12Resource iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   D3D12_RESOURCE_DESC desc;
   D3D12_HEAP_PROPERTIES heap_props;
   struct agx_bo *bo;

   /* Textures only */
   struct ail_layout layout;

   /* Depth/stencil only. AGX stores stencil in a physically separate plane
    * from depth, so a D24S8/D32S8 resource is two allocations behind one
    * ID3D12Resource. `layout` above is the depth plane.
    */
   bool is_depth_stencil;
   struct agx_bo *stencil_bo; /* NULL when the format has no stencil */
   struct ail_layout stencil_layout;
};

/*
 * One descriptor, whatever the heap type. Sized to exactly the advertised
 * handle increment so CPU-handle arithmetic in applications lands on slot
 * boundaries.
 *
 * Shader-visible descriptors carry a precomputed address rather than a
 * resource pointer plus offset: binding a descriptor table copies that
 * address straight into the root table, and nothing on the GPU ever reads
 * a descriptor.
 */
enum d12_descriptor_kind {
   D12_DESC_NONE = 0,
   D12_DESC_RTV,
   D12_DESC_DSV,
   D12_DESC_CBV,
   D12_DESC_SRV,
   D12_DESC_UAV,
};

struct d12_descriptor {
   /* GPU-read fields first: an SM 6.6 heap handle indexes this struct
    * IN MEMORY, so the shader-visible layout is part of the ABI. The
    * address must sit at offset 0 (resource_address reads it there) and
    * the UAV counter pointer at offset 8 (D12_DESC_COUNTER_OFFSET).
    */
   uint64_t address;         /* resource base + view offset */
   uint64_t counter_address; /* UAV counter, or 0 */

   struct d12_resource *resource; /* not referenced; app keeps alive */
   uint32_t size_B;
   uint32_t stride_B;
   DXGI_FORMAT format;
   uint8_t kind; /* enum d12_descriptor_kind */
   uint8_t pad[3];
};
static_assert(sizeof(struct d12_descriptor) == 40,
              "must match handle increment");
static_assert(offsetof(struct d12_descriptor, address) == 0 &&
                 offsetof(struct d12_descriptor, counter_address) == 8,
              "shader-visible ABI");

/* The RTV/DSV path predates the unified descriptor and still speaks this
 * name; they are the same object.
 */
#define d12_rtv d12_descriptor

struct d12_descriptor_heap {
   ID3D12DescriptorHeap iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   D3D12_DESCRIPTOR_HEAP_DESC desc;
   struct d12_descriptor *slots; /* NumDescriptors entries */
};

/*
 * The parsed root signature.
 *
 * This is the only thing that connects a root PARAMETER index -- what
 * SetGraphicsRootDescriptorTable and friends name -- to the shader
 * REGISTER a shader actually asks for. Without it the driver has to assume
 * they are the same number, which holds only by coincidence.
 */
#define D12_MAX_ROOT_PARAMS 64
#define D12_MAX_ROOT_RANGES 16

struct d12_root_range {
   D3D12_DESCRIPTOR_RANGE_TYPE type;
   uint32_t num_descriptors;
   uint32_t base_register;
   uint32_t space;
   uint32_t offset_in_descriptors; /* ~0u means "append" */
};

struct d12_root_param {
   D3D12_ROOT_PARAMETER_TYPE type;
   D3D12_SHADER_VISIBILITY visibility;

   /* Root descriptors and root constants */
   uint32_t base_register;
   uint32_t space;
   uint32_t num_constants;

   /* Descriptor tables */
   struct d12_root_range ranges[D12_MAX_ROOT_RANGES];
   uint32_t num_ranges;
};

struct d12_root_layout {
   struct d12_root_param params[D12_MAX_ROOT_PARAMS];
   uint32_t num_params;
   uint32_t flags;
};

struct d12_root_signature {
   ID3D12RootSignature iface; /* must be first */
   uint32_t refcount;
   struct d12_device *device; /* holds a device ref */
   struct d12_root_layout layout;

   /* The slot assignment derived from the layout. Shaders are compiled
    * against it and the command list fills the root table through it.
    */
   struct d12_binding_map map;
};

/* Root signature blobs (d12_root_sig.c) */
HRESULT d12_serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC *desc,
                                     void **out_blob, size_t *out_size);
bool d12_root_signature_parse(const void *blob, size_t blob_size,
                              struct d12_root_layout *out);
unsigned d12_root_param_class(const struct d12_root_param *rp);
unsigned d12_descriptor_range_class(D3D12_DESCRIPTOR_RANGE_TYPE type);
void d12_root_layout_build_map(const struct d12_root_layout *layout,
                               struct d12_binding_map *out);
ID3DBlob *d12_blob_create(const void *data, size_t size);

/* One linked graphics stage */
struct d12_gfx_stage {
   struct agx_shader_info info;
   struct agx_linked_shader linked;
   struct agx_counts_packed counts;                  /* vertex */
   struct agx_fragment_shader_word_0_packed fs_counts; /* fragment */
   struct agx_fragment_face_2_packed frag_face;        /* fragment */
};

/* One face's stencil test, translated to hardware enums at PSO creation so
 * the draw path pushes packed state rather than re-translating per draw.
 */
struct d12_stencil_face {
   enum agx_zs_func compare;
   enum agx_stencil_op pass, depth_fail, stencil_fail;
};

/* The pipeline's depth/stencil state, already in AGX terms. */
struct d12_depth_stencil_state {
   bool depth_test;  /* DepthEnable */
   bool depth_write; /* DepthWriteMask == ALL */
   enum agx_zs_func depth_func;

   bool stencil_test;
   uint8_t stencil_read_mask, stencil_write_mask;
   struct d12_stencil_face front, back;

   DXGI_FORMAT dsv_format; /* DSVFormat from the PSO desc */
};

/* Rasterizer state that reaches the CULL block. Facing was hardcoded until
 * the depth work measured which winding the hardware calls front.
 */
struct d12_raster_state {
   bool cull_front, cull_back;
   bool front_face_ccw;
};

/* A compiled pipeline: AGX machine code produced from the app's DXIL. */
struct d12_pipeline_state {
   ID3D12PipelineState iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   bool is_compute;

   /* Compute */
   struct agx_bo *code_bo;
   uint64_t code_va;
   struct agx_shader_info info;

   /* Graphics */
   struct d12_gfx_stage vs, fs;
   struct agx_unlinked_uvs_layout uvs;
   struct agx_varyings_vs varyings;
   struct d12_depth_stencil_state ds;
   struct d12_raster_state raster;
};

static inline struct d12_pipeline_state *
d12_pipeline_state_from_iface(ID3D12PipelineState *iface)
{
   return (struct d12_pipeline_state *)iface;
}

struct d12_cmd_allocator {
   ID3D12CommandAllocator iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   D3D12_COMMAND_LIST_TYPE type;

   /* Lists derived from this allocator that are currently recording.
    * Reset must fail while any are.
    */
   unsigned recording_lists;
};

enum d12_list_state {
   D12_LIST_RECORDING,
   D12_LIST_CLOSED,
   D12_LIST_ERROR,
};

/* A command list is a sequence of hardware command segments, submitted as
 * one multi-command drm_asahi_submit in order:
 *
 * - CDM segments: compute control streams (copies etc.), encoded at record
 *   time exactly the way hk encodes internal dispatches (agx_cdm_launch +
 *   agx_cdm_barrier per dispatch, agx_cdm_terminate at segment close).
 * - RENDER segments: one per ClearRenderTargetView for now — a complete
 *   clear-only tiler pass (background program AGX_BG_CLEAR, end-of-tile
 *   AGX_EOT_STORE, empty VDM stream, PROCESS_EMPTY_TILES).
 *
 * Arg blobs and descriptors bump down from the top of the stream BO; USC
 * words live in a separate AGX_BO_LOW_VA BO for agx_usc_addr.
 */

enum d12_segment_type {
   D12_SEG_CDM,
   D12_SEG_RENDER,
};

struct d12_segment {
   enum d12_segment_type type;

   /* Control-stream extent within stream_bo (start/end offsets). CDM: the
    * dispatch stream. RENDER: the (empty) VDM stream.
    */
   size_t stream_start;
   size_t stream_end;

   /* CDM */
   uint32_t dispatches;

   /* RENDER */
   uint16_t width_px, height_px;
   struct agx_tilebuffer_layout tib;
   struct d12_resource *rt;  /* the target this pass renders to */
   uint32_t bg_usc, eot_usc; /* agx_usc_addr()-encoded */
   struct agx_counts_packed bg_counts, eot_counts;
   bool has_draws;

   /* Depth/stencil attachment. Unlike colour, depth never passes through a
    * background/end-of-tile USC program: the fixed-function ZLS unit loads
    * and stores it, driven entirely by the fields below.
    */
   struct d12_resource *ds; /* NULL when the pass has no depth/stencil */
   bool depth_cleared, stencil_cleared;
   uint32_t isp_bgobjdepth; /* clear value in the depth plane's encoding */
   uint8_t stencil_clear;

   /* Hardware scissor array (ISP_SCISSOR_BASE). Each draw indexes one
    * entry. This is not optional state: alongside the XY bounds each entry
    * carries the depth CLIP range, so a pass without it rasterizes nothing
    * outside z == 0.
    */
   uint64_t scissor_base;
   unsigned scissor_count;
};

#define D12_MAX_SCISSORS 256

#define D12_MAX_SEGMENTS 32

struct d12_cmd_list {
   ID3D12GraphicsCommandList iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   D3D12_COMMAND_LIST_TYPE type;
   enum d12_list_state state;

   struct agx_bo *stream_bo; /* control streams (bottom-up) + data (top-down) */
   struct agx_bo *usc_bo;    /* USC words, AGX_BO_LOW_VA */

   uint8_t *stream_cur;   /* next control word write */
   size_t data_floor;     /* offset of lowest data byte (bumps down) */
   size_t usc_offset;     /* next USC byte (bumps up) */

   struct d12_segment segments[D12_MAX_SEGMENTS];
   unsigned segment_count;
   bool cdm_open;    /* last segment is an unterminated CDM */
   bool render_open; /* last segment is an unterminated RENDER */

   /* Bound state */
   struct d12_cmd_allocator *allocator; /* the one it is recording into */
   struct d12_pipeline_state *pso; /* not referenced; app keeps alive */

   /* Resource addresses indexed by binding slot, one set per pipeline
    * type: D3D12 keeps graphics and compute root bindings completely
    * separate, and sharing one table lets a dispatch inherit a draw's
    * resources.
    */
   /* Whole descriptors, not just addresses: a texture needs its layout
    * and format to pack a hardware descriptor at draw time, which an
    * address alone cannot give.
    */
   struct d12_descriptor compute_root_table[D12_MAX_RESOURCE_BINDINGS];
   struct d12_descriptor graphics_root_table[D12_MAX_RESOURCE_BINDINGS];

   /* The bound root signatures, which are what map a root parameter index
    * to a shader register. Not referenced; the app keeps them alive.
    */
   struct d12_root_signature *compute_rs;
   struct d12_root_signature *graphics_rs;
   /* Rasterizer state, actually consumed by the draw path. */
   D3D12_VIEWPORT viewport;
   bool has_viewport;
   D3D12_RECT scissor;
   bool has_scissor;

   /* The render target bound by OMSetRenderTargets, so a draw into a pass
    * opened for a different target is caught instead of silently going to
    * the wrong texture.
    */
   struct d12_resource *bound_rt;

   /* The depth/stencil target bound by OMSetRenderTargets. Same purpose,
    * and it is also how a pass acquires depth when the application binds
    * without clearing (ClearDepthStencilView being the other way in).
    */
   struct d12_resource *bound_ds;

   /* OMSetStencilRef. Separate from the PSO because D3D12 makes the
    * reference value dynamic state while the ops live in the PSO.
    */
   uint8_t stencil_ref;
};

static inline struct d12_resource *
d12_resource_from_iface(ID3D12Resource *iface)
{
   return (struct d12_resource *)iface;
}

static inline struct d12_cmd_list *
d12_cmd_list_from_iface(ID3D12GraphicsCommandList *iface)
{
   return (struct d12_cmd_list *)iface;
}

struct d12_fence {
   ID3D12Fence iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */

   /* DRM timeline syncobj: D3D12 fences ARE timeline semaphores. The
    * completed value is the syncobj's signaled point; CPU Signal is a
    * timeline signal; queue Signal/Wait are syncobj transfers, mirroring
    * hk_queue.c.
    */
   uint32_t syncobj;
};

struct d12_queue {
   ID3D12CommandQueue iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   D3D12_COMMAND_QUEUE_DESC desc;

   /* Hardware queue on the asahi device */
   uint32_t agx_queue_id;

   /* Queue-progress timeline, same scheme as hk_queue: point N signals when
    * the Nth chunk of queue work completes. With no execution engine yet
    * (G1), the timeline only advances through Wait() transfers.
    */
   uint32_t syncobj;
   uint64_t timeline_value;
   simple_mtx_t lock;
};

static inline struct d12_device *
d12_device_from_iface(ID3D12Device *iface)
{
   return (struct d12_device *)iface;
}

static inline struct d12_fence *
d12_fence_from_iface(ID3D12Fence *iface)
{
   return (struct d12_fence *)iface;
}

static inline struct d12_queue *
d12_queue_from_iface(ID3D12CommandQueue *iface)
{
   return (struct d12_queue *)iface;
}

/* Object constructors (Create* device methods land here) */
HRESULT d12_fence_create(struct d12_device *device, UINT64 initial_value,
                         D3D12_FENCE_FLAGS flags, REFIID riid, void **out);
HRESULT d12_queue_create(struct d12_device *device,
                         const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid,
                         void **out);
HRESULT d12_resource_create_committed(struct d12_device *device,
                                      const D3D12_HEAP_PROPERTIES *heap_props,
                                      const D3D12_RESOURCE_DESC *desc,
                                      REFIID riid, void **out);
HRESULT d12_cmd_allocator_create(struct d12_device *device,
                                 D3D12_COMMAND_LIST_TYPE type, REFIID riid,
                                 void **out);
HRESULT d12_cmd_list_create(struct d12_device *device,
                            D3D12_COMMAND_LIST_TYPE type,
                            struct d12_cmd_allocator *allocator, REFIID riid,
                            void **out);

static inline struct d12_cmd_allocator *
d12_cmd_allocator_from_iface(ID3D12CommandAllocator *iface)
{
   return (struct d12_cmd_allocator *)iface;
}
HRESULT d12_descriptor_heap_create(struct d12_device *device,
                                   const D3D12_DESCRIPTOR_HEAP_DESC *desc,
                                   REFIID riid, void **out);
/* DXGI format support (d12_format.c) */
enum pipe_format d12_format_to_pipe(DXGI_FORMAT format);
bool d12_format_is_renderable(DXGI_FORMAT format);
unsigned d12_format_bytes_per_pixel(DXGI_FORMAT format);

/* Depth/stencil formats. AGX keeps stencil in a separate plane from depth,
 * so these are asked for independently rather than as one pipe format.
 */
bool d12_format_is_depth_stencil(DXGI_FORMAT format);
enum pipe_format d12_format_depth_plane(DXGI_FORMAT format);
enum pipe_format d12_format_stencil_plane(DXGI_FORMAT format);
bool d12_format_depth_is_substituted(DXGI_FORMAT format);

HRESULT d12_root_signature_create(struct d12_device *device,
                                  const void *blob, size_t blob_size,
                                  REFIID riid, void **out);
HRESULT d12_pipeline_state_create_compute(struct d12_device *device,
                                          const void *dxil, size_t dxil_size,
                                          const struct d12_binding_map *map,
                                          REFIID riid, void **out);
HRESULT d12_pipeline_state_create_graphics(
   struct d12_device *device, const void *vs_dxil, size_t vs_size,
   const void *ps_dxil, size_t ps_size, enum pipe_format rt_format,
   const D3D12_DEPTH_STENCIL_DESC *ds_desc, DXGI_FORMAT dsv_format,
   const D3D12_RASTERIZER_DESC *rs_desc, const struct d12_binding_map *map,
   REFIID riid, void **out);

void d12_device_add_ref(struct d12_device *device);
void d12_device_release(struct d12_device *device);
