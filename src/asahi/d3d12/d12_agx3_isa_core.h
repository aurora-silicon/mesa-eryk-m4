/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded D3D12 scalar-ALU mapping into the source-grounded AGX3 packer.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_BINARY_ALU_MAX_LENGTH 14
#define D12_AGX3_TERNARY_ALU_MAX_LENGTH 14
#define D12_AGX3_FADD_F32_REG_REG_LENGTH 6
#define D12_AGX3_FMUL_F32_REG_REG_LENGTH 6
#define D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH 14
#define D12_AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH 16
#define D12_AGX3_FMIN_F32_REG_REG_MAX_LENGTH 14
#define D12_AGX3_YIELD_I16_REG_LENGTH 6
#define D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH 4

/* These are D3D/DXIL scalar semantics, not raw Apple opcode numbers. */
enum d12_agx3_binary_alu_op {
   D12_AGX3_BINARY_ALU_IADD_U32 = 0,
   D12_AGX3_BINARY_ALU_IAND_U32 = 1,
   D12_AGX3_BINARY_ALU_IOR_U32 = 2,
   D12_AGX3_BINARY_ALU_IXOR_U32 = 3,
   D12_AGX3_BINARY_ALU_ISUB_U32 = 4,
   D12_AGX3_BINARY_ALU_ISHL_U32 = 5,
   D12_AGX3_BINARY_ALU_IASHR_I32 = 6,
   D12_AGX3_BINARY_ALU_ILSHR_U32 = 7,
   D12_AGX3_BINARY_ALU_IMUL_U32 = 8,
};

/*
 * Selection presented to the target-aware integer-multiply bridge.  Only the
 * low-u32 product is encoded.  Naming the rejected selections here lets a
 * lowering caller fail closed instead of accidentally treating an addend or
 * high product as the canonical three-register multiply.
 */
enum d12_agx3_integer_multiply_selection {
   D12_AGX3_INTEGER_MULTIPLY_LOW_U32 = 0,
   D12_AGX3_INTEGER_MULTIPLY_WITH_ADDEND_U32 = 1,
   D12_AGX3_INTEGER_MULTIPLY_HIGH_I32 = 2,
   D12_AGX3_INTEGER_MULTIPLY_HIGH_U32 = 3,
};

/* These selections are validation inputs, not claims of encoded support. */
enum d12_agx3_integer_multiply_add_selection {
   D12_AGX3_INTEGER_MULTIPLY_ADD_LOW_U32 = 0,
   D12_AGX3_INTEGER_MULTIPLY_ADD_IMPLICIT_ZERO_IMUL = 1,
   D12_AGX3_INTEGER_MULTIPLY_ADD_ALTERNATE_P3_LOW_U32 = 2,
   D12_AGX3_INTEGER_MULTIPLY_ADD_ALTERNATE_Q_LOW_U32 = 3,
   D12_AGX3_INTEGER_MULTIPLY_ADD_HIGH_I32 = 4,
   D12_AGX3_INTEGER_MULTIPLY_ADD_HIGH_U32 = 5,
};

enum d12_agx3_ternary_alu_op {
   D12_AGX3_TERNARY_ALU_IMADD_U32 = 0,
};

enum d12_agx3_unary_alu_op {
   D12_AGX3_UNARY_ALU_INOT_U32 = 0,
};

struct d12_agx3_binary_alu_encoding {
   uint8_t bytes[D12_AGX3_BINARY_ALU_MAX_LENGTH];
   uint8_t length;
};

struct d12_agx3_unary_alu_encoding {
   uint8_t bytes[D12_AGX3_BINARY_ALU_MAX_LENGTH];
   uint8_t length;
};

struct d12_agx3_ternary_alu_encoding {
   uint8_t bytes[D12_AGX3_TERNARY_ALU_MAX_LENGTH];
   uint8_t length;
};

struct d12_agx3_fadd_f32_reg_reg_encoding {
   uint8_t bytes[D12_AGX3_FADD_F32_REG_REG_LENGTH];
   uint8_t length;
};

struct d12_agx3_fmul_f32_reg_reg_encoding {
   uint8_t bytes[D12_AGX3_FMUL_F32_REG_REG_LENGTH];
   uint8_t length;
};

struct d12_agx3_fmul_f32_reg_reg_long_encoding {
   uint8_t bytes[D12_AGX3_FMUL_F32_REG_REG_LONG_LENGTH];
   uint8_t length;
};

