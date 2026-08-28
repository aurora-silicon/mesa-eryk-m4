/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for the exact bounded AGX3 Ioto program composer.
 */

#pragma once

#include "asahi/compiler/agx3_ioto_uniform_program.h"
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
   const struct agx3_ioto_uniform_range *uniform_ranges;
   size_t uniform_range_count;
   uint32_t shader_binary_size;
   uint32_t scratch_size;
   uint16_t nr_gprs;
   bool fragment;
};

#define HK_AGX3_G17P_GRAPHICS_PROGRAM_MAX_SIZE 512u

struct hk_agx3_g17p_graphics_program {
   uint8_t bytes[HK_AGX3_G17P_GRAPHICS_PROGRAM_MAX_SIZE];
   uint16_t length;
};

/* Build the bounded G17P vertex or fragment Ioto program. Zero ranges preserve
 * the exact no-resource triangle bytes. Up to three dword-aligned absolute
 * uniform ranges add the CProg loads used by two vertex attributes and one
 * descriptor-set pointer. */
bool hk_agx3_g17p_basic_graphics_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_graphics_program_desc *desc,
   struct hk_agx3_g17p_graphics_program *program);

enum hk_agx3_g17p_rt0_bg_op {
   HK_AGX3_G17P_RT0_BG_CLEAR,
   HK_AGX3_G17P_RT0_BG_RELOAD,
};

#define HK_AGX3_G17P_RT0_CLEAR_PUSH_HALFS 12u

struct hk_agx3_g17p_rt0_bg_program_desc {
   uint64_t program_va;
   uint64_t shader_va;
   uint64_t clear_value_va;
   uint64_t texture_va;
   uint64_t sampler_va;
   uint32_t shader_binary_size;
   uint32_t scratch_size;
   uint16_t nr_gprs;
   enum hk_agx3_g17p_rt0_bg_op op;
};

struct hk_agx3_g17p_rt0_bg_program {
   uint8_t bytes[AGX_IOTO_G17P_RT0_RELOAD_PROGRAM_MAX_SIZE];
   uint16_t length;
};

/* Build the one-attachment clear or reload background program. Clear maps one
 * 16-byte value to uniform halfs 4..11. Reload maps one 32-byte Gen9 texture
 * descriptor and one 32-byte HAL200 sampler heap element to consecutive CProg
 * resource slots. Both use the default G17P background resource spec. */
bool hk_agx3_g17p_rt0_bg_program_generate(
   enum agx_chip chip, const struct hk_agx3_g17p_rt0_bg_program_desc *desc,
   struct hk_agx3_g17p_rt0_bg_program *program);

bool hk_agx3_g17p_rt0_bg_shader_resources_match(
   enum hk_agx3_g17p_rt0_bg_op op, unsigned push_count, bool uses_txf);

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
