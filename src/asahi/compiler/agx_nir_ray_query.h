/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Exact, fail-closed NIR boundary for Apple's inline ray-query ABI.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "compiler/nir/nir.h"
#include "agx3_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

enum agx_nir_ray_query_result_kind {
   AGX_NIR_RAY_QUERY_RESULT_U32 = 0,
   AGX_NIR_RAY_QUERY_RESULT_F32,
   AGX_NIR_RAY_QUERY_RESULT_BOOL,
};

struct agx_nir_ray_query_result_layout {
   uint16_t byte_offset;
   uint8_t components;
   uint8_t storage_bit_size;
   enum agx_nir_ray_query_result_kind kind;
};

/*
 * This is a prerequisite contract, not a traversal lowering.
 *
 * The pinned Apple runtime IR establishes the query-state size and the named
 * committed-result fields below.  Independent native machine-code corpora
 * establish one intersect_ray differential pair and fixed first-site
 * continue_ray/incStk witnesses.  A chip is admitted only when all three
 * witnesses agree on one AGX3 encoding and the requested NIR rq_load semantic
 * has an exact committed-result field.
 *
 * The returned instruction lengths describe those isolated witnesses only;
 * they are not a sequence to append.  The API deliberately does not represent
 * candidate loads, query initialization, SCS placement, barriers, control
 * flow, scheduling, acceleration-structure construction, submission, or an
 * API capability.  Failure zeroes out, making speculative callers
 * transactional.
 */
struct agx_nir_ray_query_committed_prerequisites {
   enum agx_isa_encoding encoding;
   uint16_t query_state_size;
   uint8_t intersect_ray_witness_size;
   uint8_t continue_ray_first_site_witness_size;
   uint8_t inc_stk_witness_size;
   struct agx_nir_ray_query_result_layout result;
};

bool agx_nir_ray_query_committed_load_prerequisites(
   enum agx_chip chip, const nir_intrinsic_instr *load,
   struct agx_nir_ray_query_committed_prerequisites *out);

#ifdef __cplusplus
}
#endif
