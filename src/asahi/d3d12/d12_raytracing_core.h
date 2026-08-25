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
 *   - G15/G16 identity and ISA discovery have begun, but their execution and
 *     command-stream paths are not available yet.
 *   - On G17 (M5), traversal prototypes and intrinsic tables are grounded,
 *     but the instruction semantics are not yet complete enough to emit a
 *     hardware traversal shader.
 *   - The RIA Gen1 BVH8 branching/leaf layout and quantized child fields, and
 *     the Gen5 child-section addressing, are now solved exactly and exposed
 *     below.  The public builder capture taken on an M5 Pro emitted a
 *     version-3/Gen1 blob, not a Gen5 blob; the Gen5 QTB/VRB compressed
 *     payload layouts and a live version-5 builder capture remain separate
 *     traversal work.
 *
 * So the driver reports D3D12_RAYTRACING_TIER_NOT_SUPPORTED and will keep
 * reporting it until a traversal path exists.  What this file buys is that
 * when one does -- hardware traversal on a later part, or a compute-shader
 * traversal over a BVH of our own design, which the API permits because an
 * acceleration structure is opaque to the application -- the host half and
 * the proved Gen1 and Gen5 child-layout decoders are already written and
 * covered.
 *
 * Keep this free of Windows headers and Mesa runtime state.  The shared AGX
 * packer and BVH helpers included here are standard-C-only contracts.
 */

#ifndef D12_RAYTRACING_CORE_H
#define D12_RAYTRACING_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asahi/compiler/agx3_pack.h"
#include "../lib/agx_rt_bvh.h"

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
   if (!table || kind > D12_RT_TABLE_STRIDED)
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
      /* One record: a stride would have nothing to stride over.  The API
       * bounds the ray-generation range to the maximum record size. */
      return table->stride_bytes == 0 &&
             table->size_bytes <= D12_RT_MAX_SHADER_RECORD_STRIDE;
   }

   /* DXR deliberately permits a zero stride. Every index then selects the
    * same first record; this is useful when a whole table shares one shader
    * and one set of local root arguments. */
   if (!table->stride_bytes)
      return true;
   if (table->stride_bytes % D12_RT_SHADER_RECORD_BYTE_ALIGNMENT)
      return false;
   if (table->stride_bytes > D12_RT_MAX_SHADER_RECORD_STRIDE)
      return false;
   if (table->stride_bytes < D12_RT_SHADER_IDENTIFIER_SIZE)
      return false;
   return true;
}

/* Number of minimum-sized records whose identifiers fit in a table.  With a
 * zero stride every logical index aliases the same one physical record, so
 * this reports one rather than attempting to describe the unbounded alias. */
static inline uint64_t
d12_rt_shader_table_records(const struct d12_rt_shader_table *table)
{
   if (!d12_rt_shader_table_valid(table, D12_RT_TABLE_STRIDED) ||
       !table->start_address ||
       table->size_bytes < D12_RT_SHADER_IDENTIFIER_SIZE)
      return 0;
   if (!table->stride_bytes)
      return 1;
   return 1 + ((table->size_bytes - D12_RT_SHADER_IDENTIFIER_SIZE) /
               table->stride_bytes);
}

/* Resolve an indexed record without permitting integer wrap or a record to
 * extend beyond SizeInBytes.  The caller supplies the actual record size,
 * including its local root arguments; unlike a coarse table validator this
 * makes the last partially occupied stride checkable exactly. */
