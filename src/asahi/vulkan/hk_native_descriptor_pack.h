/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "agx_pack.h"
#include "hk_native_descriptor.h"

struct hk_native_texture_descriptor {
   uint32_t opaque[HK_TEXTURE_DESCRIPTOR_AGX3_SIZE / 4];
};

struct hk_native_pbe_descriptor {
   uint32_t opaque[HK_PBE_DESCRIPTOR_AGX3_SIZE / 4];
};

bool hk_pack_native_texture(unsigned generation,
                            struct hk_native_texture_descriptor *out,
                            const struct AGX_TEXTURE *legacy);

/* Translate a multisample count only when the native sampled-texture format is
 * recovered for the descriptor generation.  There is no one-sample field in
 * the descriptor, so samples == 1 deliberately returns false.
 */
bool hk_translate_native_texture_sample_count(
   unsigned generation, unsigned samples, enum agx_sample_count *out);

/* Pack the native hardware PBE word. Gen9/10 accept only the exact uncompressed
 * single-level/single-layer 2D subset; software-addressed axes remain gated.
 */
bool hk_pack_native_pbe(unsigned generation,
                        struct hk_native_pbe_descriptor *out,
                        const struct AGX_PBE *legacy);

bool hk_set_null_native_texture(unsigned generation,
                                struct hk_native_texture_descriptor *out);

/* Construct the generation-native robust null PBE descriptor. */
bool hk_set_null_native_pbe(unsigned generation,
                            struct hk_native_pbe_descriptor *out);
