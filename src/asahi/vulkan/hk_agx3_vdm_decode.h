/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for bounded AGX3 VDM State Update template packing and
 * strict VDM record decoding.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "asahi/lib/agx_vdm_decode_core.h"

struct blob;
struct blob_reader;

#ifdef __cplusplus
extern "C" {
#endif

/* Bump whenever the Honeykrisp shader payload below changes.  Vulkan shader
 * binaries carry this version outside the driver-private blob, so stale
 * layouts are rejected before any fields are consumed. */
#define HK_AGX3_SHADER_BINARY_VERSION 1u

bool hk_agx3_shader_binary_version_supported(uint32_t version);

bool hk_agx3_vdm_program_cache_serialize(
   struct blob *blob, const struct agx_vdm_state_update_program_info *info);

bool hk_agx3_vdm_program_cache_deserialize(
   struct blob_reader *blob, struct agx_vdm_state_update_program_info *info);

enum agx_vdm_state_update_source_layout
hk_agx3_vdm_live_source_layout(
   const struct agx_vdm_state_update_program_info *program,
   bool active_context_selector_valid, bool ordinary_draw_path);

/*
 * Decode one record from a potentially longer AGX3 VDM stream through the
 * shared Mesa classifier. This is a validation surface only: it does not
 * follow links, inspect PPP payloads, emit commands, submit work, admit an
 * AGX3 device, or expose a Vulkan capability.
 */
bool hk_agx3_decode_vdm_record(enum agx_chip chip, const void *command,
                               size_t available,
                               struct agx_vdm_decoded_record *record);

/* Validation-only structural bridge.  The live Honeykrisp draw path remains
 * closed until its compiler produces every AGX3 template input directly. */
bool hk_agx3_encode_vdm_state_update_template(
   enum agx_chip chip, const struct agx_vdm_state_update_template_info *info,
   struct agx_vdm_state_update *out);

/* Exact AGX3 VertexProgramVariant selector bridge used by live draw emission.
 * hk_shader source_layout remains UNSPECIFIED until the compiler publishes
 * every raw AGX3 template axis, so that path currently fails closed. */
bool hk_agx3_encode_vdm_state_update_draw(
   enum agx_chip chip, const struct agx_vdm_state_update_draw_info *info,
   struct agx_vdm_state_update *out);

#ifdef __cplusplus
}
#endif
