/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for strict AGX3 PPP W-clamp/Cull-2 validation.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "asahi/lib/agx_ppp_w_clamp_cull2_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode and validate exactly one bounded W-clamp/Cull-2 PPP payload through
 * Mesa's shared target-aware codec.  This validation surface does not alter a
 * Honeykrisp command buffer, allocate PPP storage, emit or follow a VDM PPP
 * pointer, submit work, admit an AGX3 device, or advertise a Vulkan feature.
 */
bool hk_agx3_decode_ppp_w_clamp_cull2(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_w_clamp_cull2_fields *fields);

#ifdef __cplusplus
}
#endif
