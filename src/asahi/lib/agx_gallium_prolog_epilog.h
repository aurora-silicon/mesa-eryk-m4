/* SPDX-License-Identifier: MIT */
#pragma once
#include "agx_linker.h"
bool agx_gallium_nir_lower_non_monolithic_uniforms(nir_shader *nir, unsigned nr);
void agx_gallium_nir_vs_prolog(struct nir_builder *b, const void *key_);
void agx_gallium_nir_vs_prolog_raw(struct nir_builder *b, const void *key_);
bool agx_gallium_nir_gather_vs_inputs(nir_shader *s,
                              BITSET_WORD *attrib_components_read);
bool agx_gallium_nir_lower_vs_input_to_prolog(nir_shader *s);
void agx_gallium_nir_fs_epilog(struct nir_builder *b, const void *key_);
bool agx_gallium_nir_lower_fs_output_to_epilog(nir_shader *s,
                                       struct agx_fs_epilog_link_info *out);
bool agx_gallium_nir_lower_fs_output_to_epilog_g17p(
   nir_shader *s, struct agx_fs_epilog_link_info *out);
bool agx_gallium_nir_lower_fs_output_to_epilog_for_device(
   nir_shader *s, enum agx_chip chip, struct agx_fs_epilog_link_info *out);
bool agx_gallium_nir_pack_g17p_colour_exports(nir_shader *s,
                                      struct agx_fs_epilog_link_info *link);
void agx_gallium_nir_gather_fs_colour_exports(const nir_shader *s,
                                      uint8_t *loc_written, uint8_t *size_32,
                                      uint32_t *colour_comps);
bool agx_gallium_nir_lower_g17p_tile_samples(nir_shader *s, unsigned nr_samples);
bool agx_gallium_nir_lower_g17p_1x_tile_samples(nir_shader *s);
bool agx_gallium_nir_lower_g17p_1x_sample_shading(nir_shader *s);
bool agx_gallium_nir_lower_fs_active_samples_to_register(nir_shader *s);
void agx_gallium_nir_fs_prolog(struct nir_builder *b, const void *key_);
