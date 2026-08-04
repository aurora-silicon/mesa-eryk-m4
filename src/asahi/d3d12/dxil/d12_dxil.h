/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * DXIL front end for d3d12agx: DXBC container + LLVM 3.7-era bitstream
 * reader. This is the bottom of the "no SPIR-V" shader path —
 *
 *    DXIL container -> bitstream -> module -> NIR -> AGX
 *
 * with no SPIR-V and no external compiler anywhere in it.
 *
 * Format references (all public): LLVM's BitCodeFormat documentation for
 * the bitstream container, and DirectXShaderCompiler's docs/DXIL.rst for
 * the DXIL layer on top. Block/abbrev numbering cross-checked against
 * Mesa's own MIT-licensed DXIL *writer* in src/microsoft/compiler, which
 * emits this exact format.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shader_enums.h"

/* Bitstream container magic: 'B','C',0xC0,0xDE */
#define D12_BITCODE_MAGIC 0xDEC04342u

/* DXBC container magic: 'D','X','B','C' */
#define D12_DXBC_MAGIC 0x43425844u

/* Standard and application block IDs (LLVM bitcode) */
enum d12_block_id {
   D12_BLOCK_BLOCKINFO = 0,
   D12_BLOCK_MODULE = 8,
   D12_BLOCK_PARAMATTR = 9,
   D12_BLOCK_PARAMATTR_GROUP = 10,
   D12_BLOCK_CONSTANTS = 11,
   D12_BLOCK_FUNCTION = 12,
   D12_BLOCK_VALUE_SYMTAB = 14,
   D12_BLOCK_METADATA = 15,
   D12_BLOCK_METADATA_ATTACHMENT = 16,
   D12_BLOCK_TYPE = 17,
};

/* Fixed abbreviation IDs present in every block */
enum d12_abbrev_id {
   D12_ABBREV_END_BLOCK = 0,
   D12_ABBREV_ENTER_SUBBLOCK = 1,
   D12_ABBREV_DEFINE_ABBREV = 2,
   D12_ABBREV_UNABBREV_RECORD = 3,
   D12_ABBREV_FIRST_APPLICATION = 4,
};

/* BLOCKINFO record codes */
enum d12_blockinfo_code {
   D12_BLOCKINFO_SETBID = 1,
   D12_BLOCKINFO_BLOCKNAME = 2,
   D12_BLOCKINFO_SETRECORDNAME = 3,
};

/* Abbreviation operand encodings */
enum d12_abbrev_encoding {
   D12_ENC_LITERAL = 0,
   D12_ENC_FIXED = 1,
   D12_ENC_VBR = 2,
   D12_ENC_ARRAY = 3,
   D12_ENC_CHAR6 = 4,
   D12_ENC_BLOB = 5,
};

struct d12_abbrev_op {
   enum d12_abbrev_encoding enc;
   uint64_t value; /* literal value, or bit width for FIXED/VBR */
};

#define D12_MAX_ABBREV_OPS 32

struct d12_abbrev {
   struct d12_abbrev_op ops[D12_MAX_ABBREV_OPS];
   unsigned num_ops;
};

#define D12_MAX_ABBREVS 128

struct d12_abbrev_list {
   struct d12_abbrev abbrevs[D12_MAX_ABBREVS];
   unsigned count;
};

/* Per-block-ID abbreviations declared in the BLOCKINFO block */
#define D12_MAX_BLOCKINFO_BLOCKS 16

struct d12_blockinfo {
   struct {
      unsigned block_id;
      struct d12_abbrev_list abbrevs;
   } entries[D12_MAX_BLOCKINFO_BLOCKS];
   unsigned count;
};

/* A decoded record. Operand storage is caller-owned scratch. */
#define D12_MAX_RECORD_OPS 1024

struct d12_record {
   uint32_t code;
   uint64_t ops[D12_MAX_RECORD_OPS];
   unsigned num_ops;

   /* BLOB payload, if the abbreviation ended in one */
   const uint8_t *blob;
   size_t blob_size;
};