struct d12_agx3_fma_f32_reg_reg_reg_encoding {
   uint8_t bytes[D12_AGX3_FMA_F32_REG_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct d12_agx3_fmin_f32_reg_reg_encoding {
   uint8_t bytes[D12_AGX3_FMIN_F32_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct d12_agx3_yield_i16_reg_encoding {
   uint8_t bytes[D12_AGX3_YIELD_I16_REG_LENGTH];
   uint8_t length;
};

struct d12_agx3_end_with_wait_fence_mask_encoding {
   uint8_t bytes[D12_AGX3_END_WITH_WAIT_FENCE_MASK_LENGTH];
   uint8_t length;
};

/*
 * Encode one canonical three-register scalar operation. Register arguments
 * are Mesa half-register indices, so 32-bit operands must be even. This is a
 * compiler building block only: success does not imply that AGX3 pipeline
 * compilation, scheduling, command submission, or device admission exists.
 * ISHL, IASHR and ILSHR map only the proved register-register `sll`, `sra`
 * and `srl` instructions; they do not emit or claim the separate shift-count
 * masking seen in compiler output. IMUL maps only the implicit-zero-addend
 * low-u32 product selected by the shared packer, not integer add or high
 * multiply forms.
 */
bool d12_agx3_encode_binary_alu(
   enum agx_isa_encoding encoding, enum d12_agx3_binary_alu_op op,
   uint16_t dest, uint16_t src0, uint16_t src1,
   struct d12_agx3_binary_alu_encoding *out);

/*
 * Encode the canonical integer multiply for an independently witnessed Apple
 * compiler target.  The allowlist is intentionally narrower than the set of
 * chips sharing an encoding: G17C, for example, is AGX3c but has no compiler
 * witness in the source corpus and remains rejected.  This is still only an
 * ISA/lowering building block and does not admit any AGX3 device.
 */
bool d12_agx3_encode_integer_multiply(
   enum agx_chip chip, enum d12_agx3_integer_multiply_selection selection,
   uint16_t dest, uint16_t src0, uint16_t src1,
   struct d12_agx3_binary_alu_encoding *out);

/*
 * Encode the proved real-register-addend, low-u32 multiply-add selection.
 * Signed and unsigned low products are identical modulo 2^32.  The separate
 * implicit-zero multiply, alternate low selections, and high-half forms are
 * deliberately represented only as rejected selections.  The target gate is
 * the same compiler-witness allowlist as d12_agx3_encode_integer_multiply;
 * this does not enable live NIR/DXIL lowering or any AGX3 runtime path.
 */
bool d12_agx3_encode_integer_multiply_add(
   enum agx_chip chip,
   enum d12_agx3_integer_multiply_add_selection selection, uint16_t dest,
   uint16_t mul0, uint16_t mul1, uint16_t addend,
   struct d12_agx3_ternary_alu_encoding *out);

/*
 * Encode/decode only the shared packer's exact compact register-register f32
 * add on the nine Apple compiler targets that produced operand-correlated
 * witnesses.  `encoding` must match `chip`: AGX3b is accepted only for
 * G15G/S/D, G16P/G/S and G17P; AGX3c only for G17G/S.  All operands are even
 * half-register indices in 0..254, and their physical source-field order is
 * preserved even though floating-point addition is commutative.
 *
 * This is a strict ISA representation only.  It neither accepts a canonical
 * prefix with trailing bytes nor enables DXIL lowering, scheduling, command
 * submission, device admission, runtime use, or any D3D12 feature.
 */
bool d12_agx3_encode_fadd_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fadd_f32_reg_reg_encoding *out);
bool d12_agx3_decode_fadd_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1);

/*
 * Encode/decode only the exact compact register-register f32 multiply form
 * witnessed by the L/P2 corpus on G15G/S/D, G16P/G/S and G17P (AGX3b), plus
 * the Q corpus on G17S (AGX3c).  G17G remains rejected because the pinned
 * corpus witnesses only its longer multiply form.  `encoding` must match the
 * target, and all operands are even half-register indices in the
 * conservatively proved 0..126 domain.
 *
 * This strict six-byte ISA bridge does not accept trailing bytes and does not
 * enable DXIL lowering, scheduling, command submission, device admission,
 * runtime use, or any D3D12 feature.
 */
bool d12_agx3_encode_fmul_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fmul_f32_reg_reg_encoding *out);
bool d12_agx3_decode_fmul_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1);

/*
 * Encode/decode the exact fourteen-byte register-register f32 multiply form
 * independently witnessed on G17G and G17S.  The explicit long-form API keeps
 * its fixed anonymous/context selection separate from the compact G17S form.
 * G17C and all C-suffixed G15/G16 identities remain rejected because the
 * pinned corpus contains no direct target artifacts for them.  Operands are
 * even half-register indices in the conservatively proved 0..126 domain.
 *
 * This is an exact ISA bridge only.  It does not enable DXIL lowering,
 * scheduling, command submission, device admission, runtime use, or any
 * D3D12 feature.
 */
bool d12_agx3_encode_fmul_f32_reg_reg_long(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fmul_f32_reg_reg_long_encoding *out);
bool d12_agx3_decode_fmul_f32_reg_reg_long(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1);

