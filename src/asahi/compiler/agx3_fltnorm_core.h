/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * The two AGX3 fragment colour-path instructions Mesa had no producer for:
 * `fltnorm` (float -> normalized lane pack) and `f32mul.ep4` (the perspective
 * divide).
 *
 * Both appear in *every* Apple fragment shader in the G17P corpus, which is
 * why they rank ahead of discard and depth: a fragment shader that writes a
 * normalized render target cannot be assembled without `fltnorm`, and one that
 * reads a perspective-correct varying cannot be assembled the way Apple does
 * without `f32mul.ep4`.
 *
 * ## fltnorm
 *
 * `fltnorm` takes two 32-bit floats and packs them, normalized, into the two
 * halves of one 16-bit register; `lstcol` then stores those 16-bit registers.
 * A four-component 8-bit target therefore costs exactly two `fltnorm`s, which
 * is what every witness shows.
 *
 * Operand model, PROVEN by construction against 19 witnesses (7 from the
 * shipped .metalar objects fmain / fdiscard / ftex / fdepth, 12 from offline
 * `applegpu-nt -arch applegpu_g17p` probes that vary component count, target
 * format, swizzle, attachment count and operand class):
 *
 *   f9  = 2, f2 = destination 16-bit register, f10 = 1 (16-bit)
 *   f23 = source-0 class: 1 immediate (AGX minifloat in f15),
 *                         2 register (f16), 3 uniform (f17); f24 = 2, 32-bit
 *   f40 = source-1 class, same encoding, value in f32 / f33 / f34; f41 = 2
 *   f25 / f27  = source-0 liveness pair, only for a register source:
 *                f27 = 1 last use, f25 = 1 still live
 *   f42 / f44  = the same pair for source 1
 *   f58 = 1 signed-normalized, 0 unsigned-normalized (fn_snorm vs fn_rgba
 *         differ in exactly this bit)
 *   f59 = 1 two packed lanes. A single-component target instead sets f59 = 0,
 *         f40 = 0, f41 = 1 and has no second source (probe fn_r).
 *   f50 + (slot - 1) = the per-slot wait mask, as in every other AGX3b form.
 *
 * The minifloat encoding of an immediate source is Apple's usual one: 0.25 is
 * 16, 0.5 is 32, 0.75 is 40, 1.0 is 48 (probe fn_const, whose two instructions
 * carry exactly those four values).
 *
 * f68, a one-bit field Apple sets on the first instruction of the main program
 * and clears elsewhere, is left clear; it is not modelled because no probe
 * separates it from "first instruction".
 *
 * ## f32mul.ep4
 *
 * AGX3 has no fused projective iterator, so a perspective-correct varying is
 * `f32interp` of the raw value, `f32rcp` of the interpolated 1/W, and a
 * multiply. Apple spells that multiply with encoding point 4 rather than the
 * ordinary `f32mul.ep1` it uses for user multiplies -- probe fn_mul2, which
 * contains both in one shader, separates them cleanly.
 *
 * ep4 carries one extra base field, f70, and across all 20 witnesses in six
 * shaders f70 equals the varying index (`f22`) of the `f32interp` that
 * produced the multiply's raw operand -- including probe fn_swz, where the
 * shader's component order is reversed and f70 follows the varying rather than
 * the program order. INFERRED, on that correlation: f70 names the attribute
 * the divide belongs to. Nothing else about ep4 differs from ep1.
 *
 *   f9 = 2 / f2 general destination (with hint f11), or f9 = 5 / f5 staging
 *          destination on slot f12 -- ftex divides straight into the texture
 *          coordinate staging registers
 *   f35 = the reciprocal operand (f42 = 2 register, f43 = 2 32-bit), liveness
 *         pair f46 last use / f44 still live
 *   f18 = the interpolated operand (f25 = 2, f26 = 2), liveness pair
 *         f29 last use / f27 still live
 *   f80 + (slot - 1) = wait mask
 *
 * ## normflt
 *
 * The inverse, for a tilebuffer *read* of a normalized 8-bit attachment: one
 * 16-bit register in, two 32-bit floats out. Same story as the store side --
 * `lldcol` carries a width, not a format, so the unpack is explicit. Witnessed
 * by three programmable-blending probes (`float4/float2/float [[color(0)]]`)
 * on both encodings.
 *
 *   f9 = 2, f2 = the 32-bit destination pair, f10 = 2
 *   f18 = the 16-bit source, f25 = 2 register, f26 = 1 (16-bit)
 *   f41 = 1 two lanes, 0 one lane -- the same role f59 plays for fltnorm
 *   f32 + (slot - 1) = the per-slot wait mask. PROVEN: the tile load these
 *       consume publishes on slot 6 (AGX3b) or 8 (AGX3c), and exactly the
 *       corresponding bit is set on the first unpack of each pair.
 *
 * ## AGX3c
 *
 * Every operand field id is identical on AGX3c; the encoding differs only by
 * the sub-opcode constants it carries -- f48 = 3, f49 = 72 for `fltnorm`, and
 * f30 = 3, f31 = 115 for `normflt`. That was established by re-running the
 * same probe set against `applegpu_g17s`, which is the AGX3c part.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Longest of the three forms across both encodings: AGX3b's fltnorm is 10
 * bytes and its normflt 8, while AGX3c's fltnorm is 12 and its normflt 10.
 * f32mul.ep4 is 8 on both.
 */