struct d12_bitstream {
   const uint8_t *data;
   size_t size_B;

   /* Absolute bit position */
   uint64_t pos_bits;

   bool error;
   const char *error_msg;
};

/*
 * Container handling: returns a pointer to raw LLVM bitcode within the
 * input. Accepts either a DXBC container (extracts its DXIL part) or bare
 * bitcode. Returns false and sets *why on malformed input.
 */
bool d12_dxil_find_bitcode(const void *data, size_t size_B,
                           const uint8_t **out_bitcode, size_t *out_size_B,
                           const char **why);

/*
 * Recover the shader stage from a DXBC container's DXIL program version.
 * Returns false for bare bitcode (no container to ask).
 */
bool d12_dxil_container_kind(const void *data, size_t size_B,
                             uint32_t *out_kind);
const char *d12_dxil_kind_name(uint32_t kind);
bool d12_dxil_container_stage(const void *data, size_t size_B,
                              mesa_shader_stage *out_stage);

/* Bitstream primitives */
void d12_bitstream_init(struct d12_bitstream *bs, const uint8_t *data,
                        size_t size_B);
uint32_t d12_bitstream_read(struct d12_bitstream *bs, unsigned bits);
uint64_t d12_bitstream_read64(struct d12_bitstream *bs, unsigned bits);
uint64_t d12_bitstream_read_vbr(struct d12_bitstream *bs, unsigned bits);
void d12_bitstream_align32(struct d12_bitstream *bs);
bool d12_bitstream_at_end(const struct d12_bitstream *bs);

/*
 * Block walking. The visitor is called for every record encountered; return
 * false from it to abort the walk. Sub-blocks are entered depth-first;
 * enter/exit callbacks may be NULL.
 */
struct d12_block_visitor {
   void *user;
   bool (*enter_block)(void *user, unsigned block_id, unsigned depth);
   bool (*record)(void *user, unsigned block_id, unsigned depth,
                  const struct d12_record *rec, unsigned abbrev_id);
   bool (*exit_block)(void *user, unsigned block_id, unsigned depth);
};

/*
 * Walk a whole bitcode module (magic already at bs position 0). Returns
 * false on malformed input or if the visitor aborted; bs->error_msg
 * explains malformed input.
 */
bool d12_dxil_walk(struct d12_bitstream *bs,
                   const struct d12_block_visitor *visitor);

/*
 * ---------------------------------------------------------------------
 * Module decoding (G4b)
 *
 * Record codes below are the LLVM 3.7 numbering, cross-checked against
 * Mesa's DXIL writer (src/microsoft/compiler/dxil_module.c).
 * ---------------------------------------------------------------------
 */

enum d12_module_code {
   D12_MODULE_VERSION = 1,
   D12_MODULE_TRIPLE = 2,
   D12_MODULE_DATALAYOUT = 3,
   D12_MODULE_GLOBALVAR = 7,
   D12_MODULE_FUNCTION = 8,
   D12_MODULE_ALIAS = 9,
};

enum d12_type_code {
   D12_TYPE_NUMENTRY = 1,
   D12_TYPE_VOID = 2,
   D12_TYPE_FLOAT = 3,
   D12_TYPE_DOUBLE = 4,
   D12_TYPE_LABEL = 5,
   D12_TYPE_OPAQUE = 6,
   D12_TYPE_INTEGER = 7,
   D12_TYPE_POINTER = 8,
   D12_TYPE_HALF = 10,
   D12_TYPE_ARRAY = 11,
   D12_TYPE_VECTOR = 12,
   D12_TYPE_METADATA = 16,
   D12_TYPE_STRUCT_ANON = 18,
   D12_TYPE_STRUCT_NAME = 19,
   D12_TYPE_STRUCT_NAMED = 20,
   D12_TYPE_FUNCTION = 21,
};

