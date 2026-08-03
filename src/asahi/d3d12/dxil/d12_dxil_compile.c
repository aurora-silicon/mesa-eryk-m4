/*
 * Copyright 2026 NTASP contributors
 * SPDX-License-Identifier: MIT
 *
 * G4c acceptance driver: DXIL in, AGX machine code out, with the whole
 * path visible — bitstream, module, NIR, and the final disassembly. No
 * SPIR-V, no dxc, no external compiler is involved at run time.
 */

#include "d12_dxil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agx_compile.h"
#include "agx_nir.h"
#include "agx_tilebuffer.h"
#include "disasm.h"
#include "nir.h"

static const char *
stage_name(mesa_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX: return "vertex";
   case MESA_SHADER_FRAGMENT: return "fragment";
   case MESA_SHADER_COMPUTE: return "compute";
   default: return "?";
   }
}

/*
 * The shader kind lives in the DXBC container's DXIL part header (u16 at
 * offset 2) and mirrors DXC's -T profile. Map it to a Mesa stage.
 */
static bool
stage_from_option(const char *opt, mesa_shader_stage *out)
{
   if (!strcmp(opt, "ps") || !strcmp(opt, "fragment")) {
      *out = MESA_SHADER_FRAGMENT;
      return true;
   }
   if (!strcmp(opt, "vs") || !strcmp(opt, "vertex")) {
      *out = MESA_SHADER_VERTEX;
      return true;
   }
   if (!strcmp(opt, "cs") || !strcmp(opt, "compute")) {
      *out = MESA_SHADER_COMPUTE;
      return true;
   }
   return false;
}

int
main(int argc, char **argv)
{
   mesa_shader_stage stage = MESA_SHADER_FRAGMENT;
   bool stage_forced = false;
   const char *path = NULL;
   bool print_nir = false;

   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "-s") && i + 1 < argc) {
         if (!stage_from_option(argv[++i], &stage)) {
            fprintf(stderr, "unknown stage %s\n", argv[i]);
            return 2;
         }
         stage_forced = true;
      } else if (!strcmp(argv[i], "-n")) {
         print_nir = true;
      } else {
         path = argv[i];
      }
   }

   if (!path) {
      fprintf(stderr, "usage: %s [-s ps|vs|cs] [-n] shader.dxil\n", argv[0]);
      return 2;
   }

   FILE *f = fopen(path, "rb");
   if (!f) {
      fprintf(stderr, "cannot open %s\n", path);
      return 2;
   }

   fseek(f, 0, SEEK_END);
   long size = ftell(f);
   fseek(f, 0, SEEK_SET);

   uint8_t *data = malloc(size);
   if (!data || fread(data, 1, size, f) != (size_t)size) {
      fprintf(stderr, "cannot read %s\n", path);
      return 2;
   }
   fclose(f);

   /* 1. container -> bitcode (and the stage it declares) */
   const uint8_t *bc;
   size_t bc_size;
   const char *why = NULL;
   if (!d12_dxil_find_bitcode(data, size, &bc, &bc_size, &why)) {
      fprintf(stderr, "%s: %s\n", path, why);
      return 1;
   }

   mesa_shader_stage container_stage;
   if (!stage_forced && d12_dxil_container_stage(data, size, &container_stage))
      stage = container_stage;

   /* 2. bitcode -> module */
   struct d12_module mod;
   if (!d12_dxil_module_parse(&mod, bc, bc_size)) {
      fprintf(stderr, "module parse failed: %s\n",
              mod.error ? mod.error : "unknown");
      return 1;
   }

   printf("%s: %s shader, %u types, %u values, %u functions\n", path,
          stage_name(stage), mod.num_types, mod.num_values,
          mod.num_functions);

   /* 3. module -> NIR (no SPIR-V anywhere in here) */
   const char *error = NULL;
   nir_shader *nir = d12_dxil_to_nir(&mod, stage, &error);
   if (!nir) {
      fprintf(stderr, "DXIL->NIR failed: %s\n", error);
      d12_dxil_module_finish(&mod);
      return 1;
   }

   printf("DXIL->NIR OK\n");
   if (print_nir)
      nir_print_shader(nir, stdout);

   /* 4. Lowering, then NIR -> AGX.
    *
    * The AGX backend does not consume store_output for fragment shaders
    * directly: colour writes become tile-memory stores, which is what
    * agx_nir_lower_tilebuffer does (the same pass Honeykrisp and the
    * background/end-of-tile builders run). Skipping this is exactly the
    * failure mode where the backend reports "Unhandled intrinsic".
    */
   NIR_PASS(_, nir, agx_nir_lower_texture);
   agx_preprocess_nir(nir);

   struct agx_tilebuffer_layout tib = {0};
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      enum pipe_format formats[8] = {PIPE_FORMAT_R8G8B8A8_UNORM};
      tib = agx_build_tilebuffer_layout(formats, 1, 1, false);

      unsigned bindless_base = 0;
      NIR_PASS(_, nir, agx_nir_lower_tilebuffer, &tib, NULL, &bindless_base,
               NULL, NULL);
      NIR_PASS(_, nir, agx_nir_lower_monolithic_msaa, 1);
      NIR_PASS(_, nir, agx_nir_lower_multisampled_image_store);
      NIR_PASS(_, nir, agx_nir_lower_texture);
   }

   struct agx_shader_key key = {
      .no_stop = false,
      .promote_constants = true,
   };

   struct agx_shader_part part;
   memset(&part, 0, sizeof(part));
   agx_compile_shader_nir(nir, &key, &part);

   if (!part.binary || !part.info.binary_size) {
      fprintf(stderr, "AGX compile produced no code\n");
      d12_dxil_module_finish(&mod);
      return 1;
   }

   printf("NIR->AGX OK: %u bytes total, %u GPRs, main at 0x%x%s\n",
          part.info.binary_size, part.info.nr_gprs, part.info.main_offset,
          part.info.has_preamble ? " (has preamble)" : "");

   /* The binary is rodata + optional preamble + main; disassemble the code
    * sections rather than decoding the constant pool as instructions.
    */
   if (part.info.has_preamble) {
      printf("\n; AGX preamble (offset 0x%x)\n", part.info.preamble_offset);
      agx2_disassemble(part.binary + part.info.preamble_offset,
                       part.info.main_offset > part.info.preamble_offset
                          ? part.info.main_offset - part.info.preamble_offset
                          : part.info.binary_size - part.info.preamble_offset,
                       stdout);
   }

   printf("\n; AGX main (offset 0x%x) — from %s\n", part.info.main_offset,
          path);
   agx2_disassemble(part.binary + part.info.main_offset,
                    part.info.binary_size - part.info.main_offset, stdout);

   free(part.binary);
   d12_dxil_module_finish(&mod);
   free(data);
   return 0;
}
