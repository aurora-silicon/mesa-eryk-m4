/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded, target-aware AGX3 VDM memory-barrier representation.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_VDM_BARRIER_CORE_LENGTH 4u
#define AGX_VDM_BARRIER_CORE_WORD   UINT32_C(0x21000960)

struct agx_vdm_barrier_core {
   uint8_t bytes[AGX_VDM_BARRIER_CORE_LENGTH];
   uint8_t length;
};

/*
 * Pack/unpack only the exact four-byte type-1 VDM memory barrier emitted by
 * the inspected G15G/G15X, G16G/G16X, G17P, and G17G/G17X Apple Metal
 * plugins.  The target allowlist contains only identities directly served by
 * those plugins; G16P and G17A remain closed.
 *
 * The inspected aggregate G15X path can set word bits 2..4 when the separate
 * DeviceConstants::hasMultipleAGCs property is true.  No per-identity value
 * for that property is pinned, so this helper deliberately rejects that
 * alternate form on every target.  This is a representation/validation
 * helper only and does not emit a live command, submit work, or admit a
 * device.
 */
bool agx_vdm_barrier_core_supported(enum agx_chip chip);

bool agx_vdm_barrier_core_pack(enum agx_chip chip,
                               struct agx_vdm_barrier_core *out);

bool agx_vdm_barrier_core_unpack(enum agx_chip chip, const void *bytes,
                                 size_t size);

#ifdef __cplusplus
}
#endif
