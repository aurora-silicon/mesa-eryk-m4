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
 * object whose instruction stream is addressed by program_va.  On G17P,
 * ProgramVariantESLState ctor 0x216be7de0 sources variant+0xc44 and +0xc50
 * directly from AGC reply vtable entries +0x42 and +0x4e; finalize
 * 0x216baa9d8 consumes them at 0x216bab4bc and 0x216bab4c4.  Their schema
 * meanings remain unnamed until grounded.  Keep the objects joined so a
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

/* Flat, pointer-free state needed after the Ioto bytes have been uploaded. */
struct agx3_compute_program {
   enum agx_chip chip;
   uint64_t program_va;
   struct agx3_cdm_direct_template launch_template;
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
 * failure.  This does not infer the two LoadShader source meanings, allocate
 * GPU memory, submit work, or admit an AGX3 device.
 */
bool agx3_compute_program_prepare(const struct agx3_compute_program_desc *desc,
                                  void *output, size_t capacity,
                                  size_t *written,
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
