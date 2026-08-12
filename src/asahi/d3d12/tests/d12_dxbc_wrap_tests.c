/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_dxbc_wrap.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t
load_u32(const uint8_t *source)
{
   uint32_t value;
   memcpy(&value, source, sizeof(value));
   return value;
}

int
main(int argc, char **argv)
{
   const uint32_t tokens[] = {
      UINT32_C(1) << 16 | UINT32_C(5) << 4,
      3,
      UINT32_C(1) << 24,
   };
   const struct d12_frontend_signature_entry input[] = {
      {.system_value = 1,
       .register_index = 0,
       .mask = 0xf,
       .component_type = 3},
      {.system_value = 0,
       .register_index = 3,
       .mask = 3,
       .component_type = 3,
       .min_precision = 1},
   };
   const struct d12_frontend_shader_signatures signatures = {
      .input = {.count = 2, .entries = input},
   };
   uint8_t *container = NULL;
   size_t size = 0;

   assert(d12_dxbc_tokens_valid(tokens, 1));
   assert(!d12_dxbc_tokens_valid(tokens, 5));
   assert(!strcmp(d12_dxbc_signature_semantic(1), "SV_Position"));
   assert(!strcmp(d12_dxbc_signature_semantic(0), "ATTR"));
   assert(d12_dxbc_wrap_tokens(tokens, &signatures,
                               (void **)&container, &size));
   assert(container != NULL && size > 64);
   assert(load_u32(container) == UINT32_C(0x43425844));
   assert(load_u32(container + 24) == size);
   assert(load_u32(container + 28) == 4);

   const uint32_t input_offset = load_u32(container + 32);
   assert(input_offset < size && !memcmp(container + input_offset, "ISG1", 4));
   const uint8_t *input_payload = container + input_offset + 8;
   assert(load_u32(input_payload) == 2);
   const uint32_t first_name = load_u32(input_payload + 8 + 4);
   const uint32_t second_name = load_u32(input_payload + 8 + 32 + 4);
   assert(!strcmp((const char *)input_payload + first_name, "SV_Position"));
   assert(!strcmp((const char *)input_payload + second_name, "ATTR"));
   assert(load_u32(input_payload + 8 + 32 + 28) == 1);

   const uint32_t shader_offset = load_u32(container + 44);
   assert(shader_offset < size && !memcmp(container + shader_offset, "SHEX", 4));
   assert(load_u32(container + shader_offset + 4) == sizeof(tokens));
   assert(!memcmp(container + shader_offset + 8, tokens, sizeof(tokens)));

   {
      uint8_t *imported = NULL;
      size_t imported_size = 0;
      assert(d12_dxbc_shader_valid((const uint32_t *)container, 1,
                                   &imported_size));
      assert(imported_size == size);
      assert(!d12_dxbc_shader_valid((const uint32_t *)container, 5, NULL));
      assert(d12_dxbc_import_shader((const uint32_t *)container, 1,
                                    &signatures, (void **)&imported,
                                    &imported_size));
      assert(imported_size == size && imported != container);
      assert(!memcmp(imported, container, size));
      free(imported);
   }
   free(container);

   {
      /* A minimal, structurally valid DXIL container. Program kind 5 is
       * compute, encoded in the high half of the DXIL program version. */
      uint32_t dxil[] = {
         UINT32_C(0x43425844), 0, 0, 0, 0, 1, 48, 1, 36,
         UINT32_C(0x4c495844), 4, UINT32_C(5) << 16,
      };
      uint8_t *imported = NULL;
      size_t imported_size = 0;
      assert(d12_dxbc_shader_valid(dxil, 5, &imported_size));
      assert(imported_size == sizeof(dxil));
      assert(!d12_dxbc_shader_valid(dxil, 1, NULL));
      assert(d12_dxbc_import_shader(dxil, 5, NULL, (void **)&imported,
                                    &imported_size));
      assert(imported_size == sizeof(dxil));
      assert(!memcmp(imported, dxil, sizeof(dxil)));
      free(imported);

      dxil[10] = 8;
      assert(!d12_dxbc_shader_valid(dxil, 5, NULL));
      dxil[10] = 4;
      dxil[8] = 38;
      assert(!d12_dxbc_shader_valid(dxil, 5, NULL));
   }

   if (argc > 1) {
      FILE *file = fopen(argv[1], "rb");
      long file_size;
      uint8_t *file_data;
      size_t container_size = 0;

      assert(file);
      assert(fseek(file, 0, SEEK_END) == 0);
      file_size = ftell(file);
      assert(file_size > 0 &&
             (unsigned long)file_size <= D12_FRONTEND_MAX_SHADER_BYTES);
      assert(fseek(file, 0, SEEK_SET) == 0);
      file_data = malloc((size_t)file_size);
      assert(file_data);
      assert(fread(file_data, 1, (size_t)file_size, file) ==
             (size_t)file_size);
      assert(fclose(file) == 0);
      assert(d12_dxbc_shader_valid((const uint32_t *)file_data, 5,
                                   &container_size));
      assert(container_size == (size_t)file_size);
      free(file_data);
   }

   {
      uint32_t invalid_tokens[] = {tokens[0], 1};
      assert(!d12_dxbc_wrap_tokens(invalid_tokens, NULL,
                                   (void **)&container, &size));
   }
   {
      const uint32_t zero_length[] = {tokens[0], 3, 0};
      const uint32_t overrun[] = {tokens[0], 3, UINT32_C(2) << 24};
      const uint32_t truncated_custom[] = {
         tokens[0], 3, UINT32_C(53),
      };
      const uint32_t short_custom[] = {
         tokens[0], 4, UINT32_C(53), 1,
      };
      const uint32_t overrun_custom[] = {
         tokens[0], 4, UINT32_C(53), 3,
      };
      const uint32_t valid_custom[] = {
         tokens[0], 5, UINT32_C(53), 3, UINT32_C(0x12345678),
      };

      assert(!d12_dxbc_tokens_valid(zero_length, 1));
      assert(!d12_dxbc_tokens_valid(overrun, 1));
      assert(!d12_dxbc_tokens_valid(truncated_custom, 1));
      assert(!d12_dxbc_tokens_valid(short_custom, 1));
      assert(!d12_dxbc_tokens_valid(overrun_custom, 1));
      assert(d12_dxbc_tokens_valid(valid_custom, 1));
   }
   {
      struct d12_frontend_signature_entry invalid = input[0];
      invalid.mask = 0;
      const struct d12_frontend_shader_signatures invalid_signatures = {
         .input = {.count = 1, .entries = &invalid},
      };
      assert(!d12_dxbc_wrap_tokens(tokens, &invalid_signatures,
                                   (void **)&container, &size));
   }

   puts("d12_dxbc_wrap_tests: PASS");
   return 0;
}
