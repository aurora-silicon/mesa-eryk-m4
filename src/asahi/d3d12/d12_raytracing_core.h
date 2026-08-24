/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * DXR host-side contract: shader tables, shader identifiers, DispatchRays
 * argument rules and acceleration-structure sizing.
 *
 * WHY THIS EXISTS BEFORE TRAVERSAL DOES
 *
 * Everything here is required no matter how rays are eventually traced, and
 * none of it depends on how they are.  The shader-table geometry, the record
 * and table alignments, the DispatchRays bounds and the acceleration-structure
 * size contract are fixed by the API, so they can be built and tested now,
 * against nothing.  What cannot be built now is traversal itself:
 *
 *   - Apple hardware ray tracing begins at G15 (M3).  G13/G14 (M1/M2), which
 *     is what this driver runs on today, have no ray-tracing hardware at all.
 *   - G15 and G16 are not supported by Mesa in any form yet.
 *   - On G17 (M5), where the hardware exists and has been studied, the
 *     traversal core of the instruction set is 96.8% ungrounded: instruction
 *     boundaries in the ~988-byte inlined intersect block have not been
 *     solved, so no traversal code can be emitted.  See
 *     research/gpu/GPU_AGX3_RT_ISA_2026-07-23.md.
 *   - Apple's BVH8 acceleration-structure format is likewise undocumented;
 *     only its builder kernel names are known.
 *
 * So the driver reports D3D12_RAYTRACING_TIER_NOT_SUPPORTED and will keep
 * reporting it until a traversal path exists.  What this file buys is that
 * when one does -- hardware traversal on a later part, or a compute-shader
 * traversal over a BVH of our own design, which the API permits because an
 * acceleration structure is opaque to the application -- the host half is
 * already written and covered.
 *
 * Keep this free of Windows and Mesa headers.
 */

#ifndef D12_RAYTRACING_CORE_H
#define D12_RAYTRACING_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed by the API (d3d12.h). Reproduced so this stays header-free; the
 * device translation unit static-asserts them against the real values. */
#define D12_RT_SHADER_IDENTIFIER_SIZE        32u
#define D12_RT_SHADER_RECORD_BYTE_ALIGNMENT  32u
#define D12_RT_SHADER_TABLE_BYTE_ALIGNMENT   64u
#define D12_RT_MAX_SHADER_RECORD_STRIDE      4096u
#define D12_RT_AS_BYTE_ALIGNMENT             256u
#define D12_RT_MAX_ATTRIBUTE_SIZE            32u
#define D12_RT_MAX_DECLARABLE_RECURSION      31u
#define D12_RT_MAX_GEOMETRIES_PER_BLAS       16777216u
#define D12_RT_MAX_INSTANCES_PER_TLAS        16777216u
#define D12_RT_MAX_PRIMITIVES_PER_BLAS       536870912u
#define D12_RT_MAX_RAYGEN_THREADS            1073741824u

/*
 * A shader table is an array of equally sized records, each beginning with a
 * 32-byte shader identifier and followed by that shader's local root
 * arguments.  Three of the four tables DispatchRays takes are strided arrays;
 * the ray-generation "table" is a single record and therefore has no stride.
 */
struct d12_rt_shader_table {
   uint64_t start_address;
   uint64_t size_bytes;
   uint64_t stride_bytes; /* zero for the ray-generation record */
};

enum d12_rt_table_kind {
   D12_RT_TABLE_RAY_GENERATION = 0, /* one record, no stride */
   D12_RT_TABLE_STRIDED,            /* miss, hit group, callable */
};

/*
 * Validate one shader table.
 *
 * The rules are the API's and they are all alignment or containment
 * statements, which is exactly the class of mistake that produces a GPU
 * reading records from the wrong offsets rather than an error.
 */
static inline bool
d12_rt_shader_table_valid(const struct d12_rt_shader_table *table,
                          enum d12_rt_table_kind kind)
{
   if (!table)
      return false;

   /* An absent table is legal: a pipeline with no miss shaders binds none. */
   if (!table->start_address && !table->size_bytes && !table->stride_bytes)
      return true;

   if (!table->start_address || !table->size_bytes)
      return false;
   if (table->start_address % D12_RT_SHADER_TABLE_BYTE_ALIGNMENT)
      return false;
   if (table->start_address > UINT64_MAX - table->size_bytes)
      return false;

   /* A record has to be at least an identifier, and the table has to hold at
    * least one whole record. */
   if (table->size_bytes < D12_RT_SHADER_IDENTIFIER_SIZE)
      return false;

   if (kind == D12_RT_TABLE_RAY_GENERATION) {
      /* One record: a stride would have nothing to stride over. */
      return table->stride_bytes == 0;
   }

