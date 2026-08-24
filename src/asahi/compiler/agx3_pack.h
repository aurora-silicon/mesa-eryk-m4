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
#define AGX3_NOP_LENGTH 2
#define AGX3_THREADGROUP_BARRIER_LENGTH 6
#define AGX3_END_LENGTH 4
#define AGX3_ENABLE_ALL_THREADS_MAX_LENGTH 8
#define AGX3_UPDATE_NODE_MAX_LENGTH 14

struct agx3_encoded_ldimm {
   uint8_t bytes[AGX3_LDIMM_MAX_LENGTH];
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
 * those noncanonical forms are deliberately rejected until their effects are
 * independently established.
 */
bool agx3_pack_end(enum agx_isa_encoding encoding,
                   struct agx3_encoded_end *out);
bool agx3_unpack_end(enum agx_isa_encoding encoding, const uint8_t *bytes,
                     size_t available, uint8_t *length);

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

#ifdef __cplusplus
}
#endif
