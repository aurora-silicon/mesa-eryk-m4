/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "d12_dxbc_wrap.h"

#include <stdlib.h>
#include <string.h>

#define D12_DXBC_MAGIC UINT32_C(0x43425844)
#define D12_DXBC_VERSION UINT32_C(1)
#define D12_DXBC_SIGNATURE_RECORD_SIZE 32u
#define D12_DXBC_HEADER_SIZE 32u
#define D12_DXBC_CHUNK_HEADER_SIZE 8u
#define D12_DXBC_CHUNK_COUNT 4u
#define D12_DXBC_MAX_CHUNK_COUNT 4096u

#define D12_FOURCC(a, b, c, d)                                              \
   ((uint32_t)(uint8_t)(a) | (uint32_t)(uint8_t)(b) << 8 |                 \
    (uint32_t)(uint8_t)(c) << 16 | (uint32_t)(uint8_t)(d) << 24)
#define D12_DXBC_CHUNK_SHDR D12_FOURCC('S', 'H', 'D', 'R')
#define D12_DXBC_CHUNK_SHEX D12_FOURCC('S', 'H', 'E', 'X')
#define D12_DXBC_CHUNK_DXIL D12_FOURCC('D', 'X', 'I', 'L')

/* Pinned d3d10TokenizedProgramFormat.hpp encodes the opcode in bits 10:0,
 * ordinary instruction length in bits 30:24, and gives CUSTOMDATA (opcode
 * 53) a full-DWORD length in its second token. Keep these values local so
 * the frontend-neutral validator remains buildable by host tests without a
 * WDK dependency. */
#define D12_TOKEN_OPCODE_MASK UINT32_C(0x7ff)
#define D12_TOKEN_OPCODE_CUSTOM_DATA UINT32_C(53)
#define D12_TOKEN_INSTRUCTION_LENGTH_SHIFT 24u
#define D12_TOKEN_INSTRUCTION_LENGTH_MASK UINT32_C(0x7f)

static void
d12_store_u32(uint8_t *destination, uint32_t value)
{
   memcpy(destination, &value, sizeof(value));
}

static uint32_t
d12_load_u32(const uint8_t *source)
{
   uint32_t value;
   memcpy(&value, source, sizeof(value));
   return value;
}

const char *
d12_dxbc_signature_semantic(uint32_t system_value)
{
   switch (system_value) {
   case 1:  return "SV_Position";
   case 2:  return "SV_ClipDistance";
   case 3:  return "SV_CullDistance";
   case 4:  return "SV_RenderTargetArrayIndex";
   case 5:  return "SV_ViewportArrayIndex";
   case 6:  return "SV_VertexID";
   case 7:  return "SV_PrimitiveID";
   case 8:  return "SV_InstanceID";
   case 9:  return "SV_IsFrontFace";
   case 10: return "SV_SampleIndex";
   case 11:
   case 12:
   case 13:
   case 14:
   case 17:
   case 18:
   case 19:
   case 21:
   case 22:
      return "SV_TessFactor";
   case 15:
   case 16:
   case 20:
      return "SV_InsideTessFactor";
   default:
      return "ATTR";
   }
}

static bool
d12_signature_valid(const struct d12_frontend_signature *signature)
{
   if (!signature || signature->count > D12_FRONTEND_MAX_SIGNATURE_ENTRIES ||
       (signature->count && !signature->entries))
      return false;

   for (uint32_t index = 0; index < signature->count; ++index) {
      const struct d12_frontend_signature_entry *entry =
         &signature->entries[index];
      if (entry->system_value > 22 || entry->register_index >= 256 ||
          entry->mask == 0 || entry->mask > 0xf ||
          entry->stream >= D12_FRONTEND_MAX_SIGNATURE_STREAMS ||
          entry->component_type > 3 ||
          (entry->min_precision != 0 && entry->min_precision != 1 &&
           entry->min_precision != 2 && entry->min_precision != 4 &&
           entry->min_precision != 5))
         return false;
   }
   return true;
}

bool
d12_dxbc_tokens_valid(const uint32_t *tokens, uint32_t expected_program_type)
{
   uint32_t cursor;
   uint32_t token_count;

   if (!tokens)
      return false;

   token_count = tokens[1];
   if (token_count < 2 || token_count > D12_FRONTEND_MAX_SHADER_TOKENS ||
       (expected_program_type != UINT32_MAX &&
        (tokens[0] >> 16) != expected_program_type))
      return false;

   /* Do not let a corrupt app token-count or instruction length reach the
    * shared DXBC parser/compiler. The DDI has no separate byte-count, so the
    * tokenized-program header is the only authoritative bound available to
    * a UMD. */
   cursor = 2;
   while (cursor < token_count) {
      const uint32_t opcode_token = tokens[cursor];
      const uint32_t opcode = opcode_token & D12_TOKEN_OPCODE_MASK;
      uint32_t instruction_length;

      if (opcode == D12_TOKEN_OPCODE_CUSTOM_DATA) {
         if (token_count - cursor < 2)
            return false;
         instruction_length = tokens[cursor + 1];
         if (instruction_length < 2)
            return false;
      } else {
         instruction_length =
            (opcode_token >> D12_TOKEN_INSTRUCTION_LENGTH_SHIFT) &
            D12_TOKEN_INSTRUCTION_LENGTH_MASK;
         if (!instruction_length)
            return false;
      }

      if (instruction_length > token_count - cursor)
         return false;
      cursor += instruction_length;
   }

   return cursor == token_count;
}

