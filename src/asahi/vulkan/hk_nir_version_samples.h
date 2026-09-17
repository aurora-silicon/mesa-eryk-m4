/* SPDX-License-Identifier: MIT */
#pragma once
#include "nir.h"

nir_variable *hk_nir_sample_version_candidate(nir_shader *nir);
bool hk_nir_version_samples(nir_shader *nir, nir_variable *sampler, bool view_min);
