/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for the exact bounded AGX3 Ioto program composer.
 */

#pragma once

#include "asahi/lib/agx3_compute_program_core.h"
#include "asahi/lib/agx_ioto_program_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hk_agx3_ioto_program_desc {
   uint64_t program_va;
   const void *body;
   size_t body_size;
   struct agx_ioto_state_tail tail;
   uint16_t profile_payload0;
   uint16_t profile_payload1;
};

/*
 * Byte-generation bridge only; success does not admit a physical device.
 * This exposes only the straight-line absent-preshader form and preserves
 * output and *written on every failure.
 */
bool hk_agx3_ioto_program_generate(
   enum agx_chip chip, const struct hk_agx3_ioto_program_desc *desc,
   void *output, size_t capacity, size_t *written);

/*
 * Compose an exact terminal suffix without PIXREL.  Inline/external suffixes
 * replace the absent 0e terminal; they are never appended after it.
 */
bool hk_agx3_ioto_program_generate_no_pixrel(
   enum agx_chip chip, const struct hk_agx3_ioto_program_desc *desc,
   const struct agx_ioto_preshader_desc *preshader, void *output,
   size_t capacity, size_t *written);

/* Honeykrisp bridge for G17P's default-profile program with no resource-load
 * records. */
bool hk_agx3_g17p_no_resource_program_generate(
   enum agx_chip chip, uint64_t program_va,
   const struct agx_ioto_state_tail_output *packed_tail, void *output,
   size_t capacity, size_t *written);

struct hk_agx3_g17p_graphics_program_desc {
   uint64_t program_va;
   uint64_t shader_va;
   uint32_t shader_binary_size;
   uint32_t scratch_size;
   uint16_t nr_gprs;
   bool fragment;
};

struct hk_agx3_g17p_graphics_program {
   uint8_t bytes[AGX_IOTO_G17P_NO_RESOURCE_PROGRAM_SIZE];
   uint8_t length;
};

/* Build the position-only vertex or flat-colour fragment Ioto program used by
 * the basic G17P triangle.  The two stage-specific SPECLM records, LDSHDR text
 * span rule, SPECTPR compiler rule, and default profile are all byte-exact. */
bool hk_agx3_g17p_basic_graphics_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_graphics_program_desc *desc,
   struct hk_agx3_g17p_graphics_program *program);

struct hk_agx3_g17p_rt0_eot_program_desc {
   uint64_t program_va;
   uint64_t shader_va;
   uint64_t pbe_argument_va;
   uint32_t shader_binary_size;
   uint32_t scratch_size;
   uint16_t nr_gprs;
};

struct hk_agx3_g17p_rt0_eot_program {
   uint8_t bytes[AGX_IOTO_G17P_RT0_EOT_PROGRAM_SIZE];
   uint16_t length;
};

/* Build the one-attachment G17P EOT Ioto program. The PBE argument address is
 * materialized into the first two LDIMM records; the remaining records retain
 * the compiler's RT0 selector and resource contract. */
bool hk_agx3_g17p_rt0_eot_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_rt0_eot_program_desc *desc,
   struct hk_agx3_g17p_rt0_eot_program *program);

/*
 * Host-only admission bridge for the exact G17S minimal compute witness.
 * The explicit chip argument is intentionally rejected for every sibling
 * identity; this does not publish a Vulkan feature or admit a device.
 */
bool hk_agx3_g17s_minimal_compute_program_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc, void *output,
   size_t capacity, size_t *written, struct agx3_compute_program *program);

/* G17S-only mapped upload/launch-address join.  This is host-side admission
 * plumbing and does not publish a Vulkan feature or device capability. */
bool hk_agx3_g17s_minimal_compute_program_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program);

/* Exact G17S live-resource bridge.  The shared core owns all target,
 * relocation and transactional validation; this publishes no Vulkan cap. */
bool hk_agx3_g17s_minimal_compute_program_live_upload_prepare(
   enum agx_chip chip,
   const struct agx3_g17s_minimal_compute_program_desc *desc,
   const struct agx_ioto_g17s_minimal_compute_resources *resources,
   const struct agx3_compute_program_upload *upload, size_t *written,
   struct agx3_compute_program *program);

/* Append one live-admitted G17S launch plus its strong barrier.  This is a
 * bounded byte bridge only and does not publish a Vulkan capability. */
bool hk_agx3_g17s_minimal_compute_dispatch_append(
   const struct agx3_compute_program *program,
   const struct agx3_compute_direct_grid *grid,
   struct agx3_compute_dispatch_sink *sink);

#ifdef __cplusplus
}
#endif
