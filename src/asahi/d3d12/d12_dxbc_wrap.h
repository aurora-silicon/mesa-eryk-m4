/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Build the minimal DXBC container required by the shared SM4/SM5 compiler
 * from the raw token stream and explicit stage signatures supplied by the
 * D3D12 WDDM DDI.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D12_FRONTEND_MAX_SIGNATURE_ENTRIES 256u
#define D12_FRONTEND_MAX_SIGNATURE_STREAMS 4u
#define D12_FRONTEND_MAX_SHADER_TOKENS (4u * 1024u * 1024u)
#define D12_FRONTEND_MAX_SHADER_BYTES (64u * 1024u * 1024u)

struct d12_frontend_signature_entry {
   uint32_t system_value;
   uint32_t register_index;
   uint32_t mask;
   uint32_t stream;
   uint32_t component_type;
   uint32_t min_precision;
};

struct d12_frontend_signature {
   uint32_t count;
   const struct d12_frontend_signature_entry *entries;
};

struct d12_frontend_shader_signatures {
   struct d12_frontend_signature input;
   struct d12_frontend_signature output;
   struct d12_frontend_signature patch;
};

bool d12_dxbc_tokens_valid(const uint32_t *tokens,
                           uint32_t expected_program_type);
/* D3D12 Release 1 may pass either legacy tokenized shader code or a complete
 * DXBC container carrying DXIL.  The DDI has no explicit byte count, so the
 * appropriate self-describing header is validated before any copy or parser
 * access. */
bool d12_dxbc_shader_valid(const uint32_t *shader,
                           uint32_t expected_program_type,
                           size_t *out_container_size);
const char *d12_dxbc_signature_semantic(uint32_t system_value);
bool d12_dxbc_wrap_tokens(
   const uint32_t *tokens,
   const struct d12_frontend_shader_signatures *signatures,
   void **out_container, size_t *out_size);
bool d12_dxbc_import_shader(
   const uint32_t *shader, uint32_t expected_program_type,
   const struct d12_frontend_shader_signatures *signatures,
   void **out_container, size_t *out_size);
