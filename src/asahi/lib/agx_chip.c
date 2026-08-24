/*
 * Copyright 2026 The Asahi Linux Contributors
 * SPDX-License-Identifier: MIT
 */

#include "agx_chip.h"

enum agx_chip
agx_chip_from_params(const struct drm_asahi_params_global *params)
{
   if (params->num_clusters_total == 0 ||
       params->num_clusters_total > DRM_ASAHI_MAX_CLUSTERS)
      return AGX_CHIP_UNKNOWN;

   const bool multi_cluster = params->num_clusters_total > 1;

   /* These are command-stream architecture identities, not an ordering.
    *
    * Generations 15 and up are recognised so an unsupported part is refused
    * by name rather than as an anonymous "unsupported architecture", and so
    * that the reason -- no AGX3 code generator -- is the reason reported.
    * agx_chip_codegen_supported is what decides whether the driver proceeds.
    *
    * Apple ships one AGX2 part in the G15 generation (the A16 phone SoC,
    * g15p in its toolchain naming). It is not in any Mac, so this kernel
    * interface never enumerates it and generation 15 here always means an
    * AGX3 M3.
    */
   switch (params->gpu_generation) {
   case 13:
      return multi_cluster ? AGX_CHIP_G13X : AGX_CHIP_G13G;
   case 14:
      return multi_cluster ? AGX_CHIP_G14X : AGX_CHIP_G14G;
   case 15:
      return multi_cluster ? AGX_CHIP_G15X : AGX_CHIP_G15G;
   case 16:
      return multi_cluster ? AGX_CHIP_G16X : AGX_CHIP_G16G;
   case 17:
      return multi_cluster ? AGX_CHIP_G17X : AGX_CHIP_G17G;
   default:
      return AGX_CHIP_UNKNOWN;
   }
}

enum agx_isa
agx_chip_isa(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
      return AGX_ISA_AGX2;

   case AGX_CHIP_G15G:
   case AGX_CHIP_G15X:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16X:
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17X:
      return AGX_ISA_AGX3;

   case AGX_CHIP_UNKNOWN:
   default:
      return AGX_ISA_UNKNOWN;
   }
}

enum agx_isa_encoding
agx_chip_isa_encoding(enum agx_chip chip)
{
   /*
    * Which encoder table a part uses. Apple's own tables decode each family
    * 100% under exactly one of these and essentially 0% under the other, so
    * this is a measurement rather than a reading of the naming scheme.
    *
    * The kernel's generation-and-variant scheme and Apple's toolchain family
    * names line up directly once you have the kernel's core-name table:
    * gpu_gen and gpu_var are a string split of kAGXGPUCoreName[core], so
    * kernel "G17S" is Apple's g17s. Every G15 and G16 variant is on the
    * variant-0 table and every G17 Mac variant is on variant 1, so collapsing
    * Apple's per-die variants into this enum's G/X pair loses nothing here.
    *
    * G17P is variant 0 despite being generation 17, but it is a phone part
    * that this kernel interface never enumerates. See agx_chip_from_params.
    */
   switch (chip) {
   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
      return AGX_ISA_ENCODING_AGX2;

   case AGX_CHIP_G15G:
   case AGX_CHIP_G15X:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16X:
      return AGX_ISA_ENCODING_AGX3B;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17X:
      return AGX_ISA_ENCODING_AGX3C;

   case AGX_CHIP_UNKNOWN:
   default:
      return AGX_ISA_ENCODING_UNKNOWN;
   }
}

bool
agx_chip_codegen_supported(enum agx_chip chip)
{
   /*
    * One *packing* table exists, and it is AGX2 (agx_pack.c, AGX2.xml).
    *
    * Naming an AGX3 part's encoding does not make it supported. AGX3b.xml and
    * AGX3c.xml drive disassemblers, which decode; emitting code needs an
    * encoder, and the operand layout AGX3 selects from is not resolved far
    * enough to build one. Keep this gated on AGX2 until agx_pack.c can emit
    * AGX3, or a recognised M3 will be handed AGX2 instructions.
    */
   return agx_chip_isa_encoding(chip) == AGX_ISA_ENCODING_AGX2;
}

bool
agx_chip_has_hardware_ray_tracing(enum agx_chip chip)
{
   return agx_chip_isa(chip) == AGX_ISA_AGX3;
}

bool
agx_chip_needs_scheduling_stalls(enum agx_chip chip)
{
   /* The M3 base part alone. Not a property of its encoding: G15G shares the
    * agx3b table with G15X, G16G and G16X, none of which pad. */
   return chip == AGX_CHIP_G15G;
}

const char *
agx_chip_name(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G13G: return "G13G";
   case AGX_CHIP_G13X: return "G13X";
   case AGX_CHIP_G14G: return "G14G";
   case AGX_CHIP_G14X: return "G14X";
   case AGX_CHIP_G15G: return "G15G";
   case AGX_CHIP_G15X: return "G15X";
   case AGX_CHIP_G16G: return "G16G";
   case AGX_CHIP_G16X: return "G16X";
   case AGX_CHIP_G17G: return "G17G";
   case AGX_CHIP_G17X: return "G17X";
   case AGX_CHIP_UNKNOWN:
   default: return "unknown";
   }
}

const char *
agx_isa_encoding_name(enum agx_isa_encoding encoding)
{
   switch (encoding) {
   case AGX_ISA_ENCODING_AGX2: return "agx2";
   case AGX_ISA_ENCODING_AGX3B: return "agx3b";
   case AGX_ISA_ENCODING_AGX3C: return "agx3c";
   case AGX_ISA_ENCODING_UNKNOWN:
   default: return "unknown";
   }
}

const char *
agx_isa_name(enum agx_isa isa)
{
   switch (isa) {
   case AGX_ISA_AGX2: return "AGX2";
   case AGX_ISA_AGX3: return "AGX3";
   case AGX_ISA_UNKNOWN:
   default: return "unknown";
   }
}
