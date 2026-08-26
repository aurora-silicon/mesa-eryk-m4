/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx_compile.h"
#include "nir_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
check_empty_compute(enum agx_isa_encoding encoding)
{
   static const uint8_t expected[] = {0x0e, 0x00, 0x00, 0x00};

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "empty AGX3 compute");
   b.shader->info.io_lowered = true;
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;

   struct agx_shader_key key = {0};
   key.dev.encoding = encoding;
   struct agx_shader_part part = {0};

   if (!agx_compile_shader_nir(b.shader, &key, &part)) {
      fprintf(stderr, "empty AGX3 compute shader was rejected\n");
      ralloc_free(b.shader);
      return false;
   }

   const bool valid = part.binary != NULL &&
                      part.info.binary_size == sizeof(expected) &&
                      part.info.main_offset == 0 &&
                      part.info.main_size == sizeof(expected) &&
                      part.info.stats.instrs == 1 &&
                      part.info.workgroup_size[0] == 1 &&
                      part.info.workgroup_size[1] == 1 &&
                      part.info.workgroup_size[2] == 1 &&
                      memcmp(part.binary, expected, sizeof(expected)) == 0;

   if (!valid)
      fprintf(stderr, "empty AGX3 compute shader metadata/binary mismatch\n");

   free(part.binary);
   ralloc_free(b.shader);
   return valid;
}

static bool
check_shared_store_compute(enum agx_isa_encoding encoding)
{
   static const uint8_t end[] = {0x0e, 0x00, 0x00, 0x00};

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "AGX3 shared store");
   b.shader->info.io_lowered = true;
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.shared_size = 4;
   nir_store_shared(&b, nir_imm_int(&b, 0x12345678), nir_imm_int(&b, 0),
                    .align_mul = 4);

   struct agx_shader_key key = {0};
   key.dev.encoding = encoding;
   struct agx_shader_part part = {0};

   if (!agx_compile_shader_nir(b.shader, &key, &part)) {
      fprintf(stderr, "AGX3 shared-store compute shader was rejected\n");
      ralloc_free(b.shader);
      return false;
   }

   const bool valid =
      part.binary != NULL && part.info.binary_size > sizeof(end) &&
      part.info.main_offset == 0 &&
      part.info.main_size == part.info.binary_size &&
      part.info.stats.instrs > 1 && part.info.local_size == 4 &&
      memcmp((const uint8_t *)part.binary + part.info.binary_size - sizeof(end),
             end, sizeof(end)) == 0;

   if (!valid)
      fprintf(stderr, "AGX3 shared-store metadata/binary mismatch\n");

   free(part.binary);
   ralloc_free(b.shader);
   return valid;
}

int
main(void)
{
   const enum agx_isa_encoding encodings[] = {
      AGX_ISA_ENCODING_AGX3B,
      AGX_ISA_ENCODING_AGX3C,
   };

   for (unsigned i = 0; i < sizeof(encodings) / sizeof(encodings[0]); ++i) {
      if (!check_empty_compute(encodings[i]))
         return 1;
      if (!check_shared_store_compute(encodings[i]))
         return 1;

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

   puts("agx3_compile: empty/shared-store compute emitted; unlowered ray queries rejected");
   return 0;
}
