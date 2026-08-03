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
