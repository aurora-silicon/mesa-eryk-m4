/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Strict AGX3 2D texel-fetch encoding, including native M4 integer register
 * LOD via PACK_SMPIR. Register LOD plus array layers remains unsupported.
 * Unsupported texture modes are represented by the absence of an API,
 * rather than guessed selector values.
 *
 * The array axis is f77, independent of the dimension selector f76: compiling
 * `texture2d<>::read()` against `texture2d_array<>::read()` with
 * `metal-nt -arch applegpu_g16g` changes that field alone (0 -> 2), leaving
 * the coordinate operand untouched, so the layer is read from the register
 * following the coordinate pair.  A constant layer selects a third value (1)
 * with the layer inline, which is not modelled here.
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

/*
 * How a texture or sampler operand is located. The kind selects which of three
 * consecutive fields carries the value -- the same 1/2/3 convention documented
 * in agx3_copy_core.h -- and immediate vs non-immediate also selects the
 * encoding point (ep3 vs ep4 on AGX3b).
 *
 *     slot      immediate   register   uniform     selector
 *     texture   f95         f96        f97         f103
 *     sampler   f124        f125       f126        f132
 *
 * IMMEDIATE names an index into the texture state registers. REGISTER and
 * UNIFORM carry a zero-extended native heap index in a 64-bit value, located by half-register or
 * uniform-half index; Apple's own shaders load a 16-byte argument-buffer
 * entry and name the texture at +0 and the sampler at +8. All three kinds are
 * witnessed byte-exactly in test/test-agx3-smp-witness.c.
 */
enum agx3_tex_operand_kind {
   AGX3_TEX_OPERAND_IMMEDIATE = 1,
   AGX3_TEX_OPERAND_REGISTER = 2,
   AGX3_TEX_OPERAND_UNIFORM = 3,
};

struct agx3_tex_fetch_desc {
   uint16_t dest;       /* half-register index */
   uint16_t coord;      /* four-half aligned pair of f32 coordinates */
   enum agx3_tex_width width;
   uint8_t mask;        /* Packed result channels, nonempty RGBA subset */
   uint8_t texture;     /* immediate texture index, compact-domain 0..15 */
   uint8_t slot;
   uint16_t wait_mask;  /* zero or one compactly encodable dependency */
   bool coord_kill;
   bool final;          /* witnessed final texture-op marker */
   bool array;          /* 2D array: layer in the register after the pair */
   bool three_d;        /* 3D fetch: z at staging coord + 4. */
   bool multisample;    /* Native MS fetch auxiliary word at coord + 4. */
   bool register_lod;   /* Integer LOD published by PACK_SMPIR in bank 6. */
   bool sparse;         /* M4 residency code follows the four colour lanes. */
   /* SMP f112 is independent of residency: native heap accesses use 4,
    * Apple's extended sparse-resource witnesses use 8. Zero selects 4. */
   uint8_t texture_control;
   bool texture_kill;   /* Last use of an indirect texture index */
   /* Operand kinds. Zero is treated as IMMEDIATE so existing callers, which
    * set only `texture`, keep their exact previous encoding. */
   enum agx3_tex_operand_kind texture_kind;
   enum agx3_tex_operand_kind sampler_kind;
   /* Value for the non-immediate kinds; `texture` remains the immediate
    * index. Half-register index for REGISTER, uniform half for UNIFORM. */
   uint16_t texture_value;
   uint16_t sampler_value;
};

/* G16 native register PACK_SMPIR: layer converts U16 to the array word;
 * sample combines U16 sample and that word. layer_half == UINT16_MAX selects
 * immediate zero for a non-array sample. */