static inline bool
d12_rt_shader_record_address(const struct d12_rt_shader_table *table,
                             enum d12_rt_table_kind kind, uint64_t index,
                             uint64_t record_size, uint64_t *address)
{
   if (!address || !d12_rt_shader_table_valid(table, kind) ||
       !table->start_address ||
       record_size < D12_RT_SHADER_IDENTIFIER_SIZE ||
       record_size > D12_RT_MAX_SHADER_RECORD_STRIDE)
      return false;

   if (kind == D12_RT_TABLE_RAY_GENERATION) {
      if (index || record_size > table->size_bytes)
         return false;
      *address = table->start_address;
      return true;
   }

   if (record_size > table->size_bytes)
      return false;

   if (!table->stride_bytes) {
      *address = table->start_address;
      return true;
   }

   /* The stride has to hold the largest shader record in the table. */
   if (record_size > table->stride_bytes ||
       index > UINT64_MAX / table->stride_bytes)
      return false;

   const uint64_t offset = index * table->stride_bytes;
   if (offset > table->size_bytes - record_size ||
       table->start_address > UINT64_MAX - offset)
      return false;

   *address = table->start_address + offset;
   return true;
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

   /* If any dimension is zero, the dispatch is a legal no-op and accesses no
    * shader record. The other dimensions and table ranges are immaterial. */
   if (!dispatch->width || !dispatch->height || !dispatch->depth)
      return true;

   /* Check the product against the API limit without overflowing uint64_t:
    * three UINT dimensions can have a 96-bit mathematical product. */
   uint64_t threads = dispatch->width;
   if (threads > D12_RT_MAX_RAYGEN_THREADS / dispatch->height)
      return false;
   threads *= dispatch->height;
   if (threads > D12_RT_MAX_RAYGEN_THREADS / dispatch->depth)
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

/* AGX3 encoding variants used by the bounded RT representations below. */
enum d12_rt_agx3_encoding {
   D12_RT_AGX3_ENCODING_B = 0,
   D12_RT_AGX3_ENCODING_C,
};

struct d12_rt_agx3_update_node_expansion {
   uint8_t bytes[AGX3_UPDATE_NODE_MAX_LENGTH];
   uint8_t length;
};

struct d12_rt_agx3_continue_ray_instruction {
   uint8_t bytes[AGX3_CONTINUE_RAY_MAX_LENGTH];
   uint8_t length;
};

struct d12_rt_agx3_continue_ray_fixed_first_site_witness {
   uint8_t bytes[AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH];
   uint8_t length;
};

struct d12_rt_agx3_update_bvh_fixed_witness {
   uint8_t bytes[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH];
   uint8_t length;
};

static inline enum agx_isa_encoding
d12_rt_agx3_shared_encoding(enum d12_rt_agx3_encoding encoding)
{
   switch (encoding) {
   case D12_RT_AGX3_ENCODING_B:
      return AGX_ISA_ENCODING_AGX3B;
   case D12_RT_AGX3_ENCODING_C:
      return AGX_ISA_ENCODING_AGX3C;
   default:
      return AGX_ISA_ENCODING_UNKNOWN;
   }
}

/*
 * Generate the complete canonical llvm.agx3.update.node expansion through
 * the shared compiler packer: the generation-specific six-byte barrier and
 * the fixed update_node operation that follows it.  This is an exact byte
 * representation only, not RT lowering or permission to submit it.
 */
static inline bool
d12_rt_agx3_update_node_expansion_generate(
   enum d12_rt_agx3_encoding encoding,
   struct d12_rt_agx3_update_node_expansion *out)
{
   if (!out)
      return false;

   *out = (struct d12_rt_agx3_update_node_expansion){0};

   struct agx3_encoded_update_node shared;
   if (!agx3_pack_update_node(d12_rt_agx3_shared_encoding(encoding),
                              &shared) ||
       shared.length > sizeof(out->bytes))
      return false;

   for (uint8_t i = 0; i < shared.length; ++i)
      out->bytes[i] = shared.bytes[i];
   out->length = shared.length;
   return true;
}

/*
 * Validate one complete expansion against both sides of the shared contract.
 * Exact size is required because agx3_unpack_update_node() deliberately
 * accepts a larger containing buffer.  Repacking and byte comparison also
 * ensures this DXR-facing representation cannot drift from compiler output.
 */
static inline bool
d12_rt_agx3_update_node_expansion_valid(
   enum d12_rt_agx3_encoding encoding, const void *instructions,
   uint32_t instruction_size)
{
   if (!instructions)
      return false;

   const enum agx_isa_encoding shared_encoding =
      d12_rt_agx3_shared_encoding(encoding);
   struct agx3_encoded_update_node packed;
   uint8_t unpacked_length = 0;

   if (!agx3_pack_update_node(shared_encoding, &packed) ||
       instruction_size != packed.length ||
       !agx3_unpack_update_node(shared_encoding, instructions,
                                instruction_size, &unpacked_length) ||
       unpacked_length != packed.length)
      return false;

   const uint8_t *code = instructions;
   for (uint8_t i = 0; i < packed.length; ++i) {
      if (code[i] != packed.bytes[i])
         return false;
   }

   return true;
}

/*
 * Map only the exact targets on which Apple's compiler reproduced the fixed
 * first continue_ray site.  Several other chips share these ISA encodings,
 * so using agx_chip_isa_encoding() directly would silently over-admit them.
 */
static inline enum agx_isa_encoding
d12_rt_agx3_continue_ray_instruction_encoding(enum agx_chip chip)
{
   return agx3_continue_ray_witnessed_chip_encoding(chip);
}

static inline enum agx_isa_encoding
d12_rt_agx3_continue_ray_fixed_first_site_witness_encoding(enum agx_chip chip)
{
   return d12_rt_agx3_continue_ray_instruction_encoding(chip);
}

/*
 * Generate one canonical hardware `continue_ray` instruction through Mesa's
 * shared codec.  `state_register` is the Apple-table f5 half-register field,
 * not a DXR shader operand.  Only per-encoding values and chip identities
 * witnessed in the pinned native corpus are accepted.  This is a bounded
 * machine-code representation; it does not implement DispatchRays, SCS state,
 * traversal lowering, scheduling, submission, or a DXR capability.
 */
static inline bool
d12_rt_agx3_continue_ray_instruction_generate(
   enum agx_chip chip, uint16_t state_register,
   struct d12_rt_agx3_continue_ray_instruction *out)
{
   if (!out)
      return false;

   *out = (struct d12_rt_agx3_continue_ray_instruction){0};

   struct agx3_encoded_continue_ray shared;
   if (!agx3_pack_continue_ray(
          d12_rt_agx3_continue_ray_instruction_encoding(chip),
          state_register, &shared) ||
       shared.length > sizeof(out->bytes))
      return false;

   for (uint8_t i = 0; i < shared.length; ++i)
      out->bytes[i] = shared.bytes[i];
   out->length = shared.length;
   return true;
}

/* Decode exactly one target-approved canonical instruction.  The shared
 * unpacker accepts an instruction prefix in a larger stream; this DXR-facing
 * boundary requires the exact instruction size and re-packs the decoded field
 * so no anonymous control-field variation can cross the boundary. */
static inline bool
d12_rt_agx3_continue_ray_instruction_decode(
   enum agx_chip chip, const void *instruction, uint32_t instruction_size,
   uint16_t *state_register)
{
   if (!instruction || !state_register)
      return false;

   *state_register = 0;
   const enum agx_isa_encoding encoding =
      d12_rt_agx3_continue_ray_instruction_encoding(chip);
   uint16_t decoded = 0;
   uint8_t decoded_length = 0;
   struct agx3_encoded_continue_ray canonical;

   if (!agx3_unpack_continue_ray(encoding, instruction, instruction_size,
                                 &decoded, &decoded_length) ||
       instruction_size != decoded_length ||
       !agx3_pack_continue_ray(encoding, decoded, &canonical) ||
       canonical.length != decoded_length)
      return false;

   const uint8_t *code = instruction;
   for (uint8_t i = 0; i < canonical.length; ++i) {
      if (code[i] != canonical.bytes[i])
         return false;
   }

   *state_register = decoded;
   return true;
}

/*
 * Represent only the fixed first hardware instruction witnessed inside the
 * compiler's much larger continue_ray expansion.  The intrinsic operands and
 * anonymous hardware fields are deliberately absent: this is not traversal
 * lowering and grants no permission to schedule or submit the bytes.
 */
static inline bool
d12_rt_agx3_continue_ray_fixed_first_site_witness_generate(
   enum agx_chip chip,
   struct d12_rt_agx3_continue_ray_fixed_first_site_witness *out)
{
   if (!out)
      return false;

   *out = (struct d12_rt_agx3_continue_ray_fixed_first_site_witness){0};

   const enum agx_isa_encoding encoding =
      d12_rt_agx3_continue_ray_fixed_first_site_witness_encoding(chip);
   struct agx3_encoded_continue_ray_fixed_first_site_witness shared;

   if (!agx3_pack_continue_ray_fixed_first_site_witness(encoding, &shared) ||
       shared.length > sizeof(out->bytes))
      return false;

   for (uint8_t i = 0; i < shared.length; ++i)
      out->bytes[i] = shared.bytes[i];
   out->length = shared.length;
   return true;
}

/* Require one exact fixed first-site witness.  The shared unpacker accepts a
 * canonical prefix in a larger stream, so this DXR-facing boundary checks the
 * complete size before unpacking and comparing with a fresh shared pack. */
static inline bool
d12_rt_agx3_continue_ray_fixed_first_site_witness_valid(
   enum agx_chip chip, const void *instruction, uint32_t instruction_size)
{
   if (!instruction)
      return false;

   const enum agx_isa_encoding encoding =
      d12_rt_agx3_continue_ray_fixed_first_site_witness_encoding(chip);
   struct agx3_encoded_continue_ray_fixed_first_site_witness packed;
   uint8_t unpacked_length = 0;

   if (!agx3_pack_continue_ray_fixed_first_site_witness(encoding, &packed) ||
       instruction_size != packed.length ||
       !agx3_unpack_continue_ray_fixed_first_site_witness(
          encoding, instruction, instruction_size, &unpacked_length) ||
       unpacked_length != packed.length)
      return false;

   const uint8_t *code = instruction;
   for (uint8_t i = 0; i < packed.length; ++i) {
      if (code[i] != packed.bytes[i])
         return false;
   }

   return true;
}

/*
 * Represent only the exact G17S compiler witness for llvm.agx3.update.bvh.
 * The intrinsic has operands whose machine fields are not correlated, so this
 * is deliberately not an operand encoder and grants no permission to lower,
 * schedule or submit the bytes.  G17G and G17C share the AGX3c encoding but
 * have no pinned per-chip witness and therefore remain rejected.
 */
static inline bool
d12_rt_agx3_update_bvh_fixed_witness_generate(
   enum agx_chip chip, struct d12_rt_agx3_update_bvh_fixed_witness *out)
{
   if (!out)
      return false;

   *out = (struct d12_rt_agx3_update_bvh_fixed_witness){0};

   if (chip != AGX_CHIP_G17S)
      return false;

   struct agx3_encoded_update_bvh_fixed_witness shared;
   if (!agx3_pack_update_bvh_fixed_witness(AGX_ISA_ENCODING_AGX3C,
                                            &shared) ||
       shared.length > sizeof(out->bytes))
      return false;

   for (uint8_t i = 0; i < shared.length; ++i)
      out->bytes[i] = shared.bytes[i];
   out->length = shared.length;
   return true;
}

/*
 * Validate the complete fixed witness against both shared packer directions.
 * Exact size is required because the shared unpacker accepts a canonical
 * prefix in a larger instruction buffer.  The chip allowlist is independent
 * of the encoding so another AGX3c chip cannot be admitted accidentally.
 */
static inline bool
d12_rt_agx3_update_bvh_fixed_witness_valid(
   enum agx_chip chip, const void *instructions, uint32_t instruction_size)
{
   if (chip != AGX_CHIP_G17S || !instructions)
      return false;

   struct agx3_encoded_update_bvh_fixed_witness packed;
   uint8_t unpacked_length = 0;

   if (!agx3_pack_update_bvh_fixed_witness(AGX_ISA_ENCODING_AGX3C,
                                            &packed) ||
       instruction_size != packed.length ||
       !agx3_unpack_update_bvh_fixed_witness(
          AGX_ISA_ENCODING_AGX3C, instructions, instruction_size,
          &unpacked_length) ||
       unpacked_length != packed.length)
      return false;

   const uint8_t *code = instructions;
   for (uint8_t i = 0; i < packed.length; ++i) {
      if (code[i] != packed.bytes[i])
         return false;
   }

   return true;
}

/*
 * Exact AGX3 encoding check for the trailing, fixed zero-argument update_node
 * operation by itself.
 *
 * The AGX3b and AGX3c decoder tables specify every bit of this one operation,
 * and the captured disassembler corpus carries the same byte strings.  That
 * makes exact recognition safe even though the operand-bearing RT intrinsics
 * still have unnamed fields and predicates and must not be emitted.
 *
 * Unlike d12_rt_agx3_update_node_expansion_valid(), this validates one
 * instruction only and does not require the generation-specific barrier.
 * Neither helper claims BVH update semantics, traversal, shader lowering or
 * command submission.
 */
static inline bool
d12_rt_agx3_canonical_update_node_valid(enum d12_rt_agx3_encoding encoding,
                                        const void *instruction,
                                        uint32_t instruction_size)
{
   if (!instruction)
      return false;

   const uint8_t *code = instruction;

   switch (encoding) {
   case D12_RT_AGX3_ENCODING_B:
      return instruction_size == 4 && code[0] == 0xdf && code[1] == 0x05 &&
             code[2] == 0x54 && code[3] == 0x00;
   case D12_RT_AGX3_ENCODING_C:
      return instruction_size == 8 && code[0] == 0x27 && code[1] == 0x00 &&
             code[2] == 0x01 && code[3] == 0x00 && code[4] == 0x22 &&
             code[5] == 0x00 && code[6] == 0xad && code[7] == 0x02;
   default:
      return false;
   }
}

/* One proved Apple Gen1 hardware triangle leaf, transposed into per-vertex
 * coordinates for DXR consumers.  The complete ID words remain visible so
 * the still-unnamed high two bits are never silently discarded. */
struct d12_rt_agx_bvh_gen1_hw_triangle_leaf {
   float vertex[4][3];
   uint32_t geometry_word;
   uint32_t primitive_word[2];
   uint32_t geometry_id;
   uint32_t primitive_id[2];
};

static inline bool
d12_rt_agx_bvh_gen1_decode_hw_triangle_leaf(
   const void *packed_leaf, size_t available,
   struct d12_rt_agx_bvh_gen1_hw_triangle_leaf *leaf)
{
   if (!leaf)
      return false;

   memset(leaf, 0, sizeof(*leaf));

   struct agx_rt_bvh_gen1_hw_triangle_leaf decoded;
   if (!agx_rt_bvh_gen1_hw_triangle_leaf_unpack(packed_leaf, available,
                                                &decoded))
      return false;

   for (unsigned vertex = 0; vertex < 4; ++vertex) {
      for (unsigned axis = 0; axis < 3; ++axis)
         leaf->vertex[vertex][axis] = decoded.coordinate[axis][vertex];
   }

   leaf->geometry_word = decoded.geometry_word;
   leaf->primitive_word[0] = decoded.primitive_word[0];
   leaf->primitive_word[1] = decoded.primitive_word[1];
   leaf->geometry_id = agx_rt_bvh_gen1_hw_triangle_geometry_id(&decoded);
   leaf->primitive_id[0] =
      agx_rt_bvh_gen1_hw_triangle_primitive_id(&decoded, 0);
   leaf->primitive_id[1] =
      agx_rt_bvh_gen1_hw_triangle_primitive_id(&decoded, 1);
   return true;
}

/* One exact Apple Gen1 hardware AABB leaf.  This is a research/validation
 * surface only: decoding a captured record does not enable AS building,
 * traversal, command submission, device admission, or a DXR capability. */
struct d12_rt_agx_bvh_gen1_hw_aabb_leaf {
   uint32_t header;
   uint32_t intersection_function_table_offset;
   float minimum[3];
   float maximum[3];
   uint64_t packed_ids_and_flags;
   uint32_t geometry_id;
   uint32_t primitive_id;
   bool opaque;
   bool has_primitive_data;
   uint32_t word36;
   uint64_t primitive_data_relative_word;
   int64_t primitive_data_relative;
};

static inline bool
d12_rt_agx_bvh_gen1_decode_hw_aabb_leaf(
   const void *packed_leaf, size_t available,
   struct d12_rt_agx_bvh_gen1_hw_aabb_leaf *leaf)
{
   if (!leaf)
      return false;

   memset(leaf, 0, sizeof(*leaf));

   struct agx_rt_bvh_gen1_hw_aabb_leaf decoded;
   if (!agx_rt_bvh_gen1_hw_aabb_leaf_unpack(packed_leaf, available, &decoded))
      return false;

   leaf->header = decoded.header;
   leaf->intersection_function_table_offset =
      agx_rt_bvh_gen1_hw_aabb_intersection_function_table_offset(&decoded);
   for (unsigned axis = 0; axis < 3; ++axis) {
      leaf->minimum[axis] = decoded.minimum[axis];
      leaf->maximum[axis] = decoded.maximum[axis];
   }
   leaf->packed_ids_and_flags = decoded.packed_ids_and_flags;
   leaf->geometry_id = agx_rt_bvh_gen1_hw_aabb_geometry_id(&decoded);
   leaf->primitive_id = agx_rt_bvh_gen1_hw_aabb_primitive_id(&decoded);
   leaf->opaque = agx_rt_bvh_gen1_hw_aabb_is_opaque(&decoded);
   leaf->has_primitive_data =
      agx_rt_bvh_gen1_hw_aabb_has_primitive_data(&decoded);
   leaf->word36 = decoded.word36;
   leaf->primitive_data_relative_word =
      decoded.primitive_data_relative_word;
   leaf->primitive_data_relative = decoded.primitive_data_relative;
   return true;
}

/*
 * Decoded Apple RIA Gen1 child layout.
 *
 * BVH versions 1 through 4 select this format.  That version is a property of
 * the blob, not the executing chip: Apple's public builder emitted a
 * version-3/Gen1 blob on M5 Pro.  Callers must therefore dispatch on the
 * parsed BVH version rather than attempting to infer this format from an AGX
 * chip ID.
 *
 * The six quantized components retain their hardware order.  Their geometric
 * axis/min/max names remain deliberately unspecified until independently
 * correlated.
 */
struct d12_rt_agx_bvh_gen1_child_layout {
   uint32_t child_space_slot;
   uint32_t inner_child_offset;
   uint32_t inner_child_count;
   uint32_t leaf_box_count;
   uint32_t leaf_count;
   uint32_t leaf_storage_slots;
   uint32_t leaf_slot_capacity;
   uint32_t leaf_type;
   uint32_t quantized_bounds[8][6];
};

static inline bool
d12_rt_agx_bvh_gen1_decode_child_layout(
   const void *packed_header,
   struct d12_rt_agx_bvh_gen1_child_layout *layout)
{
   struct agx_rt_bvh_gen1_header header;

   if (!layout)
      return false;

   *layout = (struct d12_rt_agx_bvh_gen1_child_layout){0};

   if (!agx_rt_bvh_gen1_header_unpack(packed_header, &header) ||
       !agx_rt_bvh_gen1_branching_layout_valid(&header))
      return false;

   layout->child_space_slot = agx_rt_bvh_gen1_child_space_slot(&header);
   layout->inner_child_offset = agx_rt_bvh_gen1_inner_child_offset(&header);
   layout->inner_child_count = agx_rt_bvh_gen1_inner_child_count(&header);
   layout->leaf_box_count = agx_rt_bvh_gen1_leaf_box_count(&header);
   layout->leaf_count = agx_rt_bvh_gen1_leaf_count(&header);
   layout->leaf_storage_slots =
      agx_rt_bvh_gen1_leaf_storage_slots(&header);
   layout->leaf_slot_capacity =
      agx_rt_bvh_gen1_leaf_slot_capacity(&header);
   layout->leaf_type = agx_rt_bvh_gen1_leaf_type(&header);

   for (unsigned child = 0; child < 8; ++child) {
      struct agx_rt_bvh_gen1_quantized_bounds bounds;
      if (!agx_rt_bvh_gen1_quantized_child_bounds(&header, child, &bounds)) {
         *layout = (struct d12_rt_agx_bvh_gen1_child_layout){0};
         return false;
      }

      for (unsigned component = 0; component < 6; ++component)
         layout->quantized_bounds[child][component] =
            bounds.component[component];
   }

   return true;
}

/*
 * Decoded Apple RIA Gen5 child layout.
 *
 * The offsets are signed because the hardware field is a signed 32-bit
 * 64-byte-slot index.  Consumers must add them to the acceleration-structure
 * base with bounds checks appropriate to the mapped allocation.
 *
 * This wrapper is deliberately fail-closed: Apple's getQtbCount() returns 4
 * when the three QTB occupancies disagree with the leaf count.  Four is a
 * sentinel, not an addressable fourth QTB, so malformed headers never escape
 * through this DXR-facing interface.
 */
struct d12_rt_agx_bvh_gen5_child_layout {
   int32_t child_space_slot;
   uint32_t leaf_count;
   uint32_t inner_slots;
   uint32_t qtb_count;
   uint32_t vrb_size_bytes;
   int64_t child_qtbs_offset_bytes;
   int64_t child_hw_leaves_offset_bytes;
   int64_t vrbs_offset_bytes;
   int64_t lhb_offset_bytes;
   int64_t child_leaves_offset_bytes;
};

static inline bool
d12_rt_agx_bvh_gen5_decode_child_layout(
   const void *packed_header,
   struct d12_rt_agx_bvh_gen5_child_layout *layout)
{
   struct agx_rt_bvh_gen5_header header;

   if (!layout)
      return false;

   *layout = (struct d12_rt_agx_bvh_gen5_child_layout){0};

   if (!agx_rt_bvh_gen5_header_unpack(packed_header, &header) ||
       !agx_rt_bvh_gen5_qtb_layout_valid(&header))
      return false;

   *layout = (struct d12_rt_agx_bvh_gen5_child_layout){
      .child_space_slot = agx_rt_bvh_gen5_child_space_slot(&header),
      .leaf_count = agx_rt_bvh_gen5_leaf_count(&header),
      .inner_slots = agx_rt_bvh_gen5_inner_slots(&header),
      .qtb_count = agx_rt_bvh_gen5_qtb_count(&header),
      .vrb_size_bytes = agx_rt_bvh_gen5_vrb_size_bytes(&header),
      .child_qtbs_offset_bytes =
         agx_rt_bvh_gen5_offset_child_qtbs_bytes(&header),
      .child_hw_leaves_offset_bytes =
         agx_rt_bvh_gen5_offset_child_hw_leaves_bytes(&header),
      .vrbs_offset_bytes = agx_rt_bvh_gen5_offset_vrbs_bytes(&header),
      .lhb_offset_bytes = agx_rt_bvh_gen5_offset_lhb_bytes(&header),
      .child_leaves_offset_bytes =
         agx_rt_bvh_gen5_offset_child_leaves_bytes(&header),
   };

   return true;
}

/* RIA node format is selected by the acceleration-structure blob version,
 * independently of the GPU that consumes it. */
enum d12_rt_agx_bvh_node_format {
   D12_RT_AGX_BVH_NODE_FORMAT_INVALID = 0,
   D12_RT_AGX_BVH_NODE_FORMAT_GEN1 = 1,
   D12_RT_AGX_BVH_NODE_FORMAT_GEN5 = 5,
};

struct d12_rt_agx_bvh_child_layout {
   enum d12_rt_agx_bvh_node_format format;
   union {
      struct d12_rt_agx_bvh_gen1_child_layout gen1;
      struct d12_rt_agx_bvh_gen5_child_layout gen5;
   } layout;
};

/* Decode one node after the caller has parsed the containing blob's version.
 * Versions 1..4 share Gen1 nodes; version 5 selects Gen5.  No chip-ID fallback
 * exists, so a future or corrupt version cannot silently choose a layout. */
static inline bool
d12_rt_agx_bvh_decode_child_layout(
   uint32_t bvh_version, const void *packed_header,
   struct d12_rt_agx_bvh_child_layout *layout)
{
   if (!layout)
      return false;

   *layout = (struct d12_rt_agx_bvh_child_layout){0};

   if (bvh_version >= 1 && bvh_version <= 4) {
      struct d12_rt_agx_bvh_gen1_child_layout decoded;
      if (!d12_rt_agx_bvh_gen1_decode_child_layout(packed_header, &decoded))
         return false;

      layout->format = D12_RT_AGX_BVH_NODE_FORMAT_GEN1;
      layout->layout.gen1 = decoded;
      return true;
   }

   if (bvh_version == 5) {
      struct d12_rt_agx_bvh_gen5_child_layout decoded;
      if (!d12_rt_agx_bvh_gen5_decode_child_layout(packed_header, &decoded))
         return false;

      layout->format = D12_RT_AGX_BVH_NODE_FORMAT_GEN5;
      layout->layout.gen5 = decoded;
      return true;
   }

   return false;
}

/*
 * SOFTWARE-VISIBLE TRAVERSAL RESULT (grade A, both AGX3 encodings)
 *
 * Once traversal exists, the shader reads the hit back from one SCS scratch
 * segment that `agx.intersect.ray.result` unpacks into Apple's own
 * `IntersectRayResult` struct.  Those byte offsets are grade A from the RIA3
 * runtime IR (research/gpu/GPU_AGX3_RT_TRAVERSAL_SEMANTICS_2026-08-24.md S4.3),
 * and the public accessor family (`get_distance`, `get_primitive_id`, ...) maps
 * 1:1 onto the DXR/HLSL RayQuery / hit-attribute getters.  This is the
 * result-decode half of a ray_query path; it does not by itself trace a ray.
 *
 * Offsets 0..56 are pinned explicitly in the IR.  The two 4x3 transforms follow
 * the trailing i32 at the standard LLVM struct offsets (+60 and +108); they are
 * marked as struct-layout-derived rather than separately cited.
 *
 * Fields are little-endian, matching Apple Silicon and the ARM64 host.  The SCS
 * scratch *input* layout (origin/dir/tmin/tmax placement) is a separate, still
 * UNKNOWN, hardware-register-defined concern (S8) and is deliberately absent.
 */
#define D12_RT_HIT_OFF_TYPE               0u  /* i32 intersection type/status */
#define D12_RT_HIT_OFF_T                  4u  /* f32 ray distance             */
#define D12_RT_HIT_OFF_PRIMITIVE_ID       8u  /* i32                          */
#define D12_RT_HIT_OFF_GEOMETRY_ID        12u /* i32                          */
#define D12_RT_HIT_OFF_PRIMITIVE_DATA     16u /* addrspace(1) ptr, 8 bytes    */
#define D12_RT_HIT_OFF_INSTANCE_ID        24u /* i32 (bottom-level)           */
#define D12_RT_HIT_OFF_USER_INSTANCE_ID   28u /* i32 (InstanceID())           */
#define D12_RT_HIT_OFF_INSTANCE_COUNT     32u /* i8  levels hit               */
#define D12_RT_HIT_OFF_BARYCENTRICS       40u /* 2 x f32                      */
#define D12_RT_HIT_OFF_FRONT_FACE         48u /* i8                           */
#define D12_RT_HIT_OFF_CURVE_PARAM        52u /* f32 curve param / ray tmin   */
#define D12_RT_HIT_OFF_INTERSECTION_FN_ID 56u /* i32                          */
#define D12_RT_HIT_OFF_OBJECT_TO_WORLD    60u /* 4x3 f32, struct-derived      */
#define D12_RT_HIT_OFF_WORLD_TO_OBJECT    108u /* 4x3 f32, struct-derived     */
#define D12_RT_HIT_RESULT_SIZE            156u /* end of world-to-object 4x3  */
#define D12_RT_TRANSFORM_FLOATS           12u  /* 4 rows x 3 columns          */

/* DXR RayQuery / DXR 1.1 inline state: a 168-byte query object holding the
 * software traversal stack for the proceed()/commit() loop (S4.4).  Grade A for
 * the size; the internal field layout is grade B and is not exposed. */
#define D12_RT_RAY_QUERY_STATE_SIZE       168u

/* TLAS instance leaf (grade A byte offsets from the RIA3 module's TBAA
 * metadata, S4.5).  The object-to-world transform at +12 is what drives
 * multi-level TLAS->BLAS descent. */
#define D12_RT_INSTANCE_LEAF_OFF_HEADER          0u   /* 4 x i32 header words   */
#define D12_RT_INSTANCE_LEAF_OFF_OBJECT_TO_WORLD 12u  /* 4x3 f32, 48 bytes      */
#define D12_RT_INSTANCE_LEAF_OFF_AABB            60u  /* 6 x f32 min/max, 24 B  */
#define D12_RT_INSTANCE_LEAF_OFF_IDS             84u  /* 6 x i32                */
#define D12_RT_INSTANCE_LEAF_OFF_INSTANCE_FLAGS  108u /* 4 x i32 id/user/mask/fl*/
#define D12_RT_INSTANCE_LEAF_SIZE                124u

static inline uint32_t
d12_rt_read_u32_le(const void *base, uint32_t offset)
{
   const uint8_t *p = (const uint8_t *)base + offset;
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
          ((uint32_t)p[3] << 24);
}

static inline float
d12_rt_read_f32_le(const void *base, uint32_t offset)
{
   uint32_t bits = d12_rt_read_u32_le(base, offset);
   float value;
   memcpy(&value, &bits, sizeof(value));
   return value;
}

static inline uint64_t
d12_rt_read_u64_le(const void *base, uint32_t offset)
{
   return (uint64_t)d12_rt_read_u32_le(base, offset) |
          ((uint64_t)d12_rt_read_u32_le(base, offset + 4) << 32);
}

/* The DXR-visible attributes of one hit, read out of a raw result buffer at the
 * grade-A offsets above.  A decoder, not a tracer. */
struct d12_rt_hit_result {
   uint32_t intersection_type;
   float    t;
   uint32_t primitive_id;
   uint32_t geometry_id;
   uint64_t primitive_data;
   uint32_t instance_id;
   uint32_t user_instance_id;
   uint8_t  instance_count;
   float    barycentrics[2];
   uint8_t  front_face;
   float    curve_param;
   uint32_t intersection_function_id;
   float    object_to_world[D12_RT_TRANSFORM_FLOATS];
   float    world_to_object[D12_RT_TRANSFORM_FLOATS];
};

static inline void
d12_rt_read_transform(const void *base, uint32_t offset, float out[12])
{
   for (uint32_t i = 0; i < D12_RT_TRANSFORM_FLOATS; ++i)
      out[i] = d12_rt_read_f32_le(base, offset + i * 4u);
}

/* Decode a raw IntersectRayResult buffer.  `available` must cover the struct;
 * callers that only read a prefix (e.g. RayQuery without transforms) pass the
 * prefix length and the transform fields stay zero. */
static inline bool
d12_rt_hit_result_decode(const void *raw, size_t available,
                         struct d12_rt_hit_result *out)
{
   if (!raw || !out || available < D12_RT_HIT_OFF_INTERSECTION_FN_ID + 4u)
      return false;

   memset(out, 0, sizeof(*out));
   out->intersection_type = d12_rt_read_u32_le(raw, D12_RT_HIT_OFF_TYPE);
   out->t = d12_rt_read_f32_le(raw, D12_RT_HIT_OFF_T);
   out->primitive_id = d12_rt_read_u32_le(raw, D12_RT_HIT_OFF_PRIMITIVE_ID);
   out->geometry_id = d12_rt_read_u32_le(raw, D12_RT_HIT_OFF_GEOMETRY_ID);
   out->primitive_data = d12_rt_read_u64_le(raw, D12_RT_HIT_OFF_PRIMITIVE_DATA);
   out->instance_id = d12_rt_read_u32_le(raw, D12_RT_HIT_OFF_INSTANCE_ID);
   out->user_instance_id =
      d12_rt_read_u32_le(raw, D12_RT_HIT_OFF_USER_INSTANCE_ID);
   out->instance_count =
      ((const uint8_t *)raw)[D12_RT_HIT_OFF_INSTANCE_COUNT];
   out->barycentrics[0] = d12_rt_read_f32_le(raw, D12_RT_HIT_OFF_BARYCENTRICS);
   out->barycentrics[1] =
      d12_rt_read_f32_le(raw, D12_RT_HIT_OFF_BARYCENTRICS + 4u);
   out->front_face = ((const uint8_t *)raw)[D12_RT_HIT_OFF_FRONT_FACE];
   out->curve_param = d12_rt_read_f32_le(raw, D12_RT_HIT_OFF_CURVE_PARAM);
   out->intersection_function_id =
      d12_rt_read_u32_le(raw, D12_RT_HIT_OFF_INTERSECTION_FN_ID);

   if (available >= D12_RT_HIT_RESULT_SIZE) {
      d12_rt_read_transform(raw, D12_RT_HIT_OFF_OBJECT_TO_WORLD,
                            out->object_to_world);
      d12_rt_read_transform(raw, D12_RT_HIT_OFF_WORLD_TO_OBJECT,
                            out->world_to_object);
   }
   return true;
}

/* Read the object-to-world transform of a TLAS instance leaf -- the +12
 * transform that drives multi-level descent. */
static inline bool
d12_rt_instance_leaf_object_to_world(const void *raw, size_t available,
                                     float out[12])
{
   if (!raw || !out || available < D12_RT_INSTANCE_LEAF_OFF_AABB)
      return false;

   d12_rt_read_transform(raw, D12_RT_INSTANCE_LEAF_OFF_OBJECT_TO_WORLD, out);
   return true;
}

#ifdef __cplusplus
}
#endif

#endif