enum d12_const_code {
   D12_CST_SETTYPE = 1,
   D12_CST_NULL = 2,
   D12_CST_UNDEF = 3,
   D12_CST_INTEGER = 4,
   D12_CST_WIDE_INTEGER = 5,
   D12_CST_FLOAT = 6,
   D12_CST_AGGREGATE = 7,
   D12_CST_STRING = 8,
   D12_CST_CSTRING = 9,
   D12_CST_CE_CAST = 11,
   D12_CST_CE_GEP = 12,
   D12_CST_CE_INBOUNDS_GEP = 20,
   D12_CST_DATA = 22,
};

enum d12_func_code {
   D12_FUNC_DECLAREBLOCKS = 1,
   D12_FUNC_INST_BINOP = 2,
   D12_FUNC_INST_CAST = 3,
   D12_FUNC_INST_SELECT = 5,
   D12_FUNC_INST_EXTRACTELT = 6,
   D12_FUNC_INST_INSERTELT = 7,
   D12_FUNC_INST_SHUFFLEVEC = 8,
   D12_FUNC_INST_CMP = 9,
   D12_FUNC_INST_RET = 10,
   D12_FUNC_INST_BR = 11,
   D12_FUNC_INST_SWITCH = 12,
   D12_FUNC_INST_UNREACHABLE = 15,
   D12_FUNC_INST_PHI = 16,
   D12_FUNC_INST_ALLOCA = 19,
   D12_FUNC_INST_LOAD = 20,
   D12_FUNC_INST_EXTRACTVAL = 26,
   D12_FUNC_INST_CMP2 = 28,
   D12_FUNC_INST_VSELECT = 29,
   D12_FUNC_INST_GEP_OLD = 30,
   D12_FUNC_DEBUG_LOC_AGAIN = 33,
   D12_FUNC_INST_CALL = 34,
   D12_FUNC_DEBUG_LOC = 35,
   D12_FUNC_INST_ATOMICRMW = 38,
   D12_FUNC_INST_GEP = 43,
   D12_FUNC_INST_STORE = 44,
   D12_FUNC_INST_CMPXCHG = 46,
};

enum d12_metadata_code {
   D12_MD_STRING = 1,
   D12_MD_VALUE = 2,
   D12_MD_NODE = 3,
   D12_MD_NAME = 4,
   D12_MD_KIND = 6,
   D12_MD_NAMED_NODE = 10,
};

enum d12_vst_code {
   D12_VST_ENTRY = 1,
   D12_VST_BBENTRY = 2,
};

/* Decoded LLVM type */
struct d12_type {
   enum d12_type_code kind;

   union {
      unsigned int_bits;         /* INTEGER */
      struct {                   /* POINTER */
         uint32_t pointee;
         uint32_t addr_space;
      } ptr;
      struct {                   /* ARRAY / VECTOR */
         uint64_t count;
         uint32_t elem;
      } seq;
      struct {                   /* FUNCTION */
         uint32_t ret;
         uint32_t params[16];
         unsigned num_params;
      } func;
      struct {                   /* STRUCT_ANON / STRUCT_NAMED */
         uint32_t members[16];
         unsigned num_members;
      } strct;
   };

   const char *name; /* struct name, if any (points into the module's arena) */
};

enum d12_value_kind {
   D12_VALUE_UNKNOWN = 0,
   D12_VALUE_GLOBAL,
   D12_VALUE_FUNCTION,
   D12_VALUE_CONSTANT,
   D12_VALUE_ARGUMENT,
   D12_VALUE_INSTRUCTION,
};

/*
 * LLVM address spaces as DXC emits them. Only groupshared is distinguished:
 * everything else a shader declares -- constant lookup tables, the globals
 * that stand for resources -- lands in 0.
 */
#define D12_ADDR_SPACE_DEFAULT     0
#define D12_ADDR_SPACE_GROUPSHARED 3

struct d12_value {
   enum d12_value_kind kind;

   /*
    * Index into the type table, and for a GLOBAL it is the type of the
    * VALUE -- [64 x i32] for `groupshared uint scratch[64]` -- not a
    * pointer to it. LLVM 3.7 moved the address space out of the type and
    * into the record, so `type` alone cannot tell groupshared from
    * anything else; `addr_space` below is the only thing that can.
    */
   uint32_t type;
   uint32_t addr_space; /* GLOBAL only; see D12_ADDR_SPACE_* */

