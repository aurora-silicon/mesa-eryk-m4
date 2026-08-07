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
#include "poly/nir/poly_nir.h"
#include "poly/tessellator.h"

#undef MESA_LOG_TAG
#define MESA_LOG_TAG "d3d12agx"
#include "util/log.h"
#include "util/macros.h"
#include "util/simple_mtx.h"
#include "util/u_dynarray.h"
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

/*
 * Backing size for NULL descriptors. 64 KiB is D3D12's maximum constant
 * buffer size, so a null CBV is covered exactly for every offset the API
 * can legally address; a structured SRV/UAV indexed far past the end can
 * still walk off it, which needs per-access bounds checking rather than a
 * bigger buffer.
 */
#define D12_NULL_REGION_SIZE (64 * 1024)

/* d12_list_upload_vb_params returns 0 for "this pipeline fetches no vertex
 * attributes", which is a legitimate result, so failure needs a value that
 * is not a valid GPU address and is not zero.
 */
#define D12_VB_PARAMS_FAILED ((uint64_t)~0ull)

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

   /* One AGX_SAMPLER: the sampler state a texel fetch reads as ss0,
    * followed by the tessellation heap's `struct poly_heap` descriptor.
    */
   struct agx_bo *rodata_bo;

   /* THE TESSELLATION HEAP. Unlike geometry, tessellation genuinely needs
    * it: the tessellator allocates domain points from it, and the prefix
    * sum allocates the final index buffer out of it. The whole heap is
    * then bound as the index buffer, with out_draws[2] carrying the
    * element offset -- so it must be one contiguous allocation.
    *
    * `bottom` is reset per command list. hk queues a GPU write instead,
    * because a Vulkan command buffer can be resubmitted; a D3D12 command
    * list must be Reset before re-recording, so the CPU can zero it then.
    */
   struct agx_bo *heap_bo;
   uint64_t heap_desc_va; /* struct poly_heap inside rodata_bo */

   /* Zeroed backing for NULL descriptors, also inside rodata_bo. A null
    * SRV/UAV/CBV must read as zero, not fault; see D12_NULL_REGION_SIZE.
    */
   uint64_t null_va;
   struct poly_heap *heap_desc_cpu;

   D3D_FEATURE_LEVEL feature_level;

   /* Debug: the geometry vertex output buffer, dumped at device
    * destruction when D3D12AGX_DUMP_GS_VB is set. By then every
    * submission has completed.
    */
   void *dbg_gs_vb;
   size_t dbg_gs_vb_size;
   void *dbg_tess_draw;
   void *dbg_tess_counts;
   unsigned dbg_tess_counts_n;
   void *dbg_mesh_grid;
   void *dbg_mesh_disp;
   void *dbg_mesh_verts;
   void *dbg_mesh_idx;
   void *dbg_tcs_buf;
   unsigned dbg_tcs_buf_n;

   /* Set when a submission fails. D3D12 models this as device removal, and
    * without it a later Signal would advance a fence past work that never
    * ran, making every result downstream a lie.
    */
   bool removed;

   /*
    * WHY the device was removed. D3D12 distinguishes causes:
    * DXGI_ERROR_INVALID_CALL for an application error such as submitting a
    * command list still in the recording state, DXGI_ERROR_DEVICE_REMOVED
    * for a failure below us. GetDeviceRemovedReason returns this, and it is
    * the only signal an application gets about which of the two happened.
    */
   HRESULT removed_reason;
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

   /*
    * RESOLVED GPU ADDRESS, the same pattern hk uses for a bound image
    * (hk_image.c: plane->addr = mem->bo->va->addr + offset). A COMMITTED
    * resource owns its whole BO and sits at offset 0; a PLACED resource is
    * a window into a heap's BO. Everything downstream must read this and
    * never bo->va->addr, or a placed resource silently addresses the start
    * of the heap instead of its own storage.
    */
   uint64_t addr;
   uint64_t stencil_addr;   /* separate plane, same rule */
   uint64_t offset_B;       /* into bo, for CPU maps */
   bool placed;             /* does not own bo */
   struct d12_heap *heap;   /* referenced while placed; NULL if committed */

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
 * ID3D12Heap: one agx_bo of the requested size. On UMA every heap type is
 * the same memory, so a heap really is just an allocation that placed
 * resources carve windows out of.
 *
 * Placed resources hold a REFERENCE to it: D3D12 explicitly allows the
 * application to release the heap while placed resources are still alive,
 * and the storage must outlive them.
 */
struct d12_heap {
   ID3D12Heap iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   D3D12_HEAP_DESC desc;
   struct agx_bo *bo;
};