static bool
d12_dxbc_container_valid(const uint32_t *words,
                         uint32_t expected_program_type,
                         size_t *out_size)
{
   const uint8_t *container = (const uint8_t *)words;
   const uint32_t total_size = d12_load_u32(container + 24);
   const uint32_t chunk_count = d12_load_u32(container + 28);
   size_t header_size;
   bool found_shader = false;

   if (words[0] != D12_DXBC_MAGIC || d12_load_u32(container + 20) != 1 ||
       total_size < D12_DXBC_HEADER_SIZE ||
       total_size > D12_FRONTEND_MAX_SHADER_BYTES ||
       !chunk_count || chunk_count > D12_DXBC_MAX_CHUNK_COUNT)
      return false;

   header_size = D12_DXBC_HEADER_SIZE + (size_t)chunk_count * sizeof(uint32_t);
   if (header_size > total_size)
      return false;

   for (uint32_t index = 0; index < chunk_count; ++index) {
      const uint32_t offset =
         d12_load_u32(container + D12_DXBC_HEADER_SIZE + index * 4u);
      uint32_t fourcc;
      uint32_t payload_size;
      uint32_t program_type;

      if (offset < header_size ||
          offset > total_size - D12_DXBC_CHUNK_HEADER_SIZE)
         return false;
      fourcc = d12_load_u32(container + offset);
      payload_size = d12_load_u32(container + offset + 4u);
      if (payload_size >
          total_size - offset - D12_DXBC_CHUNK_HEADER_SIZE)
         return false;

      if (fourcc != D12_DXBC_CHUNK_SHDR && fourcc != D12_DXBC_CHUNK_SHEX &&
          fourcc != D12_DXBC_CHUNK_DXIL)
         continue;
      if (payload_size < sizeof(uint32_t))
         return false;

      /* Tokenized programs and DXIL program headers both encode shader kind
       * in the high 16 bits of their first DWORD. */
      program_type = d12_load_u32(container + offset + 8u) >> 16;
      if (expected_program_type != UINT32_MAX &&
          program_type != expected_program_type)
         return false;
      found_shader = true;
   }

   if (!found_shader)
      return false;
   if (out_size)
      *out_size = total_size;
   return true;
}

bool
d12_dxbc_shader_valid(const uint32_t *shader,
                      uint32_t expected_program_type,
                      size_t *out_container_size)
{
   if (out_container_size)
      *out_container_size = 0;
   if (!shader)
      return false;

   if (shader[0] == D12_DXBC_MAGIC)
      return d12_dxbc_container_valid(shader, expected_program_type,
                                      out_container_size);
   return d12_dxbc_tokens_valid(shader, expected_program_type);
}

static bool
d12_signature_payload_size(const struct d12_frontend_signature *signature,
                           size_t *out_size)
{
   size_t size = 8u +
                 (size_t)signature->count * D12_DXBC_SIGNATURE_RECORD_SIZE;

   for (uint32_t index = 0; index < signature->count; ++index) {
      const size_t name_size =
         strlen(d12_dxbc_signature_semantic(
                   signature->entries[index].system_value)) +
         1u;
      if (SIZE_MAX - size < name_size)
         return false;
      size += name_size;
   }
   *out_size = size;
   return true;
}

static void
d12_write_signature_chunk(
   uint8_t *chunk, const char fourcc[4],
   const struct d12_frontend_signature *signature, size_t payload_size)
{
   uint8_t *payload = chunk + D12_DXBC_CHUNK_HEADER_SIZE;
   size_t name_offset =
      8u + (size_t)signature->count * D12_DXBC_SIGNATURE_RECORD_SIZE;

   memcpy(chunk, fourcc, 4);
   d12_store_u32(chunk + 4, (uint32_t)payload_size);
   d12_store_u32(payload, signature->count);
   d12_store_u32(payload + 4, 8);

   for (uint32_t index = 0; index < signature->count; ++index) {
      const struct d12_frontend_signature_entry *entry =
         &signature->entries[index];
      const char *semantic = d12_dxbc_signature_semantic(entry->system_value);
      const size_t semantic_size = strlen(semantic) + 1u;
      uint8_t *record =
         payload + 8u + (size_t)index * D12_DXBC_SIGNATURE_RECORD_SIZE;

      /* SG1 starts with the geometry-output stream index.  Baseline DDI
       * callers translate the missing field to stream zero. */
      d12_store_u32(record, entry->stream);
      d12_store_u32(record + 4, (uint32_t)name_offset);
      /* The DDI supplies registers rather than application semantic names.
       * Use the register as the stable semantic index for generic linkage. */
      d12_store_u32(record + 8, entry->register_index);
      d12_store_u32(record + 12, entry->system_value);
      d12_store_u32(record + 16, entry->component_type);
      d12_store_u32(record + 20, entry->register_index);
      record[24] = (uint8_t)entry->mask;
      record[25] = (uint8_t)entry->mask;
      d12_store_u32(record + 28, entry->min_precision);
      memcpy(payload + name_offset, semantic, semantic_size);
      name_offset += semantic_size;
   }
}