   const char *name;   /* from VALUE_SYMTAB, may be NULL */

   /* GLOBAL only: LLVM's initid (value id + 1, 0 = no initializer) and
    * the isconst flag bit. Distinguishes a mutable static (per-thread
    * storage) from a constant lookup table (data we must materialize).
    */
   uint32_t global_init_id;
   bool global_is_const;

   /* CONSTANT only: a constant-expression GEP. agg_elems[0] is the base
    * value, the rest are index value ids.
    */
   bool is_ce_gep;

   /* Constants */
   bool is_undef;
   bool is_null;
   uint64_t const_int;   /* CST_INTEGER (already un-rotated) */
   uint64_t const_float; /* CST_FLOAT raw bits */

   /* CST_AGGREGATE: element value ids. Needed because SM 6.6 passes the
    * resource binding to createHandleFromBinding as a constant struct.
    */
   uint32_t agg_elems[8];
   unsigned num_agg_elems;

   /* Functions */
   uint32_t func_index; /* index into module->functions, for FUNCTION */
};

/* One decoded instruction, operands kept as value indices where resolved */
#define D12_MAX_INST_OPS 16

/* A phi's incoming edge: which value arrives from which basic block. */
#define D12_MAX_PHI_SOURCES 8

/* Basic blocks per function. Real HLSL is far below this. */
#define D12_MAX_BLOCKS 256

struct d12_phi_src {
   uint32_t value; /* ~0u when it could not be resolved */
   uint32_t block;
};

struct d12_inst {
   enum d12_func_code code;
   uint32_t result_type;              /* type index, or ~0 if void */
   uint32_t result_value;             /* value index it defines, or ~0 */
   uint32_t ops[D12_MAX_INST_OPS];    /* value indices */
   unsigned num_ops;
   uint64_t extra[4];                 /* opcode-specific immediates */
   unsigned num_extra;

   /* Which basic block this instruction belongs to, filled in by the
    * parser: LLVM marks block boundaries only implicitly, by terminators.
    */
   uint32_t block;

   /* PHI only */
   struct d12_phi_src phi_srcs[D12_MAX_PHI_SOURCES];
   unsigned num_phi_srcs;
};

/* Does this instruction end a basic block? */
static inline bool
d12_inst_is_terminator(const struct d12_inst *inst)
{
   switch (inst->code) {
   case D12_FUNC_INST_RET:
   case D12_FUNC_INST_BR:
   case D12_FUNC_INST_SWITCH:
   case D12_FUNC_INST_UNREACHABLE:
      return true;
   default:
      return false;
   }
}

#define D12_MAX_INSTS 4096

struct d12_function {
   const char *name;
   uint32_t type;        /* function type index */
   bool is_declaration;  /* proto only (dx.op intrinsics are declarations) */
   uint32_t value_index; /* its slot in the module value list */

   /* Body (definitions only) */
   struct d12_inst *insts;
   unsigned num_insts;
   unsigned num_blocks;
   unsigned first_value; /* value index of this function's first argument */
};

struct d12_md_node {
   uint64_t ops[64]; /* metadata ids (already un-biased), 0 = null */
   bool op_is_null[64];
   unsigned num_ops;
};

struct d12_metadata {
   enum d12_metadata_code kind;
   const char *string;         /* MD_STRING */
   struct d12_md_node node;    /* MD_NODE */
   uint32_t value_type;        /* MD_VALUE */
   uint64_t value_id;          /* MD_VALUE */
};

struct d12_named_md {
   const char *name;
   uint64_t nodes[16];
   unsigned num_nodes;
};

/*
 * One DXIL signature element. Layout of the metadata node, read out of
 * real dxc output:
 *   [0] id  [1] semantic name  [2] component type  [3] system-value kind
 *   [4] semantic indices  [5] interpolation mode  [6] rows  [7] cols
 *   [8] start row  [9] start col  [10] name-value pairs
 * The semantic NAME is the dependable key (SV_Position, SV_Target,
 * SV_VertexID, TEXCOORD, ...), so that is what we match on.
 */
