/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Exact AGX3 EslBuilder direct device-load record packer.
 *
 * This is intentionally narrower than a general shader-ISA encoder.  It maps
 * the fixed Register forms queued by Apple's G15/G16/G17P and G17G/S/C
 * EslBuilder implementations to native LDDEV/COPY records and supplies them
 * to the bounded agx_ioto_load_core scheduler.
 */

#pragma once

#include "asahi/lib/agx_ioto_load_core.h"
#include "agx3_mem_core.h"

#ifdef __cplusplus
extern "C" {
#endif

struct agx3_ioto_load_packer {
   enum agx_chip chip;
   enum agx_ioto_load_encoding encoding;
   bool copy_wait_emitted;
};

bool agx3_ioto_load_packer_init(struct agx3_ioto_load_packer *packer,
                                enum agx_chip chip);

/* True only for the exact direct-load register subset implemented here.  This
 * is not the broad production-readiness gate for arbitrary resources. */
bool agx3_ioto_load_absolute_packer_ready(enum agx_chip chip);

void agx3_ioto_load_packer_ops(struct agx3_ioto_load_packer *packer,
                               struct agx_ioto_load_emit_ops *ops);

bool
agx3_ioto_load_finish_round_native(struct agx_ioto_load_core *core,
                                   bool complete_round,
                                   struct agx_ioto_load_workspace workspace);

bool
agx3_ioto_load_absolute_native(struct agx_ioto_load_core *core,
                               const struct agx_ioto_load_absolute *load,
                               struct agx_ioto_load_workspace workspace,
                               struct agx_ioto_load_allocation *allocation);

bool
agx3_ioto_load_from_scs_native(struct agx_ioto_load_core *core,
                               const struct agx_ioto_load_from_fixed *load,
                               struct agx_ioto_load_workspace workspace,
                               struct agx_ioto_load_allocation *allocation);

bool agx3_ioto_load_from_abi_buffer_native(
   struct agx_ioto_load_core *core, enum agx_ioto_abi_buffer_slot source,
   const struct agx_ioto_load_from_fixed *load,
   struct agx_ioto_load_workspace workspace,
   struct agx_ioto_load_allocation *allocation);

#ifdef __cplusplus
}
#endif
