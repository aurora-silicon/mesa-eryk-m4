/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_compile.h"
#include "nir_builder.h"

#include <stdio.h>
#include <string.h>

int
main(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned i = 0; i < sizeof(encodings) / sizeof(encodings[0]); ++i) {
      nir_builder b = nir_builder_init_simple_shader(
         MESA_SHADER_COMPUTE, &agx_nir_options, "unlowered ray query");
      b.shader->info.io_lowered = true;
      b.shader->info.ray_queries = 1;

      struct agx_shader_key key = {0};
      key.dev.encoding = encodings[i];
      struct agx_shader_part part;
      memset(&part, 0xa5, sizeof(part));

      if (agx_compile_shader_nir(b.shader, &key, &part)) {
         fprintf(stderr, "AGX3 unlowered ray query was accepted\n");
         ralloc_free(b.shader);
         return 1;
      }

      const uint8_t zero[sizeof(part)] = {0};
      if (memcmp(&part, zero, sizeof(part)) != 0) {
         fprintf(stderr, "AGX3 rejection did not clear compiler output\n");
         ralloc_free(b.shader);
         return 1;
      }

      ralloc_free(b.shader);
   }

   puts("agx3_compile_fail_closed: unlowered ray queries rejected");
   return 0;
}