struct d12_sig_element {
   uint32_t id;         /* what loadInput/storeOutput reference */
   const char *name;    /* semantic name */
   uint32_t start_row;
   uint32_t cols;
};

#define D12_MAX_SIG_ELEMENTS 32

#define D12_MAX_TYPES     512
#define D12_MAX_VALUES    8192
#define D12_MAX_FUNCTIONS 256
#define D12_MAX_METADATA  4096
#define D12_MAX_NAMED_MD  32

/*
 * Declared resources, from the dx.resources named metadata: four lists,
 * one per class, each entry describing a range of shader registers.
 *
 * This is what lets a shader register be resolved to a binding slot. D3D12
 * register namespaces are per class -- t0, u0 and b0 are three different
 * resources -- so a handle cannot be identified by its number alone.
 */
enum d12_resource_class {
   D12_RES_SRV = 0,
   D12_RES_UAV = 1,
   D12_RES_CBV = 2,
   D12_RES_SAMPLER = 3,
   D12_RES_CLASS_COUNT = 4,
};

#define D12_MAX_MODULE_RESOURCES 32

/*
 * DXIL ResourceKind (subset). The shape decides how a buffer access is
 * addressed, and it is NOT recoverable from which dx.op was called: SM 6.0
 * reaches a ByteAddressBuffer through bufferLoad, the same intrinsic a
 * structured buffer uses.
 */
enum d12_resource_kind {
   D12_RES_KIND_TYPED_BUFFER = 10,
   D12_RES_KIND_RAW_BUFFER = 11,
   D12_RES_KIND_STRUCTURED_BUFFER = 12,
};

/*
 * An unbounded resource array -- `Texture2D t[] : register(t0)`, the basis
 * of bindless -- declares a range size of UINT32_MAX. Anything that
 * enumerates a range has to check for it: a loop to range_size runs four
 * billion times, which is not a hang anyone would look for in a binding
 * table.
 */
#define D12_RANGE_UNBOUNDED UINT32_MAX

struct d12_resource_binding {
   uint32_t id;    /* rangeId: index within its class's list */
   uint32_t klass; /* enum d12_resource_class */
   uint32_t kind;  /* enum d12_resource_kind (shape) */
   uint32_t space;
   uint32_t lower_bound; /* first shader register in the range */
   uint32_t range_size;  /* D12_RANGE_UNBOUNDED for a bindless array */

   /* Structured-buffer element stride, from metadata tag 1. Zero for raw
    * and typed buffers, whose accesses are byte-addressed.
    */
   uint32_t stride_B;

   uint32_t cb_size_B; /* CBV only */

   /* The global variable that stands for this resource. SM 6.x declares
    * resources as globals, loads them, and converts the loaded value with
    * createHandleForLib -- so a load has to be traceable back to here.
    */
   uint32_t global_value; /* ~0u when unresolved */
};

/* One never-moved block of interned strings. */
struct d12_str_chunk {
   struct d12_str_chunk *next;
   char data[];
};

struct d12_module {
   struct d12_type types[D12_MAX_TYPES];
   unsigned num_types;

   struct d12_value values[D12_MAX_VALUES];
   unsigned num_values;

   struct d12_function functions[D12_MAX_FUNCTIONS];
   unsigned num_functions;

   struct d12_metadata metadata[D12_MAX_METADATA];
   unsigned num_metadata;

   struct d12_named_md named_md[D12_MAX_NAMED_MD];
   unsigned num_named_md;

   /*
    * String arena for names parsed out of the bitstream.
    *
    * CHUNKED, and it has to be: every name we hand out is a bare pointer
    * into this storage, held by types, values and functions for the life
    * of the module. A single realloc'd buffer moves, and every one of
    * those pointers silently starts reading whatever now occupies the old
    * allocation -- a use-after-free that produces plausible-looking
    * garbage rather than a crash. Chunks are never moved or resized.
    */
   struct d12_str_chunk *strings;
   size_t chunk_used, chunk_size;

