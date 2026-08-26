/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Exact bounded join between an AGX3 Ioto compute program and its native
 * direct CDM launch record.
 */

#pragma once

#include "agx3_cdm_direct_launch_semantic_core.h"
#include "agx_ioto_program_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Apple derives the two launch-template fields from the same LoadShader
 * object whose instruction stream is addressed by program_va.  On HAL300,
 * ProgramVariantESLState sources ShaderInfo::max_total_threads_per_threadgroup
 * and the low word of ShaderInfo::extended_texture_write_mask_qword_1 from
 * AGC reply vtable entries +0x42 and +0x4e.  Keep the objects joined so a
 * frontend cannot accidentally launch one Ioto program with another
 * program's template or with an AGX2 shader-base-relative USC pointer.
 */
struct agx3_compute_program_desc {
   enum agx_chip chip;
   uint64_t program_va;

   const void *body;
   size_t body_size;
   struct agx_ioto_state_tail tail;
   struct agx_ioto_profile_control profile;
   struct agx_ioto_preshader_desc preshader;

   struct agx3_cdm_direct_template_sources launch_sources;
};

struct agx3_g17s_minimal_compute_program_desc {
   uint64_t program_va;
   uint64_t shader_va;

   /* Exact ProgramVariantESLState UniformData/loadFromUserBuffer inputs for
    * the witnessed root-uniform pointer.  These names preserve the Apple
    * call ABI: destination/count are the values at UniformData +0x30/+0x38,
    * and temporary_base is loadFromUserBuffer's final argument. */
   struct {
      uint32_t user_buffer_binding;
      uint32_t byte_offset;
      uint32_t uniform_data_destination;
      uint32_t uniform_data_count;
      uint32_t temporary_base;
   } root_uniform;

   struct agx3_cdm_direct_template_sources launch_sources;
};

/* Flat, pointer-free state needed after the Ioto bytes have been uploaded. */
struct agx3_compute_program {
   enum agx_chip chip;
   uint64_t program_va;
   struct agx3_cdm_direct_template launch_template;
};

/* Caller-owned CPU mapping of one GPU allocation.  Apple writes the dynamic
 * state-loader program at a 64-byte-aligned mapped address, then puts that
 * mapping's corresponding GPU address into the CDM launch in 64-byte units.
 * Keeping both views in one object prevents a successfully composed program
 * from being launched through a different allocation. */
struct agx3_compute_program_upload {
   void *cpu;
   size_t capacity;
   uint64_t gpu_va;
};

struct agx3_compute_direct_grid {
   enum agx3_cdm_direct_grid_source source;
   uint32_t size[3];
   uint32_t local_size[3];
   uint8_t dimensions;

   /* Valid only for the aggregate G15X identities accepted by the lower
    * launch codec.  It must remain false for G17P/A18 Pro. */
   bool g15x_has_multiple_agcs;
};

/*
 * Compose the Ioto program and prepare its native launch state as one
 * transaction.  output, *written and *program are unchanged on every
 * failure.  This does not infer either ShaderInfo value from legacy Mesa USC
 * counts, allocate GPU memory, submit work, or admit an AGX3 device.
 */
bool agx3_compute_program_prepare(const struct agx3_compute_program_desc *desc,
                                  void *output, size_t capacity,
                                  size_t *written,
                                  struct agx3_compute_program *program);

/* Join the exact 194-byte G17S minimal Ioto witness to its native direct-launch
 * template.  The root-uniform source must match Apple's independently traced
 * buffer(1), offset-zero load into temporary base zero.  Its result is loaded
 * at half24 and copied to CProg half8.  This template form intentionally
 * retains Apple's zero runtime relocations and does not admit a live frontend
 * path. */
bool agx3_g17s_minimal_compute_program_prepare(
   const struct agx3_g17s_minimal_compute_program_desc *desc, void *output,
   size_t capacity, size_t *written, struct agx3_compute_program *program);

/* Exact mapped-allocation handoff for the G17S witness.  Both cpu and gpu_va
 * must identify the beginning of the same 64-byte-aligned allocation, and
 * gpu_va must equal desc->program_va.  No sibling target is admitted.  The
 * mapped bytes, *written and *program are unchanged on every failure.  This
 * does not patch the live gather or root-uniform addresses. */
bool agx3_g17s_minimal_compute_program_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program);

/* G17S-only live-resource handoff.  This is the exact direct-enqueue contract
 * recovered for the minimal witness: the mapped program allocation is joined
 * to the secondary gather allocation VA and UserComputeArgumentTable GPU VA
 * entry 1 and the constructor-owned absolute UniformData source VA.
 * loadImmediateUserBuffer passes the same kind-3 Register zero to the pointer
 * load and the leading COPY pair, so no separate hardware entry seed is
 * required.  The mapping, *written and *program are unchanged on failure. */
bool agx3_g17s_minimal_compute_program_live_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   const struct agx_ioto_g17s_minimal_compute_resources *resources,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program);

/*
 * Encode a canonical 40-byte direct launch whose 48-bit pipeline pointer and
 * template are taken exclusively from a prepared program.  packed is cleared
 * on every failure.
 */
bool agx3_compute_program_pack_direct_launch(
   const struct agx3_compute_program *program,
   const struct agx3_compute_direct_grid *grid,
   uint8_t packed[AGX3_CDM_DIRECT_LAUNCH_SIZE]);

#ifdef __cplusplus
}
#endif