bool
d12_dxbc_wrap_tokens(
   const uint32_t *tokens,
   const struct d12_frontend_shader_signatures *signatures,
   void **out_container, size_t *out_size)
{
   static const char signature_tags[3][4] = {
      {'I', 'S', 'G', '1'},
      {'O', 'S', 'G', '1'},
      {'P', 'S', 'G', '1'},
   };
   const struct d12_frontend_shader_signatures empty = {0};
   size_t payload_sizes[3];
   size_t header_size;
   size_t token_size;
   size_t total_size;
   size_t offset;
   uint8_t *container;

   if (!out_container || !out_size || !d12_dxbc_tokens_valid(tokens,
                                                               UINT32_MAX))
      return false;
   *out_container = NULL;
   *out_size = 0;
   if (!signatures)
      signatures = &empty;
   if (!d12_signature_valid(&signatures->input) ||
       !d12_signature_valid(&signatures->output) ||
       !d12_signature_valid(&signatures->patch) ||
       !d12_signature_payload_size(&signatures->input, &payload_sizes[0]) ||
       !d12_signature_payload_size(&signatures->output, &payload_sizes[1]) ||
       !d12_signature_payload_size(&signatures->patch, &payload_sizes[2]))
      return false;

   header_size = D12_DXBC_HEADER_SIZE + D12_DXBC_CHUNK_COUNT * 4u;
   token_size = (size_t)tokens[1] * sizeof(*tokens);
   total_size = header_size + D12_DXBC_CHUNK_HEADER_SIZE + token_size;
   for (uint32_t index = 0; index < 3; ++index) {
      if (SIZE_MAX - total_size <
          D12_DXBC_CHUNK_HEADER_SIZE + payload_sizes[index])
         return false;
      total_size += D12_DXBC_CHUNK_HEADER_SIZE + payload_sizes[index];
   }
   if (total_size > UINT32_MAX)
      return false;

   container = calloc(1, total_size);
   if (!container)
      return false;

   d12_store_u32(container, D12_DXBC_MAGIC);
   d12_store_u32(container + 20, D12_DXBC_VERSION);
   d12_store_u32(container + 24, (uint32_t)total_size);
   d12_store_u32(container + 28, D12_DXBC_CHUNK_COUNT);

   offset = header_size;
   for (uint32_t index = 0; index < 3; ++index) {
      const struct d12_frontend_signature *signature =
         index == 0 ? &signatures->input
                    : (index == 1 ? &signatures->output
                                  : &signatures->patch);
      d12_store_u32(container + D12_DXBC_HEADER_SIZE + index * 4u,
                    (uint32_t)offset);
      d12_write_signature_chunk(container + offset, signature_tags[index],
                                signature, payload_sizes[index]);
      offset += D12_DXBC_CHUNK_HEADER_SIZE + payload_sizes[index];
   }

   d12_store_u32(container + D12_DXBC_HEADER_SIZE + 3u * 4u,
                 (uint32_t)offset);
   memcpy(container + offset, "SHEX", 4);
   d12_store_u32(container + offset + 4, (uint32_t)token_size);
   memcpy(container + offset + D12_DXBC_CHUNK_HEADER_SIZE, tokens,
          token_size);

   *out_container = container;
   *out_size = total_size;
   return true;
}

bool
d12_dxbc_import_shader(
   const uint32_t *shader, uint32_t expected_program_type,
   const struct d12_frontend_shader_signatures *signatures,
   void **out_container, size_t *out_size)
{
   size_t container_size = 0;
   void *copy;

   if (!out_container || !out_size)
      return false;
   *out_container = NULL;
   *out_size = 0;
   if (!d12_dxbc_shader_valid(shader, expected_program_type,
                              &container_size))
      return false;

   if (!container_size)
      return d12_dxbc_wrap_tokens(shader, signatures, out_container,
                                  out_size);

   copy = malloc(container_size);
   if (!copy)
      return false;
   memcpy(copy, shader, container_size);
   *out_container = copy;
   *out_size = container_size;
   return true;
}
