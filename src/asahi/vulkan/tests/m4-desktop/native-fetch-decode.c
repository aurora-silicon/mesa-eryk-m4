/* Offline candidate decoder for native AGX3b texture-fetch witnesses.
 * Every match is re-encoded byte-for-byte by agx3_tex_decode_fetch.
 * Scanning is not proof of instruction boundaries or runtime semantics.
 */
#include "asahi/compiler/agx3_tex_core.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
   if (argc != 2) {
      fprintf(stderr, "Usage: %s TEXT.bin\n", argv[0]);
      return 2;
   }
   FILE *f = fopen(argv[1], "rb");
   if (!f) { perror(argv[1]); return 2; }
   if (fseek(f, 0, SEEK_END)) return 2;
   long size = ftell(f);
   if (size < 0 || size > 8 * 1024 * 1024) return 2;
   rewind(f);
   unsigned char *bytes = malloc(size ? size : 1);
   if (!bytes || fread(bytes, 1, size, f) != (size_t)size) return 2;
   fclose(f);
   for (long offset = 0; offset < size; offset += 2) {
      for (unsigned length = 14; length <= 20; length += 6) {
         struct agx3_tex_fetch_desc d;
         if (offset + length > size ||
             !agx3_tex_decode_fetch(AGX_ISA_ENCODING_AGX3B, bytes + offset,
                                   length, &d))
            continue;
         printf("offset=%#lx size=%u dest=%u coord=%u width=%u mask=%u "
                "texture=%u kind=%u value=%u kill=%u slot=%u wait=%#x "
                "coord_kill=%u final=%u array=%u ms=%u 3d=%u lod=%u bytes=",
                offset, length, d.dest, d.coord, d.width, d.mask, d.texture,
                d.texture_kind, d.texture_value, d.texture_kill, d.slot,
                d.wait_mask, d.coord_kill, d.final, d.array, d.multisample,
                d.three_d, d.register_lod);
         for (unsigned j = 0; j < length; ++j) printf("%02x", bytes[offset+j]);
         puts("");
      }
   }
   free(bytes);
   return 0;
}
