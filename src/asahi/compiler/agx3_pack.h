/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Largest ldimm instruction emitted by the currently proven AGX3 subset. */
#define AGX3_LDIMM_MAX_LENGTH 8
#define AGX3_IADD_U32_IMM8_MAX_LENGTH 12
#define AGX3_IADD_U32_REG_REG_MAX_LENGTH 12
#define AGX3_FADD_F32_REG_REG_LENGTH 6
#define AGX3_FMUL_F32_REG_REG_LENGTH 6
#define AGX3_FMUL_F32_REG_REG_LONG_LENGTH 14
#define AGX3_BIT_AND_U32_REG_REG_LENGTH 10
#define AGX3_BIT_OR_U32_REG_REG_LENGTH 10
#define AGX3_BIT_XOR_U32_REG_REG_LENGTH 10
#define AGX3_BIT_NOT_U32_REG_MAX_LENGTH 10
#define AGX3_ISUB_U32_REG_REG_MAX_LENGTH 12
#define AGX3_SLL_U32_REG_REG_MAX_LENGTH 14
#define AGX3_ASR_I32_REG_REG_MAX_LENGTH 12
#define AGX3_LSR_U32_REG_REG_MAX_LENGTH 14
#define AGX3_IMUL_U32_REG_REG_MAX_LENGTH 14
#define AGX3_IMADD_U32_REG_REG_REG_MAX_LENGTH 14
#define AGX3_YIELD_I16_REG_LENGTH 6
#define AGX3_NOP_LENGTH 2
#define AGX3_THREADGROUP_BARRIER_LENGTH 6
#define AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH 8
#define AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH 14
#define AGX3_END_LENGTH 4
#define AGX3_ENABLE_ALL_THREADS_MAX_LENGTH 8
#define AGX3_UPDATE_NODE_MAX_LENGTH 14