static inline struct d12_heap *
d12_memheap_from_iface(ID3D12Heap *iface)
{
   return (struct d12_heap *)iface;
}

HRESULT d12_heap_create(struct d12_device *device,
                        const D3D12_HEAP_DESC *desc, REFIID riid,
                        void **out);

HRESULT d12_resource_create_placed(struct d12_device *device,
                                   struct d12_heap *heap, uint64_t offset_B,
                                   const D3D12_RESOURCE_DESC *desc,
                                   D3D12_RESOURCE_STATES initial_state,
                                   const D3D12_CLEAR_VALUE *clear_value,
                                   REFIID riid, void **out);

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

enum d12_buffer_view_kind {
   D12_BUFFER_VIEW_NONE = 0,
   D12_BUFFER_VIEW_TYPED,
   D12_BUFFER_VIEW_RAW,
   D12_BUFFER_VIEW_STRUCTURED,
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
   uint8_t buffer_kind; /* enum d12_buffer_view_kind */
   uint8_t pad[2];
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
 * A query heap: storage for Count results of one type. The heap is just
 * a buffer; Begin/End/Resolve give it meaning.
 */
struct d12_query_heap {
   ID3D12QueryHeap iface; /* must be first */
   uint32_t refcount;

   struct d12_device *device; /* holds a device ref */
   D3D12_QUERY_HEAP_DESC desc;
   unsigned stride;   /* bytes per result */
   struct agx_bo *bo;
};

unsigned d12_query_result_size(D3D12_QUERY_HEAP_TYPE type);
HRESULT d12_query_heap_create(struct d12_device *device,
                              const D3D12_QUERY_HEAP_DESC *desc, REFIID riid,
                              void **out);

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

   /* The compiled part's own BO, and the address of its preamble inside
    * it. agx_fast_link links main + prolog + epilog only: the PREAMBLE is
    * not copied into linked.bo, so info.preamble_offset is meaningless
    * there. hk keeps the same thing as hk_shader::preamble_addr.
    *
    * Zero when the shader has no preamble.
    */
   struct agx_bo *code_bo;
   uint64_t preamble_va;
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

   /* VERTEX ATTRIBUTES. Resolved at PSO creation by matching each
    * D3D12_INPUT_ELEMENT_DESC's (SemanticName, SemanticIndex) against the
    * vertex shader's own DXIL input signature, so the index here is the
    * signature element id -- which is what agx_nir_lower_vbo keys on.
    *
    * nr_vb_attribs is 0 for a PSO with no input layout, and that case
    * keeps the old static_vi prolog exactly as it was.
    */
   struct d12_vb_attrib {
      bool live;
      uint8_t slot;              /* D3D12 InputSlot -> which VBV */
      uint32_t offset_B;         /* AlignedByteOffset */
      enum pipe_format format;
      uint32_t interchange_align; /* to turn a byte stride into elements */
   } vb_attribs[D12_MAX_VB_ATTRIBS];
   unsigned nr_vb_attribs;

   /* Graphics */
   struct d12_gfx_stage vs, fs;
   /* Exact layout used while lowering fragment outputs. A lazily opened
    * render pass must publish the same layout to the hardware even when no
    * colour attachment is bound (the shader's outputs are then discarded at
    * EOT, but they still exist in the tilebuffer while rasterizing).
    */
   struct agx_tilebuffer_layout tib;
   struct agx_unlinked_uvs_layout uvs;
   struct agx_varyings_vs varyings;
   struct d12_depth_stencil_state ds;
   struct d12_raster_state raster;

   /* GEOMETRY. AGX has no hardware geometry stage: a GS becomes compute
    * programs whose output the rasterization draw consumes, so a
    * geometry pipeline carries three extra programs rather than one.
    *
    *   sw_vs    the app's vertex shader, rewritten by
    *            poly_nir_lower_vs_before_gs to write its outputs to a
    *            buffer instead of the rasterizer. Compute.
    *   gs_main  the geometry shader body. Compute. Often empty --
    *            poly deletes its store_output, so a GS with no side
    *            effects and static topology compiles to a few bytes.
    *   gs_rast  re-runs the GS body once per output vertex and feeds the
    *            rasterizer, so it is the HARDWARE vertex shader and
    *            replaces pso->vs at draw time.
    *
    * The compute programs are plain code BOs like the compute PSO's;
    * gs_rast is fast-linked like any other vertex stage.
    */
   bool has_gs;
   struct poly_gs_info gs_info;

   /* The software VS output mask, captured BEFORE the poly lowering
    * erases it. The geometry programs address the vertex buffer with it.
    */
   uint64_t sw_vs_outputs;

