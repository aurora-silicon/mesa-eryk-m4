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

/* Pack only the native hardware PBE word.  Gen9/10 callers must still keep
 * writable-image execution gated until libagx's generation-specific software
 * addressing sideband is recovered; a correct hardware word is necessary but
 * not sufficient for that path.
 */
bool hk_pack_native_pbe(unsigned generation,
                        struct hk_native_pbe_descriptor *out,
                        const struct AGX_PBE *legacy);

bool hk_set_null_native_texture(unsigned generation,
                                struct hk_native_texture_descriptor *out);

/* Returns false and writes an all-zero sentinel for AGX3.  Honeykrisp's PBE
 * software sideband/decode is not recovered for Gen9/10 yet.
 */
bool hk_set_null_native_pbe(unsigned generation,
                            struct hk_native_pbe_descriptor *out);
