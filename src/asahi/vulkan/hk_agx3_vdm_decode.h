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

#ifdef __cplusplus
extern "C" {
#endif

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

/* Exact AGX3 VertexProgramVariant selector bridge.  This remains an admission
 * boundary until hk_shader carries every raw AGX3 template axis. */
bool hk_agx3_encode_vdm_state_update_draw(
   enum agx_chip chip, const struct agx_vdm_state_update_draw_info *info,
   struct agx_vdm_state_update *out);

#ifdef __cplusplus
}
#endif