   /* Entry-point signatures, resolved from dx.entryPoints */
   struct d12_sig_element input_sig[D12_MAX_SIG_ELEMENTS];
   unsigned num_input_sig;
   struct d12_sig_element output_sig[D12_MAX_SIG_ELEMENTS];
   unsigned num_output_sig;

   /* [numthreads(x,y,z)] from the entry-point properties (compute only) */
   uint32_t num_threads[3];

   /* Declared resources, from dx.resources */
   struct d12_resource_binding resources[D12_MAX_MODULE_RESOURCES];
   unsigned num_resources;

   const char *error;
};

/* Look up a signature element by the id loadInput/storeOutput carries. */
const struct d12_sig_element *d12_dxil_find_sig(const struct d12_sig_element *sig,
                                                unsigned count, uint32_t id);

/*
 * Parse a complete module. Returns false with mod->error set on failure.
 * The module owns heap allocations; free with d12_dxil_module_finish.
 */
bool d12_dxil_module_parse(struct d12_module *mod, const uint8_t *bitcode,
                           size_t size_B);
void d12_dxil_module_finish(struct d12_module *mod);

/* Convenience: find a named metadata node list (e.g. "dx.entryPoints") */
const struct d12_named_md *d12_dxil_find_named_md(const struct d12_module *mod,
                                                  const char *name);

/*
 * ---------------------------------------------------------------------
 * Translation to NIR (G4c) — the "no SPIR-V" step
 * ---------------------------------------------------------------------
 */

struct nir_shader;

/*
 * Resource binding ABI (campaign OQ-2).
 *
 * The driver publishes ONE 64-bit pointer — the root table — in AGX
 * uniform registers (four 16-bit registers). The root table is an array
 * of 64-bit resource base addresses indexed by binding slot, so a shader
 * reaches a resource with one indirection:
 *
 *     base = load_global(root + binding * 8)
 *
 * A single uniform slot keeps the ABI stable no matter how many resources
 * are bound. The shader reads the root with load_root_agx, a placeholder
 * that survives agx_preprocess_nir; d12_dxil_lower_root() turns it into a
 * load_preamble afterwards. Emitting load_preamble before preprocessing
 * does NOT work — the compiler reassigns its base.
 */
#define D12_UNIFORM_ROOT            4
#define D12_UNIFORM_ROOT_REGS       4

/*
 * Texture descriptors live in their own heap: AGX reaches a texture
 * through a hardware descriptor, not a bare address. A bindless handle is
 * a byte OFFSET into that heap, and the heap's base sits in these uniform
 * registers -- the instruction takes a 64-bit uniform base plus a 32-bit
 * offset register and adds them.
 */
#define D12_UNIFORM_TEXTURES        8
#define D12_UNIFORM_TEXTURE_REGS    4

/*
 * Uniform registers the driver claims for its own ABI. This must cover
 * EVERY register the driver publishes: the compiler starts push_count
 * here and allocates its own promoted constants above it, so anything
 * written past this point is either never loaded by the hardware or
 * overwritten. Getting it wrong presents as half of a 64-bit pointer
 * arriving -- a fault at address 0.
 */
/*
 * Sampler heap base: hardware sampler descriptors at slot * 8 bytes, the
 * sampler analogue of the texture heap. 8 is AGX_SAMPLER_LENGTH;
 * static-asserted where agx_pack.h is visible (d12_cmd.c).
 */
#define D12_UNIFORM_SAMPLERS        12
#define D12_UNIFORM_SAMPLER_REGS    4
#define D12_SAMPLER_DESC_SIZE       8

/*
 * SM 6.6 ResourceDescriptorHeap base: the shader-visible heap's
 * d12_descriptor array, indexed directly by heap index. 32 is
 * sizeof(struct d12_descriptor); static-asserted in d12_cmd.c where the
 * struct is visible.
 */
