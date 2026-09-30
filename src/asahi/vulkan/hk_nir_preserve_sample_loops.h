/* SPDX-License-Identifier: MIT */
#pragma once
#include "nir.h"
bool hk_nir_preserve_sample_loops(nir_shader *nir);
bool hk_nir_release_short_sample_loops(nir_shader *nir);