bool agx3_tex_pack_aux(enum agx_isa_encoding enc, bool sample,
                      uint16_t dest_half, uint16_t source_half,
                      uint16_t layer_half, uint16_t wait_mask,
                      uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

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
 * and sampler state, in the LOD shapes Apple's corpus witnesses:
 *
 *  - AGX3_TEX_LOD_AUTO: the implicit-LOD fragment form (no LOD operand).
 *    Witnessed in fragment render pipelines only; fragment shaders are the
 *    only place Mesa emits it, matching the hardware's
 *    quad-derivative requirement.
 *  - AGX3_TEX_LOD_LEVEL: explicit immediate level (f37=1, f139=2), the
 *    `level(x)` kernel form; `lod_x64` is the level in 1/64 steps exactly as
 *    the corpus quantizes it (level(2.0) = 128, level(7.25) = 464).
 *  - AGX3_TEX_LOD_REGISTER: G17P fragment per-lane half-float level, packed
 *    by PACK_SMPIR and selected with f139=4. The f156 marker in the native
 *    terminal specimens is independent of LOD mode.
 *  - AGX3_TEX_LOD_BIAS_REGISTER: M4 2D fragment implicit LOD plus a
 *    shader bias, using the same PACK_SMPIR half operand with f139=3.
 *
 * G17P also supports the witnessed 2D-array implicit-LOD form with a
 * register-valued layer packed into the same auxiliary operand bank.
 * M4 runtime tests qualify this layer with immediate explicit levels too.
 * Register-valued LOD plus an array layer or cube face uses a fused PACK_SMPIR.
 * Everything else about the sampler family -- other dimensions,
 * gradients, min-LOD, offsets,
 * gather -- remains a deliberate refusal: those axes need additional operand
 * marshalling and qualification. M4 2D/array shadow comparisons are admitted
 * with a scalar F32 result and reference in staging half4 (implicit or LOD0).
 */
enum agx3_tex_lod {
   AGX3_TEX_LOD_AUTO,
   AGX3_TEX_LOD_LEVEL,
   AGX3_TEX_LOD_REGISTER, /* PACK_SMPIR publishes the per-lane level. */
   AGX3_TEX_LOD_BIAS_REGISTER, /* Same half operand bank, implicit LOD + bias. */
   AGX3_TEX_LOD_GRAD, /* Interleaved F32 ddx/ddy at staging halves 8 onward. */
};

struct agx3_tex_sample_desc {
   uint16_t dest;       /* half-register index */
   uint16_t coord;      /* four-half aligned pair of f32 coordinates */
   enum agx3_tex_width width;
   uint8_t mask;        /* G17P: every nonempty RGBA subset, packed results */
   uint8_t texture;     /* immediate texture-state index, 0..15 */
   uint8_t sampler;     /* immediate sampler-state index, 0..15 */
   enum agx3_tex_lod lod;
   uint16_t lod_x64;    /* immediate level * 64 when lod == LEVEL */
   uint8_t slot;
   uint16_t wait_mask;  /* zero or one compactly encodable dependency */
   bool coord_kill;
   bool final;          /* witnessed final texture-op marker */
   bool retain_helpers; /* AGX3B: keep helper coordinates for later samples */
   bool sparse;         /* M4 residency word follows the colour result. */
   uint8_t texture_control; /* SMP f112, as for fetches above. */
   bool min_lod;        /* Per-sample half-float minimum LOD. */
   bool min_lod_register; /* Auxiliary bank is full: packed minimum in GPR. */
   uint16_t min_lod_value;
   bool min_lod_discard;
   bool shadow;         /* M4: scalar depth compare, reference in staging half4 */
   bool query_lod;      /* M4 2D clamped/unclamped LOD query. */
   bool gather;         /* Four neighbouring texels from one colour channel. */
   uint8_t gather_component;
   uint16_t sample_offset; /* Signed x/y[/z] nibbles, as in NIR's packed offset. */
   bool dynamic_offset; /* Native packed offset shares bank6 with level/bias. */
   bool array;          /* 2D array: register layer already in operand bank 6 */
   bool three_d;        /* M4 XYZ coordinates at staging halves 0/2/4. */
   bool cube;           /* M4 projected UV plus packed face in operand bank 6 */
   enum agx3_tex_operand_kind texture_kind;
   enum agx3_tex_operand_kind sampler_kind;
   uint16_t texture_value;
   uint16_t sampler_value;
   bool texture_kill;
   bool sampler_kill;
};

/* Pack a scalar half-float LOD into native texture operand bank 6. This bank
 * is separate from GPRs and the coordinate staging bank. */
bool agx3_tex_stage_lod(enum agx_isa_encoding enc, uint16_t src, uint8_t slot,
                       uint16_t wait_mask, bool src_cache, bool src_discard,
                       uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

/* Convert F16 minimum into the full-word scalar SMP operand. */
bool agx3_tex_pack_min_lod(enum agx_isa_encoding enc, uint16_t dest,
                          uint16_t src, uint16_t wait_mask, bool discard,
                          uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

/* Minimum LOD alone in operand bank 4/6, or fused with bias in bank 6. */
bool agx3_tex_stage_min_lod(enum agx_isa_encoding enc, uint16_t src,
                           uint16_t bank, uint16_t packed_base, uint8_t slot, uint16_t wait_mask,
                           bool discard, uint8_t out[AGX3_TEX_MAX_LENGTH],
                           uint8_t *len);
bool agx3_tex_stage_min_bias(enum agx_isa_encoding enc, uint16_t min,
                            uint16_t bias, uint16_t packed_base, uint8_t slot, uint16_t wait_mask,
                            bool discard, uint8_t out[AGX3_TEX_MAX_LENGTH],
                            uint8_t *len);
bool agx3_tex_stage_layer_min(enum agx_isa_encoding enc, uint16_t layer,
                             uint16_t min, uint8_t slot, uint16_t wait_mask,
                             bool layer_discard, bool min_discard,
                             uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

/* M4 texel fetch uses an integer LOD, not the sample path's half float. */
bool agx3_tex_stage_fetch_lod(enum agx_isa_encoding enc, uint16_t src,
                             bool uniform, uint16_t staging_coord,
                             uint8_t slot, uint16_t wait_mask,
                             bool src_cache, bool src_discard,
                             uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

/* Pack an integer array layer or cube face together with a half-float LOD.
 * Native register-indexed descriptors use slot 6;
 * the caller must make the corresponding SMP wait on that staging slot. */
bool agx3_tex_stage_layer_lod(enum agx_isa_encoding enc, uint16_t layer,
                            uint16_t lod, uint8_t slot, uint16_t wait_mask,
                            bool layer_discard, bool lod_discard, bool cube,
                            uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

bool agx3_tex_stage_fetch_layer_lod(enum agx_isa_encoding enc, uint16_t layer,
                                   uint16_t lod, bool uniform, uint16_t staging_coord,
                                   uint8_t slot,
                                   uint16_t wait_mask, bool layer_discard,
                                   bool lod_discard,
                                   uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

bool agx3_tex_stage_cube_array_lod(enum agx_isa_encoding enc, uint16_t face,
                                  uint16_t lod, uint16_t layer, uint8_t slot,
                                  uint16_t wait_mask, bool face_discard,
                                  bool lod_discard,
                                  uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

bool agx3_tex_stage_cube_array(enum agx_isa_encoding enc, uint16_t layer,
                              uint16_t face, uint8_t slot, bool layer_discard,
                              bool face_discard,
                              uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

/* Cube LOD queries publish the projected face with the cube selector. */
bool agx3_tex_stage_cube_face(enum agx_isa_encoding enc, uint16_t src,
                              uint8_t slot, uint16_t wait_mask, bool src_cache,
                              bool src_discard,
                              uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);
bool agx3_tex_stage_array_layer(enum agx_isa_encoding enc, uint16_t src,
                                uint8_t slot, uint16_t wait_mask,
                                bool src_cache, bool src_discard,
                               uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

bool agx3_tex_stage_array_layer_offset(enum agx_isa_encoding enc, uint16_t src,
                                      uint16_t offset, uint8_t slot,
                                      uint16_t wait_mask, bool src_discard,
                                      uint8_t out[AGX3_TEX_MAX_LENGTH], uint8_t *len);

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

/*
 * Exact G17P storage-image write instruction from the native
 * `texture2d<uint, access::read_write>` and sampled-float-to-rgba8 witnesses.
 * The bounded forms name PBE state 0/UINT or state 1/UNORM, consume the fixed
 * coordinate/data staging lanes populated by the caller, and wait on staging
 * slot 2. The wait is the one upstream texture dependency consumed by the
 * first data-staging write.
 */
#define AGX3_IMAGE_WRITE_LENGTH 16u

bool agx3_tex_encode_image_write_rgba8_2d(
   enum agx_isa_encoding enc, uint16_t wait_mask, uint8_t image_index,
   bool normalized,
   uint8_t out[AGX3_IMAGE_WRITE_LENGTH], uint8_t *len);

bool agx3_tex_encode_image_write_indexed_typed(
   enum agx_isa_encoding enc, uint16_t pointer, bool uniform, bool kill, bool floating, bool three_d, bool array, uint16_t staging_base, uint8_t staging_slot,
   uint8_t out[AGX3_IMAGE_WRITE_LENGTH], uint8_t *len);

bool agx3_tex_encode_image_write_indexed_2d(
   enum agx_isa_encoding enc, uint16_t pointer, bool uniform, bool kill, uint16_t staging_base, uint8_t staging_slot,
   uint8_t out[AGX3_IMAGE_WRITE_LENGTH], uint8_t *len);

/* A native staging COPY must preserve a coordinate GPR if later work uses
 * it. CACHE/NONE/DISCARD are independent of the staging-bank release bit. */
bool agx3_tex_stage_copy_hints(enum agx_isa_encoding enc, uint16_t dest,
                               uint16_t src, uint8_t slot, uint16_t wait_mask,
                               bool src_cache, bool src_discard,
                               uint8_t out[AGX3_TEX_COPY_MAX_LENGTH], uint8_t *len);

bool agx3_tex_stage_copy(enum agx_isa_encoding enc, uint16_t dest,
                         uint16_t src, uint8_t slot, uint16_t wait_mask,
                         uint8_t out[AGX3_TEX_COPY_MAX_LENGTH], uint8_t *len);

#ifdef __cplusplus
}
#endif