   if (!table->stride_bytes)
      return false;
   if (table->stride_bytes % D12_RT_SHADER_RECORD_BYTE_ALIGNMENT)
      return false;
   if (table->stride_bytes > D12_RT_MAX_SHADER_RECORD_STRIDE)
      return false;
   if (table->stride_bytes < D12_RT_SHADER_IDENTIFIER_SIZE)
      return false;
   /* The table must contain whole records. A partial trailing record would
    * be indexable by a shader whose index the driver cannot bound. */
   return table->size_bytes % table->stride_bytes == 0;
}

/* Records addressable in a strided table. Zero for the single-record
 * ray-generation table, whose index is always implicitly zero. */
static inline uint64_t
d12_rt_shader_table_records(const struct d12_rt_shader_table *table)
{
   if (!table || !table->stride_bytes || !table->size_bytes)
      return 0;
   return table->size_bytes / table->stride_bytes;
}

struct d12_rt_dispatch_rays {
   struct d12_rt_shader_table ray_generation;
   struct d12_rt_shader_table miss;
   struct d12_rt_shader_table hit_group;
   struct d12_rt_shader_table callable;
   uint32_t width;
   uint32_t height;
   uint32_t depth;
};

/*
 * DispatchRays argument validation.
 *
 * The thread-count limit is a product, not a per-dimension bound: D3D12 caps
 * the total number of ray-generation invocations, so three individually legal
 * dimensions can still be an illegal dispatch.
 */
static inline bool
d12_rt_dispatch_valid(const struct d12_rt_dispatch_rays *dispatch)
{
   if (!dispatch)
      return false;

   /* A zero-sized dispatch is legal and does nothing. */
   if (!dispatch->width || !dispatch->height || !dispatch->depth) {
      return !dispatch->width && !dispatch->height && !dispatch->depth;
   }

   const uint64_t threads = (uint64_t)dispatch->width *
                            (uint64_t)dispatch->height *
                            (uint64_t)dispatch->depth;
   if (threads > D12_RT_MAX_RAYGEN_THREADS)
      return false;

   /* Rays have to come from somewhere. */
   if (!dispatch->ray_generation.start_address)
      return false;

   return d12_rt_shader_table_valid(&dispatch->ray_generation,
                                    D12_RT_TABLE_RAY_GENERATION) &&
          d12_rt_shader_table_valid(&dispatch->miss, D12_RT_TABLE_STRIDED) &&
          d12_rt_shader_table_valid(&dispatch->hit_group,
                                    D12_RT_TABLE_STRIDED) &&
          d12_rt_shader_table_valid(&dispatch->callable,
                                    D12_RT_TABLE_STRIDED);
}

/*
 * Acceleration structures.
 *
 * The application never sees inside one: it receives a GPU virtual address
 * and a size from GetRaytracingAccelerationStructurePrebuildInfo, and the
 * layout in between is entirely the driver's. That is what makes a
 * driver-defined BVH legal, and it is why the sizing contract can be settled
 * now even though the format cannot.
 *
 * The contract the API does impose:
 *   - every returned size is a multiple of the 256-byte AS alignment;
 *   - the destination must be at least ResultDataMaxSizeInBytes;
 *   - a build needs ScratchDataSizeInBytes of scratch, and an update needs
 *     UpdateScratchDataSizeInBytes, which is only meaningful when the build
 *     asked to ALLOW_UPDATE.
 */
struct d12_rt_prebuild_info {
   uint64_t result_max_size;
   uint64_t scratch_size;
   uint64_t update_scratch_size;
};

#define D12_RT_BUILD_FLAG_ALLOW_UPDATE      0x01u
#define D12_RT_BUILD_FLAG_ALLOW_COMPACTION  0x02u
#define D12_RT_BUILD_FLAG_PREFER_FAST_TRACE 0x04u
#define D12_RT_BUILD_FLAG_PREFER_FAST_BUILD 0x08u
#define D12_RT_BUILD_FLAG_MINIMIZE_MEMORY   0x10u
#define D12_RT_BUILD_FLAG_PERFORM_UPDATE    0x20u
#define D12_RT_BUILD_FLAGS_ALL              0x3fu

static inline uint64_t
d12_rt_align_as(uint64_t size)
{
   const uint64_t alignment = D12_RT_AS_BYTE_ALIGNMENT;
   if (size > UINT64_MAX - (alignment - 1))
      return 0;
   return (size + alignment - 1) & ~(alignment - 1);
}

/*
 * Build flags that contradict each other are the application's error, but a
 * driver that accepts them has to pick one and will pick differently from the
 * runtime. Refuse instead.
 */
static inline bool
d12_rt_build_flags_valid(uint32_t flags)
{
   if (flags & ~D12_RT_BUILD_FLAGS_ALL)
      return false;
   /* Fast trace and fast build are opposite requests. */
   if ((flags & D12_RT_BUILD_FLAG_PREFER_FAST_TRACE) &&
       (flags & D12_RT_BUILD_FLAG_PREFER_FAST_BUILD))
      return false;
   /* An update in place is only possible if the original build allowed it. */
   if ((flags & D12_RT_BUILD_FLAG_PERFORM_UPDATE) &&
       !(flags & D12_RT_BUILD_FLAG_ALLOW_UPDATE))
      return false;
   return true;
}