struct agx3_encoded_ldimm {
   uint8_t bytes[AGX3_LDIMM_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_iadd_u32_imm8 {
   uint8_t bytes[AGX3_IADD_U32_IMM8_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_iadd_u32_reg_reg {
   uint8_t bytes[AGX3_IADD_U32_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_fadd_f32_reg_reg {
   uint8_t bytes[AGX3_FADD_F32_REG_REG_LENGTH];
   uint8_t length;
};

struct agx3_encoded_fmul_f32_reg_reg {
   uint8_t bytes[AGX3_FMUL_F32_REG_REG_LENGTH];
   uint8_t length;
};

struct agx3_encoded_fmul_f32_reg_reg_long {
   uint8_t bytes[AGX3_FMUL_F32_REG_REG_LONG_LENGTH];
   uint8_t length;
};

struct agx3_encoded_bit_and_u32_reg_reg {
   uint8_t bytes[AGX3_BIT_AND_U32_REG_REG_LENGTH];
   uint8_t length;
};

struct agx3_encoded_bit_or_u32_reg_reg {
   uint8_t bytes[AGX3_BIT_OR_U32_REG_REG_LENGTH];
   uint8_t length;
};

struct agx3_encoded_bit_xor_u32_reg_reg {
   uint8_t bytes[AGX3_BIT_XOR_U32_REG_REG_LENGTH];
   uint8_t length;
};

struct agx3_encoded_bit_not_u32_reg {
   uint8_t bytes[AGX3_BIT_NOT_U32_REG_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_isub_u32_reg_reg {
   uint8_t bytes[AGX3_ISUB_U32_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_sll_u32_reg_reg {
   uint8_t bytes[AGX3_SLL_U32_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_asr_i32_reg_reg {
   uint8_t bytes[AGX3_ASR_I32_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_lsr_u32_reg_reg {
   uint8_t bytes[AGX3_LSR_U32_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_imul_u32_reg_reg {
   uint8_t bytes[AGX3_IMUL_U32_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_imadd_u32_reg_reg_reg {
   uint8_t bytes[AGX3_IMADD_U32_REG_REG_REG_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_yield_i16_reg {
   uint8_t bytes[AGX3_YIELD_I16_REG_LENGTH];
   uint8_t length;
};

struct agx3_encoded_nop {
   uint8_t bytes[AGX3_NOP_LENGTH];
   uint8_t length;
};

struct agx3_encoded_threadgroup_barrier {
   uint8_t bytes[AGX3_THREADGROUP_BARRIER_LENGTH];
   uint8_t length;
};

struct agx3_encoded_continue_ray_fixed_first_site_witness {
   uint8_t bytes[AGX3_CONTINUE_RAY_FIXED_FIRST_SITE_WITNESS_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_update_bvh_fixed_witness {
   uint8_t bytes[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH];
   uint8_t length;
};

struct agx3_encoded_end {
   uint8_t bytes[AGX3_END_LENGTH];
   uint8_t length;
};

struct agx3_encoded_enable_all_threads {
   uint8_t bytes[AGX3_ENABLE_ALL_THREADS_MAX_LENGTH];
   uint8_t length;
};

struct agx3_encoded_update_node {
   uint8_t bytes[AGX3_UPDATE_NODE_MAX_LENGTH];
   uint8_t length;
};

/*
 * Encode the source-grounded 32-bit-register form of AGX3 `ldimm`.
 *
 * `dest` is a 16-bit half-register index, matching agx_index.value and the
 * register fields recovered from the rp_0..rp_64 probes.  A 32-bit destination
 * must therefore be even.  This deliberately does not expose the other
 * operand-kind forms, the 10-byte encoding points, or M5's unobserved ninth
 * destination bit.
 *
 * Returns false, without producing an instruction, for every unsupported or
 * malformed request.  In particular AGX2/UNKNOWN encodings, odd destinations,
 * and destinations outside Mesa's 256-half-register file fail closed.
 */
bool agx3_pack_ldimm_u32(enum agx_isa_encoding encoding, uint16_t dest,
                         uint32_t imm, struct agx3_encoded_ldimm *out);

/*
 * Decode exactly the same bounded semantic subset.  Unknown/reserved bits must
 * be zero, so a future form is rejected instead of being mistaken for u32.
 */
bool agx3_unpack_ldimm_u32(enum agx_isa_encoding encoding,
                           const uint8_t *bytes, size_t available,
                           uint16_t *dest, uint32_t *imm, uint8_t *length);

/*
 * Encode/decode the source-grounded register-plus-inline-u8 form of 32-bit
 * integer add.  `dest` and `src` are 16-bit half-register indices, so both
 * must be even.  Apple's encoder tables limit this encoding point to r0..r95
 * on AGX3b (half-registers 0..190) and r0..r127 on AGX3c (0..254).
 *
 * The anonymous Apple fields were attached to their semantics by the pinned
 * add-immediate and register-pressure probes: f2 is the destination, f31 the
 * register source, and f15 the complete 8-bit immediate.  The same form has
 * 224 exact G15/G16 corpus witnesses and 218 exact G17 witnesses.  Cross-target
 * compiler replay is byte-identical within {G15*, G16*, G17P} and within
 * {G17G, G17S}; the two encodings are 10 and 12 bytes respectively.
 *
 * This function does not schedule G15G.  Its required standalone nop/stall
 * words remain a separate scheduling pass and are never folded into iadd.
 */
bool agx3_pack_iadd_u32_imm8(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src, uint8_t imm,
   struct agx3_encoded_iadd_u32_imm8 *out);
bool agx3_unpack_iadd_u32_imm8(enum agx_isa_encoding encoding,
                               const uint8_t *bytes, size_t available,
                               uint16_t *dest, uint16_t *src, uint8_t *imm,
                               uint8_t *length);

/*
 * Encode/decode the source-grounded three-register form of 32-bit integer
 * add.  P2i_add identifies f2 as the destination and f16/f31 as its two
 * register sources: the immediately preceding lddev instructions define the
 * exact f16/f31 registers consumed by this instruction.  The independent
 * Q_iadd occurrence ladder reproduces the same form on G15G, G16G and G17S.
 *
 * All arguments are half-register indices and must therefore be even.  This
 * bounded API exposes r0..r95 on AGX3b and r0..r127 on AGX3c.  Apple's AGX3c
 * table contains a ninth register bit, but Mesa does not model that register
 * space and the pinned source probes do not exercise it, so it fails closed.
 * Only the exact anonymous-field selection emitted by the cross-target
 * volatile-load-plus-gid replay is accepted.  The other anonymous-field
 * selections visible in P2i_add and Q_iadd remain unsupported until they are
 * named.  G15G's standalone scheduling nops are deliberately outside this
 * packer.
 */
bool agx3_pack_iadd_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_iadd_u32_reg_reg *out);
bool agx3_unpack_iadd_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the exact compact register-register f32 add selection common
 * to Apple's AGX3b and AGX3c tables.  A forced-live eight-load replay
 * independently correlates f2 with the result, f35 with `src0`, and f18 with
 * `src1` across G15G/S/D, G16P/G/S, G17P, and G17G/S.  Independent delayed-
 * use captures emit this six-byte selection on G15G, G16G, and G17S; high-
 * pressure G16G/G17S captures reproduce it while exercising register bit 7.
 *
 * All operands are even half-register indices in Mesa's modeled r0..r127
 * file.  This packer preserves the two physical source fields even though
 * fadd is commutative.  It accepts only the exact anonymous-field selection
 * in those compact captures.  Modifier-bearing, kill, wait/scheduler-extended
 * and twelve-byte forms, AGX3c's unprobed ninth register bits, and G15G's
 * standalone scheduling nops remain outside this API and fail closed.
 */
bool agx3_pack_fadd_f32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_fadd_f32_reg_reg *out);
bool agx3_unpack_fadd_f32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the exact compact register-register f32 multiply selections
 * emitted by the pinned compiler corpus.  P2f_mul correlates f2 with the
 * result, f35 with `src0`, and f18 with `src1`; the L_fmul and Q_fmul ladders
 * independently reproduce the operand fields.  The selected AGX3b form is
 * the fixed-0xc0 L/P2 selection witnessed across seven targets.  The selected
 * AGX3c form is G17S's fixed-0x20 delayed-use witness.  Those target gates
 * belong to the caller because this shared layer receives an encoding
 * generation rather than a chip.
 *
 * Operands are even half-register indices restricted to 0..126.  The compact
 * corpus does not exercise register bit 7, so it is deliberately not inferred
 * from the separate compact fadd mapping.  Alternate anonymous/context
 * selections (including the 0x20-trailer G15G/G16G Q captures), modifier-
 * bearing and fourteen-byte forms, and G15G's separate scheduling
 * instructions remain outside this exact API and fail closed.
 */
bool agx3_pack_fmul_f32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_fmul_f32_reg_reg *out);
bool agx3_unpack_fmul_f32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the exact fourteen-byte AGX3c register-register f32 multiply
 * selection witnessed byte-for-byte on G17G and G17S.  Their L_fmul ladders
 * are identical and directly exercise the destination field.  G17S's P2f_mul
 * specimen independently correlates f2 with the result, f35 with `src0`, and
 * f18 with `src1`; Apple's pinned AGX3c table supplies the exact deposits for
 * those same fields at encoding point 3.
 *
 * Operands are conservatively restricted to even half-register indices in
 * 0..126 because the direct target artifacts do not exercise register bit 7.
 * Only the fixed anonymous/context selection from the final L operation and
 * P2f_mul is accepted.  The alternate repeated-L selection, compact form,
 * other modifiers, and all non-AGX3c encodings fail closed.  Chip-level gates
 * remain the caller's responsibility.
 */
bool agx3_pack_fmul_f32_reg_reg_long(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_fmul_f32_reg_reg_long *out);
bool agx3_unpack_fmul_f32_reg_reg_long(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded three-register u32 `bit_and` form.
 * P2i_and's two preceding lddev destinations correlate f2 with the result,
 * f16 with one input, and f30 with the other on both Apple table variants.
 * Q_iand_n1..n4 independently reproduce the encoding point on G15G, G16G,
 * and G17S.
 *
 * Registers use even half-register indices.  AGX3b's table covers the whole
 * modeled r0..r127 file.  AGX3c contains an unprobed ninth register bit, so
 * this API deliberately keeps all three operands in r0..r127.  Only the exact
 * anonymous-field selection from the nine-target volatile-load-and-gid replay
 * is accepted; other anonymous-field selections and G15G's standalone
 * scheduling nops remain outside this packer.
 */
bool agx3_pack_bit_and_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_bit_and_u32_reg_reg *out);
bool agx3_unpack_bit_and_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded three-register u32 `bit_or` form.
 * P2i_or's two preceding lddev destinations correlate f2 with the result,
 * f16 with one input, and f30 with the other on both Apple table variants.
 * Q_ior_n1..n4 independently reproduce the encoding point on G15G, G16G,
 * and G17S.
 *
 * Registers use even half-register indices.  AGX3b's table covers the whole
 * modeled r0..r127 file.  AGX3c contains an unprobed ninth register bit, so
 * this API deliberately keeps all three operands in r0..r127.  Only the exact
 * anonymous-field selection from the nine-target volatile-load-and-gid replay
 * is accepted; other anonymous-field selections and G15G's standalone
 * scheduling nops remain outside this packer.
 */
bool agx3_pack_bit_or_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_bit_or_u32_reg_reg *out);
bool agx3_unpack_bit_or_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded three-register u32 `bit_xor` form.
 * P2i_xor's two preceding lddev destinations correlate f2 with the result,
 * f16 with one input, and f30 with the other on both Apple table variants.
 * Q_ixor_n1..n4 independently reproduce the encoding point on G15G, G16G,
 * and G17S.
 *
 * Registers use even half-register indices.  AGX3b's table covers the whole
 * modeled r0..r127 file.  AGX3c contains an unprobed ninth register bit, so
 * this API deliberately keeps all three operands in r0..r127.  Only the exact
 * anonymous-field selection from the nine-target volatile-load-and-gid replay
 * is accepted; other anonymous-field selections and G15G's standalone
 * scheduling nops remain outside this packer.
 */
bool agx3_pack_bit_xor_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_bit_xor_u32_reg_reg *out);
bool agx3_unpack_bit_xor_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded u32 register `bit_not` form, named `inv`
 * by Apple's tables.  The forced-live two-load replay independently
 * correlates f16 with the first lddev destination and f2 with the result
 * consumed by the following integer add.  It reproduces on every available
 * G15/G16/G17 compiler target; Q_iunot_n1..n4 separately establishes the
 * unary operation's occurrence count on G15G, G16G, and G17S.
 *
 * Registers use even half-register indices and are bounded to r0..r127.
 * AGX3c's ninth register bits, other anonymous-field selections, and G15G's
 * standalone scheduling nops remain unsupported and fail closed.
 */
bool agx3_pack_bit_not_u32_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src,
   struct agx3_encoded_bit_not_u32_reg *out);
bool agx3_unpack_bit_not_u32_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src, uint8_t *length);

/*
 * Encode/decode the source-grounded three-register u32 subtraction form,
 * named `isub` on AGX3b and `isub32` on AGX3c by Apple's tables.  The
 * forced-live two-load/three-store replay independently correlates f2 with
 * destination r3, f16 with minuend r2, and f31 with subtrahend r0 on every
 * available G15/G16/G17 compiler target.  Q_isub_n1..n4 independently
 * reproduces the operation on G15G, G16G, and G17S.
 *
 * All arguments are even half-register indices.  Apple's AGX3b encoding
 * point is limited to r0..r95; AGX3c is bounded here to the modeled
 * r0..r127 file, excluding its unprobed ninth register bits.  Other anonymous
 * selections and G15G's standalone scheduling nops remain unsupported.
 */
bool agx3_pack_isub_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_isub_u32_reg_reg *out);
bool agx3_unpack_isub_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded three-register u32 variable shift-left
 * form, named `sll` by Apple's tables.  The forced-live four-store replay
 * independently correlates f2 with destination r3, f16 with value r5, and
 * f31 with masked shift count r0 on every available G15/G16/G17 target.
 * Q_ishl_n1..n4 independently reproduces the operation on G15G, G16G, and
 * G17S.
 *
 * All arguments are even half-register indices.  Apple's AGX3b encoding
 * point is limited to r0..r95; AGX3c is bounded here to modeled r0..r127,
 * excluding its unprobed ninth register bits.  This packs only `sll`, not the
 * separate count-masking operation.  Other anonymous selections and G15G's
 * standalone scheduling nops remain unsupported.
 */
bool agx3_pack_sll_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_sll_u32_reg_reg *out);
bool agx3_unpack_sll_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded three-register i32 arithmetic variable
 * shift-right form, named `sra` by Apple's tables and `asr` by Mesa.  The
 * forced-live four-store replay independently correlates f2 with destination
 * r3, f16 with signed value r5, and f31 with masked shift count r0 on every
 * available G15/G16/G17 compiler target.  P2i_shr and Q_ishr_n1..n4
 * independently reproduce the arithmetic operation on G15G, G16G, and G17S.
 *
 * All arguments are even half-register indices.  Apple's AGX3b encoding
 * point is limited to r0..r95; AGX3c is bounded here to modeled r0..r127,
 * excluding its unprobed ninth register bits.  This packs only `sra`, not the
 * separate count-masking operation.  Other anonymous selections and G15G's
 * standalone scheduling nops remain unsupported.
 */
bool agx3_pack_asr_i32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_asr_i32_reg_reg *out);
bool agx3_unpack_asr_i32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded three-register u32 logical variable
 * shift-right form, named `srl` by Apple's tables and `lsr` by Mesa.  The
 * forced-live four-store replay independently correlates f2 with destination
 * r3, f16 with unsigned value r5, and f31 with masked shift count r0 on every
 * available G15/G16/G17 compiler target.  P2u_shr and L_ushr_n2..n8
 * independently reproduce the logical operation across those generations.
 *
 * All arguments are even half-register indices.  Apple's AGX3b encoding
 * point is limited to r0..r95; AGX3c is bounded here to modeled r0..r127,
 * excluding its unprobed ninth register bits.  This packs only `srl`, not the
 * separate count-masking operation.  Other anonymous selections and G15G's
 * standalone scheduling nops remain unsupported.
 */
bool agx3_pack_lsr_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_lsr_u32_reg_reg *out);
bool agx3_unpack_lsr_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded three-register low-u32 multiply form.
 * Apple's tables name the containing operation `imadd` on AGX3b and
 * `imadd32` on AGX3c; this API accepts only the compiler's implicit-zero
 * addend selection, which is exactly a 32-bit multiply.  The forced-live
 * three-store replay independently correlates f2 with destination r3, f16
 * with one source r0, and f31 with the other source r2 on every available
 * G15/G16/G17 compiler target.  P2i_mul/P2u_mul and the Q_imul/L_imul
 * ladders independently reproduce this low-product operation across
 * generations.
 *
 * All arguments are even half-register indices.  Apple's AGX3b encoding
 * point is limited to r0..r95; AGX3c is bounded here to modeled r0..r127,
 * excluding its unprobed ninth register bits.  Other addend and anonymous
 * selections, signed/unsigned high multiply, and G15G's standalone scheduling
 * nops remain unsupported.
 */
bool agx3_pack_imul_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_imul_u32_reg_reg *out);
bool agx3_unpack_imul_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length);

/*
 * Encode/decode the source-grounded four-register low-u32 multiply-add form,
 * named `imadd` by Apple's AGX3b table and `imadd32` by AGX3c.  The forced-live
 * four-store replay independently correlates f2 with destination r5, f16 with
 * multiplier r2, f31 with multiplier r3, and f46 with real addend register r0
 * on every available G15/G16/G17 compiler target.  P3i/P3u and Q_i3mad
 * independently reproduce the same low-product-plus-addend operation.  Signed
 * and unsigned low forms are bit-identical modulo 2^32.
 *
 * All arguments are even half-register indices.  Apple's AGX3b encoding point
 * is limited to r0..r95; AGX3c is bounded here to modeled r0..r127, excluding
 * its unprobed ninth register bits.  This real-register-addend selection is
 * distinct from the implicit-zero imul selection.  Alternate selections,
 * signed/unsigned high multiply-add, and G15G's standalone scheduling nops
 * remain unsupported.
 */
bool agx3_pack_imadd_u32_reg_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t mul0,
   uint16_t mul1, uint16_t addend,
   struct agx3_encoded_imadd_u32_reg_reg_reg *out);
bool agx3_unpack_imadd_u32_reg_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *mul0, uint16_t *mul1, uint16_t *addend,
   uint8_t *length);

/*
 * Encode/decode the source-grounded i16 register form of `yield`.  Forced-live
 * n=0..4 ladders correlate Apple's f2 field with the get_sr/iadd producer
 * half-registers on G15G/S/D, G16G/P/S, G17P, and G17G/S.  Independent value
 * probes change gid-relative values while holding the source register fixed;
 * the yield encoding remains unchanged.
 *
 * This is deliberately the no-kill, scheduler-neutral form: f11/f13 and all
 * common scheduler/context fields are zero.  Compiler captures establish f13
 * as source last-use/kill context and vary f16 with surrounding scheduling,
 * so neither is exposed here.  Immediate/constant operand selections, kill
 * variants, scheduler fields, G15G's standalone nops, AGX3c's unprobed ninth
 * register bit, and every unknown bit fail closed.  AGX3b is table-bounded to
 * half-registers 0..191; AGX3c is bounded to Mesa's modeled 0..255 file.
 */
bool agx3_pack_yield_i16_reg(
   enum agx_isa_encoding encoding, uint16_t src,
   struct agx3_encoded_yield_i16_reg *out);
bool agx3_unpack_yield_i16_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *src, uint8_t *length);

/*
 * Encode/decode the canonical two-byte scheduler nop.  The zero-field form is
 * common to AGX3b and AGX3c in Apple's independently extracted encoder tables;
 * it is also present throughout the hash-pinned G17 compute corpus.  Only this
 * field-free form is accepted -- scoreboard-bearing extended nop forms remain
 * outside the proven subset.
 */
bool agx3_pack_nop(enum agx_isa_encoding encoding,
                   struct agx3_encoded_nop *out);
bool agx3_unpack_nop(enum agx_isa_encoding encoding, const uint8_t *bytes,
                     size_t available, uint8_t *length);

/*
 * Encode/decode the fixed workgroup barrier emitted for air.wg.barrier.
 * Forced n=0..4 occurrence ladders reproduce the exact six-byte AGX3b form on
 * G15G/G15S/G16G/G17P and the distinct six-byte AGX3c form on G17S.  Apple's
 * independently extracted encoder tables identify each sequence as the
 * field-free `barrier` instruction.  Cross-generation byte strings are
 * rejected rather than being reinterpreted.
 */
bool agx3_pack_threadgroup_barrier(
   enum agx_isa_encoding encoding,
   struct agx3_encoded_threadgroup_barrier *out);
bool agx3_unpack_threadgroup_barrier(enum agx_isa_encoding encoding,
                                     const uint8_t *bytes, size_t available,
                                     uint8_t *length);

/*
 * Encode/decode the canonical unconditional shader terminator.  The exact
 * bytes 0e000000 terminate every decoded code region in the hash-pinned
 * G15/G16/G17 corpus.  Apple's tables expose additional anonymous end fields;
 * those noncanonical forms are deliberately rejected here.  The separately
 * proven wait-fence attachment fields are available only through the narrowly
 * named API below.
 */
bool agx3_pack_end(enum agx_isa_encoding encoding,
                   struct agx3_encoded_end *out);
bool agx3_unpack_end(enum agx_isa_encoding encoding, const uint8_t *bytes,
                     size_t available, uint8_t *length);

/*
 * Encode/decode a canonical terminal `end` carrying the observed one-shot
 * wait-fence attachment set.  Apple's compiler accepts only literal slots
 * 0..5 on the pinned AGX3b targets and slots 0..7 on G17G/G17S (AGX3c).
 * Consecutive waits before `end` accumulate as an idempotent, order-independent
 * bit set.  Exhaustive compiler replays cover all 64 AGX3b and 256 AGX3c sets,
 * and Apple's independently extracted `end` tables reproduce every byte.
 *
 * This API models only the dependency bits already consumed by a terminal
 * `end`.  It is not a standalone `wait.fence` instruction, does not attach a
 * wait to an arbitrary following instruction, and establishes no broader
 * scheduling, memory-ordering, lowering, or submission semantics.  AGX3b
 * slots 6 and 7 are table gaps and compiler-rejected, so masks containing
 * either bit fail closed.  All other `end` fields remain canonical zero.
 */
bool agx3_pack_end_with_wait_fence_mask(
   enum agx_isa_encoding encoding, uint8_t wait_fence_mask,
   struct agx3_encoded_end *out);
bool agx3_unpack_end_with_wait_fence_mask(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint8_t *wait_fence_mask, uint8_t *length);

/*
 * Encode/decode the exact canonical zero-argument
 * llvm.agx3.enable.all.threads machine instruction.  Five-target n=0..4
 * ladders establish a six-byte AGX3b form on G15G/G15S/G16G/G17P and an
 * eight-byte AGX3c form on G17S.  The first instruction in a run has a distinct
 * zero-wait scheduler field; this bounded API deliberately accepts only the
 * repeated canonical form until scheduler placement is modeled by codegen.
 */
bool agx3_pack_enable_all_threads(
   enum agx_isa_encoding encoding,
   struct agx3_encoded_enable_all_threads *out);
bool agx3_unpack_enable_all_threads(enum agx_isa_encoding encoding,
                                    const uint8_t *bytes, size_t available,
                                    uint8_t *length);

/*
 * Encode/decode the fixed, zero-argument llvm.agx3.update.node intrinsic
 * expansion.  Apple's compiler emits a canonical barrier followed by the
 * hardware update_node operation: 6+4 bytes on AGX3b and 6+8 bytes on AGX3c.
 * Both complete sequences are exact occurrence-count isolates; no
 * operand-bearing update.bvh form is accepted here.  Every bit is canonical
 * and a byte sequence from one ISA generation is rejected by the other.
 */
bool agx3_pack_update_node(enum agx_isa_encoding encoding,
                           struct agx3_encoded_update_node *out);
bool agx3_unpack_update_node(enum agx_isa_encoding encoding,
                             const uint8_t *bytes, size_t available,
                             uint8_t *length);

/*
 * Encode/decode only the exact first hardware `continue_ray` instruction
 * witnessed inside Apple's compiler lowering.  The pinned compiler emits the
 * six-byte AGX3b specimen on supported G15/G16/G17P targets and the distinct
 * eight-byte AGX3c specimen on G17G/G17S.  A forced-live 7-operand matrix
 * reproduces these same bytes without correlating any intrinsic operand to a
 * hardware field, so this API deliberately exposes no operands or anonymous
 * fields.
 *
 * The fixed-first-site-witness name is a hard boundary.  This instruction is
 * only one specimen inside a much larger compiler expansion: it excludes the
 * preceding barrier, the second `continue_ray`, shared-memory operations,
 * control flow, and generation-specific scheduling.  It must not be treated
 * as a standalone traversal operation or used for intrinsic lowering,
 * scheduling, or submission.  It does not enable codegen, runtime, or a device
 * target.  Every other instruction prefix, including the compiler's
 * second-site witness, fails closed.
 */
bool agx3_pack_continue_ray_fixed_first_site_witness(
   enum agx_isa_encoding encoding,
   struct agx3_encoded_continue_ray_fixed_first_site_witness *out);
bool agx3_unpack_continue_ray_fixed_first_site_witness(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint8_t *length);

/*
 * Encode/decode only the exact G17S/AGX3c compiler witness for the
 * llvm.agx3.update.bvh expansion: a six-byte barrier followed by the eight-byte
 * update_bvh operation.  An n=0..4 occurrence-count isolate reproduces this
 * complete fourteen-byte sequence exactly n times.  The three n=1->2 through
 * n=3->4 steps are clean insertions after the operand setup paid by n=1.
 *
 * The intrinsic takes arguments, but those arguments have not been correlated
 * to machine fields.  The fixed-witness name is intentional: this is not a
 * general operand encoder and makes no claim about traversal or update
 * semantics.  This encoding-level API accepts the AGX3c tag but does not admit
 * a device target: AGX3b is rejected, G17G/G17C remain unproven, and every
 * noncanonical bit remains unsupported.
 */
bool agx3_pack_update_bvh_fixed_witness(
   enum agx_isa_encoding encoding,
   struct agx3_encoded_update_bvh_fixed_witness *out);
bool agx3_unpack_update_bvh_fixed_witness(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint8_t *length);

#ifdef __cplusplus
}
#endif
