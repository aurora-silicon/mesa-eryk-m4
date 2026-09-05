/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * AGX3 `iadd` with a constant addend, an optional shift, and a narrow source.
 *
 * This is the shape all constant address arithmetic takes -- `base + i*4 + k`
 * -- so it appears in any shader with more than one memory access. It was
 * previously unencodable in the compact path and silently mis-encoded in the
 * wait-carrying one, where an immediate was deposited into a register field.
 *
 * Every field below is probe-witnessed with `metal-nt -arch applegpu_g16g`,
 * one MSL kernel per variable:
 *
 *   f2   destination register half        (32-bit)
 *   f15  slot-A immediate, 8-bit          (kind fields f23 = f24 = 1)
 *   f31  slot-B register half             (kind field f38 = 2)
 *   f39  slot-B width                     1 = 16-bit, 2 = 32-bit
 *   f43  slot-B sign-extend               `short` sets it, `ushort` does not
 *   f54  shift applied to slot B          0..3 witnessed, one kernel each
 *   f45..f50  scoreboard wait, slots 1..6
 *
 * AGX3c spells it `iadd32` with the same operand fields, its own encoding
 * constants (f44 = 3, f45 = 80), the shift moved to f55, and eight wait slots
 * at f46..f53. The same four-kernel ladder was run against
 * `-arch applegpu_g17s` to witness it.
 *
 * On AGX3b, f42 is the slot-B last-use hint for either slot-A kind. Fresh
 * G17P `o[id] = id << N` witnesses clear it because the store still needs
 * id; loaded values dead after the add set it. Drive it from IR liveness.
 * The separately witnessed AGX3c baseline remains unchanged.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX3_IADD_MAX_LENGTH 12u

struct agx3_iadd_desc {
   /*
    * Slot A is normally the immediate below. Set this to add a REGISTER
    * instead -- f23/f24 move from 1 (immediate, value in f15) to 2
    * (register, value in f16), the same operand-kind convention used
    * throughout AGX3. Witnessed byte-exactly against the shifted adds Apple
    * emits for `a*b + (c << N)`, where f54 carries N.
    */
   bool src_a_is_register;
   uint16_t src_a_reg;
   /*
    * Last-use (kill) hints for the two register operands, f27 for slot A and
    * f42 for slot B. These are LIVENESS, not constants -- agx3_mem_core.c
    * drives the same two fields from idx_kill/base_kill. Apple's witnesses
    * happen to carry both set because their operands are dead there; forcing
    * them on for a live register told the hardware it could be reused and
    * cost two compute probes on real hardware. Drive them from the IR.
    */
   bool src_a_kill;
   bool src_kill;
   uint16_t dest;      /* 32-bit register half, 2-aligned */
   uint8_t immediate;  /* the constant addend */
   uint16_t src;       /* register half added to it */
   bool src_16;        /* source is a single 16-bit half */
   bool sign_extend;   /* narrow source is sign- rather than zero-extended */
   uint8_t shift;      /* source shifted left by this first; 0..3 */
   uint16_t wait_mask; /* bit i waits scoreboard slot i + 1 */
};

bool agx3_iadd_encode(enum agx_isa_encoding encoding,
                      const struct agx3_iadd_desc *desc,
                      uint8_t out[AGX3_IADD_MAX_LENGTH], uint8_t *len);

/* Native G17P register minus an 8-bit unsigned immediate. Subtraction is
 * not commuted: the register occupies slot A, immediate slot B. */
struct agx3_isub_imm_desc {
   uint16_t dest, src, immediate;
   bool src_16, sign_extend;
   bool src_cache, src_kill;
   uint16_t wait_mask;
};
bool agx3_isub_immediate_encode(enum agx_isa_encoding encoding,
                                 const struct agx3_isub_imm_desc *desc,
                                 uint8_t out[AGX3_IADD_MAX_LENGTH], uint8_t *len);

/*
 * `imad dst32, srcA, #imm, srcC` -- multiply-accumulate with a constant
 * multiplier. This is how an index-derived address is computed
 * (`gid * stride + base`), so every SSBO access that is not at a constant
 * index goes through it.
 *
 * Witnessed the same way, with `o[i] = a[i] * K + b[i]` kernels over several
 * multipliers (129, 253, 255 -- smaller constants get strength-reduced away
 * before they reach the encoder):
 *
 *   f2   destination           f16  slot-A register
 *   f30  slot-B immediate      (kind fields f38 = f39 = 1)
 *   f46  slot-C register
 *   f54  slot-C width          2 = 32-bit, 1 = 16-bit
 *   f58  slot-C sign-extend    `short` sets it, `ushort` does not
 *   f60..f65  scoreboard wait, slots 1..6
 *
 * Native reused-input variants establish per-source cache/discard hints:
 * f25/f27 (A), f40/f42 (B), and f55/f57 (C). The multiply's shift field is
 * not modelled, so a shifted imad is refused rather than encoded with the
 * shift dropped -- which is exactly the silent failure this replaces.
 */
struct agx3_imad_desc {
   uint16_t dest;      /* 32-bit register half */
   uint16_t src_a;     /* multiplicand register half */
   bool src_a_kill, src_a_cache;
   uint8_t multiplier; /* constant multiplied by src_a (immediate form) */
   /*
    * Register multiplier. When set, src_a is multiplied by the 32-bit
    * register at `multiplier_reg` instead of the `multiplier` constant, and
    * the slot-B kind moves from immediate (f38/f39 = 1, value in f30) to
    * register (f38/f39 = 2, value in f31) -- the same 1/2/3 operand-kind
    * convention used throughout AGX3. Witnessed byte-exactly against Apple's
    * own `o[i] = a[i]*b[i] + c[i]` compute kernel; see the imad vectors in
    * test-agx3-iadd-core.c.
    */
   bool multiplier_is_register;
   uint16_t multiplier_reg;
   bool multiplier_kill; /* f42: last use of the multiplier register */
   bool multiplier_cache;
   uint16_t src_c;     /* addend register half */
   bool src_c_kill, src_c_cache;
   /*
    * Pure multiply, no addend. AGX3 has no shifted imad: to build
    * `a*b + (c << s)` Apple emits this form and then a shifted iadd, which is
    * exactly what the backend does for AGX3. The addend operand is absent
    * rather than zero -- a distinct encoding, witnessed against Apple's
    * `o[i] = a[i]*b[i] + (c[i] << N)` kernels, whose imadd is byte-identical
    * for N = 1, 2 and 3 because the shift never reaches this instruction.
    */
   bool no_addend;
   bool src_c_16;      /* addend is a single 16-bit half */
   bool src_c_sign_extend; /* narrow addend is sign- rather than zero-extended */
   uint16_t wait_mask; /* bit i waits scoreboard slot i + 1 */
};

bool agx3_imad_encode(enum agx_isa_encoding encoding,
                      const struct agx3_imad_desc *desc,
                      uint8_t out[AGX3_IADD_MAX_LENGTH], uint8_t *len);

#ifdef __cplusplus
}
#endif