   struct agx_bo *sw_vs_bo, *gs_main_bo;
   uint64_t sw_vs_va, gs_main_va;
   struct agx_shader_info sw_vs_info, gs_main_info;

   struct d12_gfx_stage gs_rast;
   struct agx_unlinked_uvs_layout gs_uvs;
   struct agx_varyings_vs gs_varyings;

   /* TESSELLATION. Same shape as geometry: the vertex shader becomes a
    * compute prepass, the hull shader is compute, and the DOMAIN shader
    * becomes the hardware vertex shader -- the tessellator writes an
    * index buffer and the domain shader is invoked per index.
    *
    * The mode/spacing/winding are declared by the SHADERS, not by API
    * state, and either stage may declare them, so they are merged.
    */
   bool has_tess;
   struct agx_bo *tcs_bo;
   uint64_t tcs_va;
   struct agx_shader_info tcs_info;

   struct d12_gfx_stage tes;
   struct agx_unlinked_uvs_layout tes_uvs;
   struct agx_varyings_vs tes_varyings;

   uint32_t tcs_output_patch_size;
   uint64_t tcs_per_vertex_outputs;
   uint32_t tcs_nr_patch_outputs;
   uint32_t tcs_output_stride;

   /* MESH. There is no hk or poly emulation to borrow -- hk implements
    * no mesh shaders at all -- so the whole path is this driver's own.
    * The mesh shader is compiled as compute and writes through
    * D12_UNIFORM_MESH_PARAMS; two internal NIR shaders complete it:
    *
    *   ms_setup  reads the GPU-written {vertices, primitives} counts and
    *             fills the 5-word indirect draw descriptor. The counts
    *             only exist after the mesh shader has run, so this
    *             cannot be done on the CPU.
    *   ms_rast   the hardware vertex shader: reads the mesh shader's
    *             per-vertex output slots and feeds the rasterizer.
    */
   bool has_mesh;
   struct agx_bo *ms_bo;
   uint64_t ms_va;
   struct agx_shader_info ms_info;
   uint32_t ms_workgroup[3];

   struct agx_bo *ms_setup_bo;
   uint64_t ms_setup_va;
   struct agx_shader_info ms_setup_info;

   /* Amplification: runs before the mesh shader, writes the payload and
    * the DispatchMesh grid; as_setup turns that grid into the six-word
    * indirect-local form the mesh dispatch consumes.
    */
   bool has_amp;
   struct agx_bo *as_bo, *as_setup_bo;
   uint64_t as_va, as_setup_va;
   struct agx_shader_info as_info, as_setup_info;
   uint32_t as_workgroup[3];
   uint32_t as_payload_size;

   struct d12_gfx_stage ms_rast;
   struct agx_unlinked_uvs_layout ms_uvs;
   struct agx_varyings_vs ms_varyings;
   uint64_t ms_outputs;

   enum tess_primitive_mode tess_mode;
   enum gl_tess_spacing tess_spacing;
   bool tess_ccw, tess_points;
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

   /*
    * Recording storage (struct agx_bo *) belonging to lists that were Reset
    * while their previous submission could still be executing.
    *
    * D3D12 lets a list be Reset immediately after ExecuteCommandLists: the
    * ALLOCATOR owns the recorded commands, and it is the allocator the
    * application must not reset until the GPU is done. We record into
    * list-owned BOs, so on such a Reset the old BOs are parked here instead
    * of being rewound under the GPU, and released at allocator Reset -- the
    * point where D3D12 guarantees completion.
    */
   struct util_dynarray retired_bos;
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
   /* Base address of the render target SLICE this pass writes. A
    * TEXTURE2DARRAY RTV selects one layer, and layers are contiguous at
    * layer_stride_B, so the view carries an offset the resource does not.
    * Using rt->bo->va->addr here would write every array slice to layer 0.
    */
   uint64_t rt_address;

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
   enum pipe_format rt_view_format;
   uint32_t bg_usc, eot_usc; /* agx_usc_addr()-encoded */
   uint32_t partial_bg_usc, partial_eot_usc;
   struct agx_counts_packed bg_counts, eot_counts;
   struct agx_counts_packed partial_bg_counts, partial_eot_counts;
   bool has_draws;
   bool process_empty_tiles;

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

