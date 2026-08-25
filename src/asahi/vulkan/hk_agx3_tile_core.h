/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Honeykrisp bridge for the bounded AGX3 single-sample tile instruction
 * codec.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/compiler/agx3_tile_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HK_AGX3_TILE_MAX_LENGTH 14u

enum hk_agx3_tile_width {
   HK_AGX3_TILE_WIDTH_8 = 8,
   HK_AGX3_TILE_WIDTH_16 = 16,
   HK_AGX3_TILE_WIDTH_32 = 32,
};

struct hk_agx3_tile_store_desc {
   uint16_t data;
   enum hk_agx3_tile_width width;
   uint8_t component_mask;
   uint16_t tile_offset;
   uint16_t wait_mask;
};

struct hk_agx3_tile_load_desc {
   uint16_t dest;
   enum hk_agx3_tile_width width;
   uint8_t component_mask;
   uint16_t tile_offset;
   uint8_t slot;
   bool first;
   bool last;
};

struct hk_agx3_tile_instruction {
   uint8_t bytes[HK_AGX3_TILE_MAX_LENGTH];
   uint8_t length;
};

/*
 * Pack exactly one directly witnessed single-sample tile operation.  Only
 * G15G, G16G, and G17S are admitted.  Loads require a full RGBA component
 * mask and carry no incoming wait; stores accept at most one dependency wait.
 *
 * These helpers do not lower SPIR-V/NIR tile operations, append code to a
 * shader, configure render targets or MSAA state, submit work, admit an AGX3
 * device, or advertise any Vulkan feature.
 */
bool hk_agx3_pack_tile_store(enum agx_chip chip,
                             const struct hk_agx3_tile_store_desc *desc,
                             struct hk_agx3_tile_instruction *out);

bool hk_agx3_pack_tile_load(enum agx_chip chip,
                            const struct hk_agx3_tile_load_desc *desc,
                            struct hk_agx3_tile_instruction *out);

#ifdef __cplusplus
}
#endif
