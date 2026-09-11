/* SPDX-License-Identifier: MIT
 * Replay trusted development compiler captures, without opening a GPU. */
#include "agx_compile.h"
#include "agx_replay_abi.h"
#include "nir_serialize.h"
#include "glsl_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
   if (argc != 2) {
      fprintf(stderr, "Usage: %s BUNDLE/input.bin\n", argv[0]);
      return 2;
   }
   FILE *f = fopen(argv[1], "rb");
   if (!f) { perror(argv[1]); return 2; }
   fseek(f, 0, SEEK_END);
   long size = ftell(f);
   rewind(f);
   if (size < 128 || size > 8 * 1024 * 1024) return 2;
   void *data = malloc(size);
   if (!data || fread(data, 1, size, f) != size) return 2;
   fclose(f);
   struct blob_reader reader;
   blob_reader_init(&reader, data, size);
   const char *magic = blob_read_bytes(&reader, 12);
   const char *abi = blob_read_bytes(&reader, 65);
   if (memcmp(magic, "AGXREPLAYv1", 12) || memcmp(abi, AGX_REPLAY_ABI, 65) ||
       blob_read_uint32(&reader) != sizeof(struct agx_shader_key)) {
      fprintf(stderr, "Incompatible compiler capture ABI; rebuild from matching serialization sources\n");
      return 2;
   }
   struct agx_shader_key key;
   blob_copy_bytes(&reader, &key, sizeof(key));
   glsl_type_singleton_init_or_ref();
   nir_shader *nir = nir_deserialize(NULL, &agx_nir_options, &reader);
   if (!nir || reader.overrun || reader.current != reader.end) return 2;
   nir_print_shader(nir, stdout);
   struct agx_shader_part out = {0};
   bool ok = agx_compile_shader_nir(nir, &key, &out);
   fprintf(stderr, "Compiler replay: %s (%u bytes)\n", ok ? "PASS" : "REJECT", out.info.binary_size);
   free(out.binary);
   ralloc_free(nir);
   glsl_type_singleton_decref();
   free(data);
   return ok ? 0 : 1;
}
