/*
 * Copyright 2026 Aurora Silicon
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
#include "agx_linker.h"
#include "agx_nir.h"
#include "agx_tilebuffer.h"
#include "agx_uvs.h"
#include "disasm.h"
#include "compiler/glsl_types.h"
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

   /* The driver gets this from agx_open_device; a tool with no device
    * must do it itself before anything touches glsl types (scratch and
    * shared-memory lowering both do).
    */
   glsl_type_singleton_init_or_ref();

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

   /* The container names its stage; honour it or refuse. Defaulting on
    * an unmappable kind is how a mesh shader once "passed" as a fragment
    * shader.
    */
   mesa_shader_stage container_stage;
   if (!stage_forced) {
      if (d12_dxil_container_stage(data, size, &container_stage)) {
         stage = container_stage;
      } else {
         uint32_t kind;
         if (d12_dxil_container_kind(data, size, &kind)) {
            fprintf(stderr, "%s: unsupported shader kind %u (%s)\n", path,
                    kind, d12_dxil_kind_name(kind));
            return 1;
         }
         fprintf(stderr, "%s: container does not declare a stage\n", path);
         return 1;
      }
   }

   if (stage != MESA_SHADER_FRAGMENT && stage != MESA_SHADER_VERTEX &&
       stage != MESA_SHADER_COMPUTE) {
      fprintf(stderr, "%s: stage %s has no AGX backing here\n", path,
              _mesa_shader_stage_to_string(stage));
      return 1;
   }

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
   /* No root signature here, so the map comes from what the module itself
    * declares. The driver never does this: there, both sides read the
    * assignment stored on the root signature.
    */
   struct d12_binding_map map;
   d12_binding_map_from_module(&mod, &map);

   nir_shader *nir = d12_dxil_to_nir(&mod, stage, &map, &error);
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
   NIR_PASS(_, nir, d12_dxil_lower_root);

   struct agx_tilebuffer_layout tib = {0};
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      enum pipe_format formats[8] = {PIPE_FORMAT_R8G8B8A8_UNORM};
      tib = agx_build_tilebuffer_layout(formats, 1, 1, false);

      unsigned bindless_base = 0;
      NIR_PASS(_, nir, agx_nir_lower_tilebuffer, &tib, NULL, &bindless_base,
               NULL, NULL);
      /* Discards become sample_mask writes; hk orders this before the
       * MSAA lowering for the same reason (hk_shader.c).
       */
      NIR_PASS(_, nir, agx_nir_lower_sample_mask);
      NIR_PASS(_, nir, agx_nir_lower_monolithic_msaa, 1);
      NIR_PASS(_, nir, agx_nir_lower_multisampled_image_store);
      NIR_PASS(_, nir, agx_nir_lower_texture);
   }

   /* Vertex outputs go through the unified vertex store, not plain
    * store_output — the vertex-stage analogue of the tilebuffer lowering.
    */
   struct agx_unlinked_uvs_layout uvs = {0};
   if (nir->info.stage == MESA_SHADER_VERTEX)
      /* Real vertex attributes arrive through the prolog ABI: the main
       * shader reads registers the prolog exported, so load_input must
       * become load_exported_agx here (see d12_pipeline.c). A
       * DRAW with a nonempty input layout also needs an attribute-
       * fetching prolog keyed from that layout, which does not exist
       * yet: the static_vi prolog only fixes up vertex/instance IDs.
       */
      NIR_PASS(_, nir, agx_nir_lower_vs_input_to_prolog);
      NIR_PASS(_, nir, agx_nir_lower_uvs, &uvs);

      /* Same constant lowering the driver does (see d12_pipeline.c). */
      struct agx_varyings_vs linked = {0};
      agx_assign_uvs(&linked, &uvs, 0, 0);
      NIR_PASS(_, nir, d12_dxil_lower_uvs_index, linked.slots);

   /*
    * This tool has no open device, so the device key is filled in for the
    * hardware the campaign targets rather than gathered. It has to be
    * filled in at all: the AGX backend asserts on an unset coherency
    * tristate the moment a shader uses a global atomic, and the assert
    * fires in the packer, a long way from anything that looks like a
    * missing device property.
    */
   struct agx_shader_key key = {
      .no_stop = false,
      /* Must match d12_pipeline.c. The driver keeps constants in the
       * binary; promoting them here would disassemble a shader the driver
       * never runs, with different uniform registers.
       */
      .promote_constants = false,
      /* The driver reserves the low uniforms for the root table and the
       * texture heap, so the preamble must allocate above them. Without
       * this the tool compiles a DIFFERENT shader from the one the driver
       * runs -- one whose preamble writes the registers the driver
       * publishes bindings in -- and its disassembly stops being evidence
       * about the driver.
       */
      .reserved_preamble = D12_UNIFORM_RESERVED,
      /* Large shaders spill; without this the allocator asserts that
       * internal shaders are unspillable. */
      .has_scratch = true,
      .dev = {
         /* M-series with one cluster per die: no G13X coherency. */
         .needs_g13x_coherency = U_TRISTATE_NO,
         .soft_fault = false,
      },
   };

   if (print_nir) {
      printf("\n=== NIR after AGX lowering ===\n");
      nir_print_shader(nir, stdout);
   }

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