/*
 * Whether a destination and scratch pair satisfies a prebuild result.
 * Addresses are GPU virtual addresses and both must carry the AS alignment;
 * scratch shares it because the builder addresses it the same way.
 */
static inline bool
d12_rt_build_allocation_valid(const struct d12_rt_prebuild_info *info,
                              uint64_t destination, uint64_t destination_size,
                              uint64_t scratch, uint64_t scratch_size,
                              bool updating)
{
   if (!info || !destination || !scratch)
      return false;
   if (destination % D12_RT_AS_BYTE_ALIGNMENT ||
       scratch % D12_RT_AS_BYTE_ALIGNMENT)
      return false;
   if (destination_size < info->result_max_size)
      return false;

   const uint64_t required =
      updating ? info->update_scratch_size : info->scratch_size;
   if (!required)
      return false;
   return scratch_size >= required;
}

/*
 * A shader identifier is 32 opaque bytes the application copies into a shader
 * record verbatim. Its only contract is that identifiers compare equal for
 * the same export in the same state object and unequal otherwise, so the
 * driver is free to make it a tagged index.
 *
 * Tagging matters: these bytes reach the GPU inside application-authored
 * memory, so a record the application built wrongly, or left uninitialised,
 * must be distinguishable from a real identifier rather than interpreted as
 * one.
 */
#define D12_RT_IDENTIFIER_MAGIC UINT32_C(0x52544149) /* 'IATR' */

struct d12_rt_shader_identifier {
   uint32_t magic;
   uint32_t state_object_id;
   uint32_t export_index;
   uint32_t kind; /* enum d12_rt_export_kind */
   uint64_t reserved[2];
};

enum d12_rt_export_kind {
   D12_RT_EXPORT_RAY_GENERATION = 0,
   D12_RT_EXPORT_MISS,
   D12_RT_EXPORT_HIT_GROUP,
   D12_RT_EXPORT_CALLABLE,
};

static inline bool
d12_rt_identifier_pack(uint32_t state_object_id, uint32_t export_index,
                       enum d12_rt_export_kind kind,
                       struct d12_rt_shader_identifier *out)
{
   if (!out || kind > D12_RT_EXPORT_CALLABLE)
      return false;

   *out = (struct d12_rt_shader_identifier){
      .magic = D12_RT_IDENTIFIER_MAGIC,
      .state_object_id = state_object_id,
      .export_index = export_index,
      .kind = (uint32_t)kind,
   };
   return true;
}

static inline bool
d12_rt_identifier_unpack(const struct d12_rt_shader_identifier *identifier,
                         uint32_t *state_object_id, uint32_t *export_index,
                         enum d12_rt_export_kind *kind)
{
   if (!identifier || identifier->magic != D12_RT_IDENTIFIER_MAGIC ||
       identifier->kind > D12_RT_EXPORT_CALLABLE ||
       identifier->reserved[0] || identifier->reserved[1])
      return false;

   if (state_object_id)
      *state_object_id = identifier->state_object_id;
   if (export_index)
      *export_index = identifier->export_index;
   if (kind)
      *kind = (enum d12_rt_export_kind)identifier->kind;
   return true;
}

/*
 * A local root signature's arguments follow the identifier inside the record,
 * so a record is only usable if the stride can hold both.
 */
static inline bool
d12_rt_record_fits(uint64_t stride_bytes, uint64_t local_root_argument_bytes)
{
   if (local_root_argument_bytes >
       UINT64_MAX - D12_RT_SHADER_IDENTIFIER_SIZE)
      return false;
   return stride_bytes >=
          D12_RT_SHADER_IDENTIFIER_SIZE + local_root_argument_bytes;
}

/*
 * Recursion depth. D3D12 lets a state object declare a maximum, and the
 * declared depth is a contract the driver has to be able to honour: each
 * level needs its own attribute and payload storage.
 */
static inline bool
d12_rt_pipeline_config_valid(uint32_t max_recursion_depth,
                             uint32_t max_attribute_size)
{
   return max_recursion_depth <= D12_RT_MAX_DECLARABLE_RECURSION &&
          max_attribute_size <= D12_RT_MAX_ATTRIBUTE_SIZE &&
          /* Attributes are the barycentrics at minimum: two floats. */
          max_attribute_size >= 8u;
}

/* Geometry and instance counts the API bounds. */
static inline bool
d12_rt_blas_counts_valid(uint64_t geometry_count, uint64_t primitive_count)
{
   return geometry_count <= D12_RT_MAX_GEOMETRIES_PER_BLAS &&
          primitive_count <= D12_RT_MAX_PRIMITIVES_PER_BLAS;
}

static inline bool
d12_rt_tlas_counts_valid(uint64_t instance_count)
{
   return instance_count <= D12_RT_MAX_INSTANCES_PER_TLAS;
}

#ifdef __cplusplus
}
#endif

#endif
