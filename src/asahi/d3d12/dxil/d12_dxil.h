/*
 * Copyright 2026 NTASP contributors
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

struct d12_value {
   enum d12_value_kind kind;
   uint32_t type;      /* index into the type table */
   const char *name;   /* from VALUE_SYMTAB, may be NULL */

   /* Constants */
   bool is_undef;
   bool is_null;
   uint64_t const_int;   /* CST_INTEGER (already un-rotated) */
   uint64_t const_float; /* CST_FLOAT raw bits */

   /* Functions */
   uint32_t func_index; /* index into module->functions, for FUNCTION */
};

/* One decoded instruction, operands kept as value indices where resolved */
#define D12_MAX_INST_OPS 16

struct d12_inst {
   enum d12_func_code code;
   uint32_t result_type;              /* type index, or ~0 if void */
   uint32_t result_value;             /* value index it defines, or ~0 */
   uint32_t ops[D12_MAX_INST_OPS];    /* value indices */
   unsigned num_ops;
   uint64_t extra[4];                 /* opcode-specific immediates */
   unsigned num_extra;
};

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

#define D12_MAX_TYPES     512
#define D12_MAX_VALUES    8192
#define D12_MAX_FUNCTIONS 256
#define D12_MAX_METADATA  4096
#define D12_MAX_NAMED_MD  32

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

   /* String arena for names parsed out of the bitstream */
   char *strings;
   size_t strings_used, strings_size;

   const char *error;
};

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