/*
 * Encode/decode the exact four-register f32 fused multiply-add form on the
 * nine targets with direct Apple compiler artifacts: G15G/S/D, G16P/G/S and
 * G17P use the eight-byte AGX3b form; G17G/S use the sixteen-byte AGX3c form.
 * `encoding` must match `chip`.  All operands are even half-register indices
 * in the conservatively proved 0..126 domain, and the two physical multiply
 * inputs are preserved in their encoded order.
 *
 * This strict bridge rejects trailing bytes, anonymous/context alternates,
 * unfused-mad claims, C-suffixed identities without direct target artifacts,
 * and all unsupported registers.  It is an ISA representation only: it does
 * not enable DXIL lowering, contraction policy, scheduling, command
 * submission, device admission, runtime use, or any D3D12 feature.
 */
bool d12_agx3_encode_fma_f32_reg_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t mul0, uint16_t mul1, uint16_t addend,
   struct d12_agx3_fma_f32_reg_reg_reg_encoding *out);
bool d12_agx3_decode_fma_f32_reg_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *mul0, uint16_t *mul1, uint16_t *addend);

/*
 * Encode/decode the exact f32-minimum compiler selection on its nine direct
 * witness targets.  G15G/S/D, G16P/G/S and G17P use the six-byte AGX3b form;
 * G17G/S use the fourteen-byte AGX3c form.  `encoding` must match `chip`, and
 * all operands are even half-register indices in 0..126.  The AGX3c bridge
 * also enforces equality of the duplicated comparison and selection sources.
 *
 * This exact representation rejects other predicates, fmax/generic select,
 * modifiers, trailing bytes, C-suffixed identities without direct artifacts,
 * and unproven registers.  It does not independently define DXIL NaN or
 * signed-zero policy and does not enable lowering, scheduling, runtime use,
 * device admission, command submission, or a D3D12 feature.
 */
bool d12_agx3_encode_fmin_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding, uint16_t dest,
   uint16_t src0, uint16_t src1,
   struct d12_agx3_fmin_f32_reg_reg_encoding *out);
bool d12_agx3_decode_fmin_f32_reg_reg(
   enum agx_chip chip, enum agx_isa_encoding encoding,
   const void *instruction, uint32_t instruction_size, uint16_t *dest,
   uint16_t *src0, uint16_t *src1);

/*
 * Encode/decode only the canonical no-kill i16-register `yield` form on the
 * exact Apple compiler targets present in the source corpus.  The source is a
 * 16-bit half-register index; AGX3b accepts 0..191 and AGX3c accepts 0..255.
 * Alternate operand selections, kill and scheduler/context fields, and the
 * AGX3c ninth register bit are deliberately absent and fail closed.
 *
 * This is a research-only ISA representation.  It does not enable D3D/DXIL
 * lowering, scheduling, command submission, device admission, API features,
 * or ray tracing.
 */
bool d12_agx3_encode_yield_i16_reg(
   enum agx_chip chip, uint16_t src,
   struct d12_agx3_yield_i16_reg_encoding *out);
bool d12_agx3_decode_yield_i16_reg(enum agx_chip chip,
                                   const void *instruction,
                                   uint32_t instruction_size, uint16_t *src);

/*
 * Encode/decode only the dependency mask carried by a canonical terminal
 * `end` on the exact Apple compiler targets present in the source corpus.
 * AGX3b exposes slots 0..5 (masks <= 0x3f); AGX3c additionally exposes slots
 * 6 and 7.  Every other `end` field remains canonical zero.
 *
 * This is a research-only representation of an already terminal wait mask.
 * It is not a standalone wait instruction and does not enable D3D/DXIL
 * lowering, scheduling or memory-ordering policy, command submission, device
 * admission, or API capabilities.
 */
bool d12_agx3_encode_end_with_wait_fence_mask(
   enum agx_chip chip, uint8_t wait_fence_mask,
   struct d12_agx3_end_with_wait_fence_mask_encoding *out);
bool d12_agx3_decode_end_with_wait_fence_mask(
   enum agx_chip chip, const void *instruction, uint32_t instruction_size,
   uint8_t *wait_fence_mask);

/* Encoding-level form used by the target-aware lowering contract above. */
bool d12_agx3_encode_ternary_alu(
   enum agx_isa_encoding encoding, enum d12_agx3_ternary_alu_op op,
   uint16_t dest, uint16_t src0, uint16_t src1, uint16_t src2,
   struct d12_agx3_ternary_alu_encoding *out);

/* Same bounded contract as the binary bridge, for one-register semantics. */
bool d12_agx3_encode_unary_alu(
   enum agx_isa_encoding encoding, enum d12_agx3_unary_alu_op op,
   uint16_t dest, uint16_t src, struct d12_agx3_unary_alu_encoding *out);

#ifdef __cplusplus
}
#endif
