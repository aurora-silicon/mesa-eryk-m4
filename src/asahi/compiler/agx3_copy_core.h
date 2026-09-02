/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * AGX3 `copy` -- the register-file move.
 *
 * AGX3 operands carry a kind selector: 1 = immediate, 2 = register, 3 =
 * uniform, with the operand value deposited in a different field per kind
 * (slot A: f15 immediate / f16 register / f17 uniform; slot B: f31 register /
 * f32 uniform).  That mapping is not a guess: it is read straight off Apple's
 * own corpus vectors in test/agx3_compose_corpus_vectors.c, where `lddev`
 * selects a uniform base with f38 = 3 + f32, `iadd` adds a uniform with
 * f38 = 3 + f32, and 49 of the 100 G16G `copy` instructions move a uniform
 * into a register with f23 = 3 + f17.
 *
 * A uniform therefore reaches a GPR on AGX3 exactly the way Apple's compiler
 * puts it there: one 4-byte `copy` (ep0), 32-bit, no wait bits.  Mesa's
 * agx_lower_uniform_sources() already materializes such a move for every
 * generic-ALU uniform source on AGX3; without this encoder that move lowers
 * to a `bitop` whose source is still a uniform, which the packer refuses --
 * so every shader that reads a pushed uniform in an ALU op fails to compile.
 *
 * Only the witnessed form is encoded.  Everything else (16- and 64-bit
 * widths, modifiers, wait masks, the register-source `copy`) fails closed.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apple's `copy` ep0 is 4 bytes in both encodings. */
#define AGX3_COPY_MAX_LENGTH 8u

/*
 * Encode `copy dest32, uniform32`.  Both indices are 16-bit half-register
 * units, the unit agx_index.value already carries.  Returns false -- writing
 * nothing -- for an unknown encoding, a misaligned or out-of-range index, or
 * any value the composer refuses.
 */
/*
 * Encode `copy dest16, src16` -- a 16-bit register-to-register move. Mesa
 * reaches this through the same bitop-with-MOV-truth-table lowering, but the
 * 32-bit `iadd d, s, #0` trick the select core uses is grounded only at 32
 * bits, and booleans live in 16-bit halves. Apple's own corpus carries this
 * form (copy.ep0 with a register source and f24 = 1), including odd
 * destination halves, which need no separate encoding point here.
 *
 * Scoreboard waits ride on f30 + slot - 1, the same convention
 * agx3_tex_stage_copy() documents; a corpus copy with f30 set reproduces
 * byte-exactly through that field.
 */
bool agx3_copy_encode_reg16(enum agx_isa_encoding encoding, uint16_t dest_half,
                            uint16_t src_half, uint16_t wait_mask,
                            uint8_t out[AGX3_COPY_MAX_LENGTH], uint8_t *len);

/*
 * Encode `copy uniform, reg32` -- the preamble's uniform write, which is what
 * Mesa calls uniform_store.
 *
 * Apple's own constant program (the `_agc.main.constant_program` half of every
 * shader Mach-O) writes its uniforms with an ordinary `copy` whose f9 is 3
 * rather than 2, moving the destination from f2 into f3 and making it a
 * UNIFORM half. Four consecutive writes to uniform halves 20, 22, 24 and 26
 * witness it byte-exactly.
 *
 * `kill` is f27, the source register's last-use hint. It is liveness, not a
 * baseline bit -- forcing it on cost two compute probes on hardware once
 * already; see the note in agx3_iadd_core.h.
 */
bool agx3_copy_encode_reg_to_uniform(enum agx_isa_encoding encoding,
                                     uint16_t uniform_half, uint16_t src_half,
                                     bool kill,
                                     uint8_t out[AGX3_COPY_MAX_LENGTH],
                                     uint8_t *len);

bool agx3_copy_encode_uniform_to_reg32(enum agx_isa_encoding encoding,
                                       uint16_t dest_half,
                                       uint16_t uniform_half,
                                       uint8_t out[AGX3_COPY_MAX_LENGTH],
                                       uint8_t *len);

#ifdef __cplusplus
}
#endif