#define AGX3_FLTNORM_MAX_LENGTH 12u

enum agx3_fltnorm_src_kind {
   AGX3_FLTNORM_SRC_NONE = 0,
   AGX3_FLTNORM_SRC_IMMEDIATE = 1, /* AGX 8-bit minifloat */
   AGX3_FLTNORM_SRC_REGISTER = 2,
   AGX3_FLTNORM_SRC_UNIFORM = 3,
};

struct agx3_fltnorm_src {
   uint8_t kind; /* enum agx3_fltnorm_src_kind */
   uint16_t value;
   bool kill; /* register sources only: this is the last read */
};

struct agx3_fltnorm_desc {
   uint16_t dest;                 /* 16-bit (half) register index */
   bool dest_hint;                /* f11, unmodelled; clear in every witness */
   struct agx3_fltnorm_src src[2];
   bool two_lanes;                /* pack both sources; false = one lane */
   bool snorm;                    /* signed-normalized target */
   uint16_t wait_mask;            /* bit i waits scoreboard slot i + 1 */
};

bool agx3_fltnorm_encode(enum agx_isa_encoding enc,
                         const struct agx3_fltnorm_desc *d,
                         uint8_t out[AGX3_FLTNORM_MAX_LENGTH], uint8_t *len);

/*
 * Each register operand of an AGX3 float op picks between two bits: one marks
 * the read as the operand's last, the other marks the value as still live.
 * Apple also leaves both clear on occasional operands (one of the seventeen
 * perspective multiplies here does), so this is a hint rather than a
 * three-valued fact about liveness, and UNMARKED -- claiming nothing -- is the
 * conservative default.
 */
enum agx3_operand_liveness {
   AGX3_OPERAND_UNMARKED = 0,
   AGX3_OPERAND_LIVE,
   AGX3_OPERAND_KILL,
};

struct agx3_persp_mul_desc {
   bool stage_dest;      /* destination is a staging register (f9 = 5) */
   uint16_t dest;        /* 32-bit register or staging index */
   uint8_t slot;         /* staging destinations only: scoreboard slot */
   uint16_t reciprocal;  /* 32-bit register holding 1/W */
   enum agx3_operand_liveness reciprocal_liveness;
   uint16_t raw;         /* 32-bit register holding the raw varying */
   enum agx3_operand_liveness raw_liveness;
   uint8_t vary_index;   /* the f32interp varying index this divide belongs to */
   bool dest_hint;       /* f11; set in every general-destination witness */
   uint16_t wait_mask;
};

bool agx3_persp_mul_encode(enum agx_isa_encoding enc,
                           const struct agx3_persp_mul_desc *d,
                           uint8_t out[AGX3_FLTNORM_MAX_LENGTH], uint8_t *len);

struct agx3_normflt_desc {
   uint16_t dest;      /* 32-bit register pair receiving the unpacked floats */
   bool dest_hint;     /* f11; set in every witness */
   uint16_t src;       /* 16-bit register holding the packed lanes */
   bool two_lanes;     /* unpack both bytes; false = one lane */
   uint16_t wait_mask; /* bit i waits scoreboard slot i + 1 */
};

bool agx3_normflt_encode(enum agx_isa_encoding enc,
                         const struct agx3_normflt_desc *d,
                         uint8_t out[AGX3_FLTNORM_MAX_LENGTH], uint8_t *len);

#ifdef __cplusplus
}
#endif
