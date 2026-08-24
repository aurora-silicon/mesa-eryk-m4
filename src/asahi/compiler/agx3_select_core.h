/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * AGX3 instruction selection core: the grade-A subset of AGX IR operations
 * that maps 1:1 onto a grounded AGX3 encoding, expressed over normalized
 * operands so it is free of the Mesa compiler's generated headers and testable
 * on its own.
 *
 * The full backend (agx3_pack_binary) is a thin shim that walks the scheduled
 * agx_instr IR, translates each instruction into the struct below, and calls
 * agx3_select_encode. Everything that can be wrong -- which IR op maps to which
 * AGX3 form, the domain each form is valid over, how IR operands bind to the
 * form's fields -- lives here where it can be exercised exhaustively, byte for
 * byte, against the hand-written agx3_pack.c encoders.
 *
 * This is milestone one: the no-modifier 32-bit ALU / move / immediate core
 * plus the two nullary terminators. It fails closed on everything else --
 * modifiers, non-32-bit sizes, immediate/uniform sources, shifts, and every
 * unmapped opcode -- because emitting a plausible-but-wrong AGX3 stream is a
 * silent mis-execution, not a fault. Mapping details and grades are in
 * research/gpu/GPU_AGX3_BACKEND_INSTRUCTION_SELECTION_2026-08-24.md.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The AGX IR operations milestone one selects. The compiler shim maps
 * AGX_OPCODE_* onto these; the core never sees the generated opcode enum. */
enum agx3_sel_op {
   AGX3_SEL_OP_UNSUPPORTED = 0,
   AGX3_SEL_OP_END,       /* IR stop / shader terminator */
   AGX3_SEL_OP_BARRIER,   /* IR threadgroup_barrier */
   AGX3_SEL_OP_MOV_IMM,   /* IR mov_imm */
   AGX3_SEL_OP_FADD,      /* IR fadd (f32) */
   AGX3_SEL_OP_FMUL,      /* IR fmul (f32) */
   AGX3_SEL_OP_FFMA,      /* IR ffma (f32) */
   AGX3_SEL_OP_IADD,      /* IR iadd (u32), shift == 0 */
   AGX3_SEL_OP_IMAD,      /* IR imad (u32), shift == 0 */
   AGX3_SEL_OP_ASR,       /* IR asr (i32) */
};

/* A normalized source operand. `reg` is a 16-bit half-register index, the same
 * unit Apple's register fields use and the unit agx_index.value already
 * carries after RA. `is_reg` is false for immediate / uniform / null sources,
 * which milestone one refuses (a pre-RA legalize pass will materialize them
 * later). abs/neg mirror the IR float modifiers; milestone one refuses any set
 * modifier because the grounded no-modifier packers cannot express them. */
struct agx3_sel_src {
   uint16_t reg;
   bool is_reg;
   bool abs;
   bool neg;
   uint8_t size; /* AGX size: 0 = 16-bit, 1 = 32-bit, 2 = 64-bit */
};

struct agx3_sel_instr {
   enum agx3_sel_op op;
   uint8_t nr_srcs;
   bool has_dest;
   uint16_t dest;
   uint8_t dest_size; /* AGX size */
   struct agx3_sel_src src[3];
   uint32_t imm;      /* mov_imm immediate */
   uint8_t shift;     /* iadd / imad shift; milestone one requires 0 */
   bool saturate;
};

/* AGX size code for a 32-bit value; the only width milestone one accepts. */
#define AGX3_SEL_SIZE_32 1u

/* The maximum bytes any milestone-one form emits (AGX3c fma is 16 B). */
#define AGX3_SEL_MAX_LENGTH 16u

/*
 * Encode one selected instruction. On success writes `*len` bytes (<=
 * AGX3_SEL_MAX_LENGTH) into `out` and returns true. Returns false -- writing
 * nothing meaningful -- for an unknown encoding, an unmapped op, or any
 * instruction outside the grounded domain. A false return must fail the
 * shader; it must never be treated as an empty instruction.
 */
bool agx3_select_encode(enum agx_isa_encoding encoding,
                        const struct agx3_sel_instr *instr,
                        uint8_t out[AGX3_SEL_MAX_LENGTH], uint8_t *len);

/* Whether the core has a grade-A mapping for this op at all, independent of the
 * operand domain. Lets the shim distinguish "refused because unmapped" from
 * "refused because out of domain" for diagnostics. */
bool agx3_select_op_supported(enum agx3_sel_op op);

#ifdef __cplusplus
}
#endif
