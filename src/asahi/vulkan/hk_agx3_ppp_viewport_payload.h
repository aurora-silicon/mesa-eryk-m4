/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for strict AGX3 viewport-only PPP payload validation.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "asahi/lib/agx_ppp_viewport_payload_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode and validate exactly one bounded viewport-only PPP payload through
 * Mesa's shared target-aware codec.  This is a validation surface only: it
 * does not allocate PPP storage, emit or follow a VDM PPP pointer, alter a
 * Honeykrisp command buffer, submit work, admit an AGX3 device, or advertise
 * a Vulkan capability.
 */
bool hk_agx3_decode_ppp_viewport_payload(
   enum agx_chip chip, const void *payload, size_t size,
   struct agx_ppp_viewport_payload_fields *fields);

#ifdef __cplusplus
}
#endif
