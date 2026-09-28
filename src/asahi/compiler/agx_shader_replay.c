#include "util/hash_table.h"
/* SPDX-License-Identifier: MIT
 * Replay trusted development compiler captures, without opening a GPU. */
#include "agx_compile.h"
#include "agx_nir.h"
#include "agx_nir_texture.h"
#include "agx_replay_abi.h"
#include "nir_serialize.h"
#include "nir_builder.h"
#include "glsl_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
   if (argc != 2 && !(argc == 3 && (!strcmp(argv[2], "--check-native-resources") ||
                                  !strcmp(argv[2], "--native-preamble") ||
                                  !strcmp(argv[2], "--gcm") ||
                                  !strcmp(argv[2], "--licm") ||
                                  !strcmp(argv[2], "--gcm-no-vn") ||
                                  !strcmp(argv[2], "--print-nir") ||
                                  !strcmp(argv[2], "--lower-texture-gen9") ||
                                  !strncmp(argv[2], "--binary-output=", 16) ||
                                  !strncmp(argv[2], "--uniform-one=", 14)))) {
      fprintf(stderr, "Usage: %s BUNDLE/input.bin [--check-native-resources|--native-preamble|--gcm|--licm|--gcm-no-vn|--uniform-one=HALF|--binary-output=FILE]\n       %s FRONTEND.nirbin [--print-nir|--lower-texture-gen9]\n", argv[0], argv[0]);
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
   if (argc == 3 && (!strcmp(argv[2], "--print-nir") ||
                     !strcmp(argv[2], "--lower-texture-gen9"))) {
      glsl_type_singleton_init_or_ref();
      nir_shader *nir = nir_deserialize(NULL, &agx_nir_options, &reader);
      if (!nir || reader.overrun || reader.current != reader.end) return 2;
      nir_validate_shader(nir, "retained frontend NIR");
      if (!strcmp(argv[2], "--lower-texture-gen9")) {
         NIR_PASS(_, nir, agx_nir_lower_texture_for_generation, 9);
         nir_validate_shader(nir, "after replay texture lowering");
         const char *output = getenv("AGX_REPLAY_NIR_OUTPUT");
         if (output) {
            nir_foreach_function_impl(impl, nir)
               nir_index_ssa_defs(impl);
            FILE *dump = fopen(output, "w");
            if (!dump) { perror(output); return 2; }
            nir_print_shader(nir, dump);
            fclose(dump);
         }
         puts("TEXTURE_LOWERING_COMPLETE");
      } else {
         nir_print_shader(nir, stdout);
      }
      ralloc_free(nir);
      glsl_type_singleton_decref();
      free(data);
      return 0;
   }
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
   /* Explicit diagnostic transforms replace the driver's default motion
    * pass rather than applying it twice. */
   if (argc == 3 && strncmp(argv[2], "--uniform-one=", 14) &&
       strncmp(argv[2], "--binary-output=", 16) &&
       strcmp(argv[2], "--native-preamble"))
      setenv("AGX_CODE_MOTION", "none", 1);
   if (argc == 3 && (!strcmp(argv[2], "--gcm") || !strcmp(argv[2], "--gcm-no-vn"))) {
      bool progress = nir_opt_gcm(nir, !strcmp(argv[2], "--gcm"));
      fprintf(stderr, "Global code motion: %s\n", progress ? "changed" : "unchanged");
      nir_validate_shader(nir, "after replay GCM");
   }
   if (argc == 3 && !strcmp(argv[2], "--licm")) {
      bool progress = nir_opt_licm(nir);
      nir_opt_cse(nir);
      nir_opt_dce(nir);
      fprintf(stderr, "Loop invariant code motion: %s\n", progress ? "changed" : "unchanged");
      nir_validate_shader(nir, "after replay LICM");
   }
   if (argc == 3 && !strncmp(argv[2], "--uniform-one=", 14)) {
      char *end;
      unsigned long half = strtoul(argv[2] + 14, &end, 10);
      if (*end || half >= 512 || (half & 1)) return 2;
      unsigned count = 0;
      nir_foreach_function_impl(impl, nir) {
         nir_foreach_block(block, impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic) continue;
               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
               if (intr->intrinsic != nir_intrinsic_load_preamble ||
                   nir_intrinsic_base(intr) != half ||
                   intr->def.bit_size != 32 || intr->def.num_components != 1)
                  continue;
               nir_builder b = nir_builder_at(nir_before_instr(instr));
               nir_def_replace(&intr->def, nir_imm_int(&b, 1));
               count++;
            }
         }
      }
      fprintf(stderr, "DIAGNOSTIC: force uniform half %lu to one (%u loads); not an equivalent shader\n", half, count);
      if (!count) return 2;
      bool progress;
      do {
         progress = false;
         NIR_PASS(progress, nir, nir_opt_constant_folding);
         NIR_PASS(progress, nir, nir_opt_dead_cf);
         NIR_PASS(progress, nir, nir_opt_dce);
      } while (progress);
      nir_validate_shader(nir, "diagnostic uniform specialization");
   }
   if (argc == 3 && !strcmp(argv[2], "--native-preamble")) {
      if (key.dev.encoding != AGX_ISA_ENCODING_AGX3B || key.secondary ||
          (nir->info.stage != MESA_SHADER_FRAGMENT &&
           nir->info.stage != MESA_SHADER_COMPUTE)) {
         fprintf(stderr, "Native preamble diagnostic requires a main AGX3b fragment or compute shader\n");
         return 2;
      }
      key.no_preamble = false;
      key.promote_textures = false;
      fprintf(stderr, "Diagnostic compiler option: native arithmetic preamble enabled\n");
   }
   nir_print_shader(nir, stdout);
   if (argc == 3 && !strcmp(argv[2], "--check-native-resources")) {
      bool indexed = agx_nir_lower_g17p_indexed_samplers(nir);
      bool images = indexed && agx_nir_lower_g17p_bindless_storage(nir);
      fprintf(stderr, "Native resource shape check: indexed=%d direct_images=%d\n",
              indexed, images);
      /* Late compiler captures use uniform-half indices for descriptor roots;
       * this is only a shape check, not an executable re-lowering. */
      ralloc_free(nir);
      glsl_type_singleton_decref();
      free(data);
      return indexed ? 0 : 1;
   }
   struct agx_shader_part out = {0};
   bool ok = agx_compile_shader_nir(nir, &key, &out);
   fprintf(stderr, "Compiler replay: %s (%u bytes)\n", ok ? "PASS" : "REJECT", out.info.binary_size);
   if (ok) {
      fprintf(stderr, "Compiler binary hash: %08x\n",
              _mesa_hash_data(out.binary, out.info.binary_size));
      agx2_stats_fprintf(stderr, "replay", &out.info.stats);
      fprintf(stderr,
              "Compiler resources: uniforms=%u preamble=%u preamble_gprs=%u "
              "preamble_scratch=%u rodata_uniform=%u rodata_halves=%u\n",
              out.info.push_count, out.info.has_preamble,
              out.info.nr_preamble_gprs, out.info.preamble_scratch_size,
              out.info.rodata.base_uniform, out.info.rodata.size_16);
      if (argc == 3 && !strncmp(argv[2], "--binary-output=", 16)) {
         FILE *binary = fopen(argv[2] + 16, "wb");
         if (!binary) {
            perror(argv[2] + 16);
            ok = false;
         } else {
            ok = fwrite(out.binary, 1, out.info.binary_size, binary) ==
                 out.info.binary_size;
            ok &= fclose(binary) == 0;
            if (!ok)
               fprintf(stderr, "Failed to save compiler binary\n");
         }
      }
   }
   free(out.binary);
   ralloc_free(nir);
   glsl_type_singleton_decref();
   free(data);
   return ok ? 0 : 1;
}