   /* THE PRE-GRAPHICS COMPUTE STREAM.
    *
    * Geometry, tessellation, mesh and work-graph stages have no hardware
    * stage on AGX: they run as compute whose output the following DRAW
    * consumes. So those dispatches must be ordered BEFORE a draw whose
    * render pass is already open — and `d12_list_get_cdm` cannot do it,
    * because it resolves the collision by ending the render pass, and a
    * render pass can only be opened by a clear (see d12_list_get_render).
    *
    * Honeykrisp keeps two control streams per command buffer and splices
    * the compute one in ahead of the graphics one (hk_cmd_buffer.h:615).
    * This is that, using a property of our segment array: a segment is
    * just a [stream_start, stream_end) range, and segments are submitted
    * in ARRAY order, so the bytes need not be laid out in that order.
    * The pre-gfx stream is therefore carved out of the data area and its
    * segment inserted before the open render segment.
    *
    * pregfx_seg is only meaningful while pregfx_open.
    */
   uint8_t *pregfx_cur;  /* next control word write, or NULL */
   uint8_t *pregfx_end;  /* one past the region: overflow check */
   unsigned pregfx_seg;  /* index of the inserted segment */
   bool pregfx_open;

   /* Sticky: set when recording hit an error, and NOT cleared by Close.
    * Close must still transition the list to CLOSED so the allocator can
    * reset, but an errored list's command stream is incomplete and must
    * never reach the GPU -- submitting one is how the bench wedged.
    */
   bool had_error;

   /* Set when a draw in this list bump-allocates from the device
    * tessellation heap. Close then appends a GPU-side write zeroing
    * poly_heap::bottom, so the heap is freed once per EXECUTION rather
    * than once per Reset -- a closed list may be executed repeatedly with
    * no Reset between, and each execution allocates again.
    */
   bool uses_heap;

   /* Set once this recording has been handed to the GPU. Reset must then
    * take fresh storage rather than rewinding what the GPU may still be
    * reading. Cleared when fresh storage is taken.
    */
   bool submitted;

   /* Bound state */
   /* IASetVertexBuffers. Recorded per slot; the draw path turns these,
    * plus the PSO's resolved attributes, into the fetch parameter block.
    */
   struct {
      uint64_t addr;
      uint32_t size_B;
      uint32_t stride_B;
   } vb[D12_MAX_VB_SLOTS];

   /* IASetIndexBuffer. size_B of zero means nothing is bound, which is
    * distinct from a bound buffer of length zero only in that the latter
    * cannot happen -- D3D12 rejects it.
    */
   struct {
      uint64_t addr;
      uint32_t size_B;
      enum agx_index_size index_size;
   } ib;

   enum mesa_prim topology; /* IASetPrimitiveTopology; TRIANGLES by default */
   /* Control points per patch, from a *_CONTROL_POINT_PATCHLIST topology.
    * Zero when the bound topology is not a patch list -- the only thing
    * API state contributes to tessellation.
    */
   unsigned patch_control_points;
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

   /* Zeroed backing for UNBOUND root slots, allocated lazily and reused by
    * every binding upload in this recording. See d12_list_null_buffer().
    */
   uint64_t null_buffer;

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

   /* OM bindings are descriptor snapshots, not just resource pointers: RTV
    * array slices share a resource but have different addresses, and typeless
    * resources acquire their interpretation from the view.
    */
   struct d12_descriptor bound_rtv;
   struct d12_descriptor bound_dsv;
   bool has_bound_rtv;
   bool has_bound_dsv;

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
d12_cmd_list_from_iface(void *iface)
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
d12_device_from_iface(void *iface)
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
                                      D3D12_RESOURCE_STATES initial_state,
                                      const D3D12_CLEAR_VALUE *clear_value,
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
HRESULT d12_pipeline_state_create_mesh(
   struct d12_device *device, const void *ms_dxil, size_t ms_size,
   const void *as_dxil, size_t as_size, const void *ps_dxil, size_t ps_size,
   enum pipe_format rt_format, const D3D12_DEPTH_STENCIL_DESC *ds_desc,
   DXGI_FORMAT dsv_format, const D3D12_RASTERIZER_DESC *rs_desc,
   const D3D12_BLEND_DESC *blend_desc,
   const struct d12_binding_map *map, REFIID riid, void **out);

HRESULT d12_pipeline_state_create_graphics(
   struct d12_device *device, const void *vs_dxil, size_t vs_size,
   const void *ps_dxil, size_t ps_size, const void *gs_dxil, size_t gs_size,
   const void *hs_dxil, size_t hs_size, const void *ds_dxil, size_t ds_size,
   enum pipe_format rt_format,
   const D3D12_DEPTH_STENCIL_DESC *ds_desc, DXGI_FORMAT dsv_format,
   const D3D12_RASTERIZER_DESC *rs_desc,
   const D3D12_BLEND_DESC *blend_desc,
   const D3D12_INPUT_LAYOUT_DESC *input_layout,
   const struct d12_binding_map *map, REFIID riid, void **out);

void d12_device_add_ref(struct d12_device *device);
void d12_device_release(struct d12_device *device);
