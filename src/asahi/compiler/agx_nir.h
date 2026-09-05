/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct nir_shader;
struct nir_instr;

/* BASE bit3 selects an explicit byte-scaled AGX3 load/store index. */
#define AGX_NIR_MEMORY_INDEX_BYTES 8u
/* BASE bits4..6 carry the native packed element count (1..4). */
#define AGX_NIR_MEMORY_PACKED_COUNT_SHIFT 4u
#define AGX_NIR_MEMORY_PACKED_COUNT_MASK (7u << AGX_NIR_MEMORY_PACKED_COUNT_SHIFT)
bool agx_nir_lower_address(struct nir_shader *shader, bool byte_indices);
bool agx_nir_lower_algebraic_late(struct nir_shader *shader);
bool agx_nir_cleanup_amul(struct nir_shader *shader);
bool agx_nir_fuse_lea(struct nir_shader *shader);
bool agx_nir_lower_lea(struct nir_shader *shader);
bool agx_nir_fuse_selects(struct nir_shader *shader);
bool agx_nir_fuse_algebraic_late(struct nir_shader *shader);
bool agx_nir_fence_images(struct nir_shader *shader);
bool agx_nir_lower_layer(struct nir_shader *s);
bool agx_nir_lower_clip_distance(struct nir_shader *s);
bool agx_nir_lower_subgroups(struct nir_shader *s);
bool agx_nir_lower_fminmax(struct nir_shader *s);

bool agx_nir_lower_texture_early(struct nir_shader *s, bool support_lod_bias);
bool agx_nir_lower_texture(struct nir_shader *s);
bool agx_nir_lower_multisampled_image_store(struct nir_shader *s);
bool agx_nir_needs_texture_crawl(struct nir_instr *instr);

/* Existing reserved software tail, relative to a Gen9 sampled texture. */
#define AGX_G17P_SAMPLED_HEAP_INDEX_OFFSET 68

bool agx_nir_lower_g17p_indexed_samplers(struct nir_shader *s);

struct agx_g17p_combined_sampler_binding {
   uint32_t texture_offset_B;
   uint32_t sampler_index_offset_B;
};

/* Native compute texture/sampler table pointers occupy halves0..7. */
#define AGX_G17P_TXF_ROOT_HALFS 8u
struct agx_g17p_txf_binding {
   uint32_t texture_offset_B;
};

/* Turn the one statically indexed set-0 Gen9 combined image/sampler used by
 * the initial G17P graphics path into texture-state 0 / sampler-state 1. The
 * caller must place the corresponding native states in those CProg regions.
 */
bool agx_nir_lower_g17p_combined_sampler(
   struct nir_shader *s, struct agx_g17p_combined_sampler_binding *binding);

/* Turn the one statically indexed set-0 Gen9 texture used by the initial
 * G17P compute texel-fetch path into texture-state 0.  Sampler-state 0 remains
 * the device TXF sampler.  The caller must publish both native states through
 * the G17P resource program; no executable shader preamble is generated.
 */
bool agx_nir_lower_g17p_txf(
   struct nir_shader *s, struct agx_g17p_txf_binding *binding);