#define D12_UNIFORM_HEAP            16
#define D12_UNIFORM_HEAP_REGS       4
#define D12_HEAP_DESC_SIZE          40
#define D12_DESC_COUNTER_OFFSET     8

/*
 * The root table ABI: two arrays back to back. addrs[i] at slot * 8,
 * then UAV counter addresses at (D12_MAX_RESOURCE_BINDINGS + slot) * 8.
 */
#define D12_ROOT_COUNTER_BASE       D12_MAX_RESOURCE_BINDINGS

#define D12_UNIFORM_RESERVED (D12_UNIFORM_HEAP + D12_UNIFORM_HEAP_REGS)

/* One hardware texture descriptor. Spelled out rather than including
 * genxml from the front end; asserted against the real value where the
 * descriptors are packed.
 */
#define D12_TEXTURE_DESC_SIZE       24

/*
 * Root-table layout.
 *
 * A resource is named by three things -- class, register space and shader
 * register -- and all three matter: t0, u0 and b0 are different resources,
 * and so are t0 space0 and t0 space1. A formula over those would need a
 * slot for every combination that could exist, most of them empty, and
 * uploaded on every draw.
 *
 * Instead the slots are assigned DENSELY, from the root signature, which
 * enumerates exactly the bindings a pipeline can have. Both sides read the
 * same assignment: the shader is compiled against it, and the command list
 * fills the table through it. Spaces cost nothing because an unused one
 * takes no slot.
 */
#define D12_MAX_RESOURCE_BINDINGS 256

struct d12_binding_map {
   struct {
      uint32_t klass;
      uint32_t space;
      uint32_t reg;
   } entries[D12_MAX_RESOURCE_BINDINGS];
   unsigned count;
};

/* Slot for a binding, or ~0u when the map does not name it. */
static inline unsigned
d12_binding_map_find(const struct d12_binding_map *map, unsigned klass,
                     unsigned space, unsigned reg)
{
   for (unsigned i = 0; i < map->count; i++) {
      if (map->entries[i].klass == klass && map->entries[i].space == space &&
          map->entries[i].reg == reg)
         return i;
   }
   return ~0u;
}

/* Append a binding if it is not already present; returns its slot. */
static inline unsigned
d12_binding_map_add(struct d12_binding_map *map, unsigned klass,
                    unsigned space, unsigned reg)
{
   unsigned existing = d12_binding_map_find(map, klass, space, reg);
   if (existing != ~0u)
      return existing;

   if (map->count >= D12_MAX_RESOURCE_BINDINGS)
      return ~0u;

   unsigned slot = map->count++;
   map->entries[slot].klass = klass;
   map->entries[slot].space = space;
   map->entries[slot].reg = reg;
   return slot;
}

/*
 * Build a map from a module's own declarations. Used when there is no root
 * signature to derive one from -- the standalone compiler tool -- and never
 * by the driver, where compile time and bind time have to agree on an
 * assignment neither derives from the other.
 */
void d12_binding_map_from_module(const struct d12_module *mod,
                                 struct d12_binding_map *out);

/* How many registers a declared range actually covers, clamped so an
 * unbounded array cannot be walked. Returns 0 for unbounded.
 */
static inline unsigned
d12_range_count(const struct d12_resource_binding *rb)
{
   if (rb->range_size == D12_RANGE_UNBOUNDED)
      return 0;
   return MAX2(rb->range_size, 1u);
}

/*
 * Translate a parsed module's entry point to NIR for the AGX backend.
 * Returns NULL with *error set on failure. Caller owns the shader.
 */
struct nir_shader *d12_dxil_to_nir(const struct d12_module *mod,
                                   mesa_shader_stage stage,
                                   const struct d12_binding_map *map,
                                   const char **error);

/*
 * Replace the root-table placeholder with a real uniform read. MUST run
 * after agx_preprocess_nir and before agx_compile_shader_nir.
 */
bool d12_dxil_lower_root(struct nir_shader *nir);
bool d12_dxil_lower_uvs_index(struct nir_shader *nir,
                              const unsigned *slots);
