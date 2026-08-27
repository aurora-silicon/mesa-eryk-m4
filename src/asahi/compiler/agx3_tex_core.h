/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Strict AGX3 2D texel-fetch encoding.  This deliberately models only the
 * directly witnessed, non-array LOD0 form with immediate texture selection
 * and sampler zero.  Unsupported texture modes are represented by the
 * absence of an API, rather than guessed selector values.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Long enough for every emitted form: the texel fetch is 14 bytes (AGX3b) /
 * 8 bytes (AGX3c); the filtered sample is 14 bytes (AGX3b) / 22 bytes
 * (AGX3c, smp.ep3). */
#define AGX3_TEX_MAX_LENGTH 22u

enum agx3_tex_width {
   AGX3_TEX_W16 = 1,
   AGX3_TEX_W32 = 2,
};

struct agx3_tex_fetch_desc {
   uint16_t dest;       /* half-register index */
   uint16_t coord;      /* four-half aligned pair of f32 coordinates */
   enum agx3_tex_width width;
   uint8_t mask;        /* directly witnessed fetch masks: 1 or 15 */
   uint8_t texture;     /* immediate texture index, compact-domain 0..15 */
   uint8_t slot;
   uint16_t wait_mask;  /* zero or one compactly encodable dependency */
   bool coord_kill;
   bool final;          /* witnessed final texture-op marker */
};

bool agx3_tex_encode_fetch(enum agx_isa_encoding enc,
                           const struct agx3_tex_fetch_desc *d,
                           uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

/* Strict canonical decode: trailing bytes, alternate forms and any bit pattern
 * that does not repack byte-for-byte through the bounded encoder are rejected.
 */
bool agx3_tex_decode_fetch(enum agx_isa_encoding enc, const void *instruction,
                           uint32_t instruction_size,
                           struct agx3_tex_fetch_desc *out);

/*
 * Filtered 2D sample (`texture_sample` -> smp with f78=2), immediate texture
 * and sampler state, in the two LOD shapes Apple's corpus witnesses:
 *
 *  - AGX3_TEX_LOD_AUTO: the implicit-LOD fragment form (f156=1, no LOD
 *    operand). Witnessed in fragment render pipelines only; fragment
 *    shaders are the only place Mesa emits it, matching the hardware's
 *    quad-derivative requirement.
 *  - AGX3_TEX_LOD_LEVEL: explicit immediate level (f37=1, f139=2), the
 *    `level(x)` kernel form; `lod_x64` is the level in 1/64 steps exactly as
 *    the corpus quantizes it (level(2.0) = 128, level(7.25) = 464).
 *
 * Everything else about the sampler family -- arrays, other dimensions,
 * register/bindless state, register LOD, bias, gradients, min-LOD, offsets,
 * shadow compare, gather -- remains a deliberate refusal: those axes either
 * lack a witness or need the unmodelled pack_smpir marshalling helper.
 */
enum agx3_tex_lod {
   AGX3_TEX_LOD_AUTO,
   AGX3_TEX_LOD_LEVEL,
};

struct agx3_tex_sample_desc {
   uint16_t dest;       /* half-register index */
   uint16_t coord;      /* four-half aligned pair of f32 coordinates */
   enum agx3_tex_width width;
   uint8_t mask;        /* witnessed sample masks: 1, 3, 8, 12, 15 */
   uint8_t texture;     /* immediate texture-state index, 0..15 */
   uint8_t sampler;     /* immediate sampler-state index, 0..15 */
   enum agx3_tex_lod lod;
   uint16_t lod_x64;    /* immediate level * 64 when lod == LEVEL */
   uint8_t slot;
   uint16_t wait_mask;  /* zero or one compactly encodable dependency */
   bool coord_kill;
   bool final;          /* witnessed final texture-op marker */
};

bool agx3_tex_encode_sample(enum agx_isa_encoding enc,
                            const struct agx3_tex_sample_desc *d,
                            uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

/*
 * The scoreboard-signalling coordinate-staging copy Apple issues before smp.
 *
 * Every witnessed smp reads its coordinates through registers that a
 * signalling producer published on the smp's wait slot. When the coordinates
 * do not come from an async op directly, Apple stages them with this `copy`
 * form -- one 32-bit register per instruction, dest == src when the values
 * are already in place (x_coordreg, fs_bufcoord) -- carrying the smp's slot
 * in f12 and any producer dependency as a wait bit (f30 + slot - 1). The smp
 * then waits on that slot.
 */
#define AGX3_TEX_COPY_MAX_LENGTH 10u

bool agx3_tex_stage_copy(enum agx_isa_encoding enc, uint16_t dest,
                         uint16_t src, uint8_t slot, uint16_t wait_mask,
                         uint8_t out[AGX3_TEX_COPY_MAX_LENGTH], uint8_t *len);

#ifdef __cplusplus
}
#endif
