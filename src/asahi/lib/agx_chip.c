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

   /* These are command-stream architecture identities, not an ordering.
    *
    * Generations 15 and up are recognised so an unsupported part is refused
    * by name rather than as an anonymous "unsupported architecture", and so
    * that the reason -- no AGX3 code generator -- is the reason reported.
    * agx_chip_codegen_supported is what decides whether the driver proceeds.
    *
    * The variant character is load-bearing. Cluster count cannot distinguish
    * G17P (A18 Pro, AGX3b) from G17G (M5, AGX3c), and G15P is an AGX2 part in
    * the same numeric generation as AGX3 M3. The kernel already publishes the
    * exact P/G/S/C/D/A character, so use it directly.
    */
   switch (params->gpu_generation) {
   case 13:
      switch (params->gpu_variant) {
      case 'G': return AGX_CHIP_G13G;
      case 'S':
      case 'C':
      case 'D': return AGX_CHIP_G13X;
      default: return AGX_CHIP_UNKNOWN;
      }
   case 14:
      switch (params->gpu_variant) {
      case 'G': return AGX_CHIP_G14G;
      case 'S':
      case 'C':
      case 'D': return AGX_CHIP_G14X;
      default: return AGX_CHIP_UNKNOWN;
      }
   case 15:
      switch (params->gpu_variant) {
      case 'P': return AGX_CHIP_G15P;
      case 'G': return AGX_CHIP_G15G;
      case 'S': return AGX_CHIP_G15S;
      case 'C': return AGX_CHIP_G15C;
      case 'D': return AGX_CHIP_G15D;
      default: return AGX_CHIP_UNKNOWN;
      }
   case 16:
      switch (params->gpu_variant) {
      case 'P': return AGX_CHIP_G16P;
      case 'G': return AGX_CHIP_G16G;
      case 'S': return AGX_CHIP_G16S;
      case 'C': return AGX_CHIP_G16C;
      default: return AGX_CHIP_UNKNOWN;
      }
   case 17:
      switch (params->gpu_variant) {
      case 'P': return AGX_CHIP_G17P;
      case 'A': return AGX_CHIP_G17A;
      case 'G': return AGX_CHIP_G17G;
      case 'S': return AGX_CHIP_G17S;
      case 'C': return AGX_CHIP_G17C;
      default: return AGX_CHIP_UNKNOWN;
      }
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
   case AGX_CHIP_G15P:
      return AGX_ISA_AGX2;

   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
   case AGX_CHIP_G17A:
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
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
    * gpu_gen and gpu_var are a string split of the kernel core-name table, so
    * they map directly. P/A are not Mac performance tiers: G15P is AGX2 and
    * G17P/G17A remain on the AGX3b/HAL200 side while the G17 Mac parts use
    * AGX3c/HAL300.
    */
   switch (chip) {
   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
   case AGX_CHIP_G15P:
      return AGX_ISA_ENCODING_AGX2;

   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
   case AGX_CHIP_G17A:
      return AGX_ISA_ENCODING_AGX3B;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return AGX_ISA_ENCODING_AGX3C;

   case AGX_CHIP_UNKNOWN:
   default:
      return AGX_ISA_ENCODING_UNKNOWN;
   }
}

enum agx_command_stream_generation
agx_chip_command_stream_generation(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
      return AGX_CS_GENERATION_AGX2;

   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
      return AGX_CS_GENERATION_G15;

   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
   case AGX_CHIP_G17A:
      return AGX_CS_GENERATION_HAL200;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return AGX_CS_GENERATION_HAL300;

   /* The A16 compiler target is known to be AGX2, but its command-stream
    * class was not present in the Mac driver corpus. Do not infer it. */
   case AGX_CHIP_G15P:
   case AGX_CHIP_UNKNOWN:
   default:
      return AGX_CS_GENERATION_UNKNOWN;
   }
}

enum agx_descriptor_generation
agx_chip_descriptor_generation(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
      return AGX_DESCRIPTOR_GENERATION_7;
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
      return AGX_DESCRIPTOR_GENERATION_8;

   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
   case AGX_CHIP_G17A:
      return AGX_DESCRIPTOR_GENERATION_9;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return AGX_DESCRIPTOR_GENERATION_10;

   case AGX_CHIP_G15P:
   case AGX_CHIP_UNKNOWN:
   default:
      return AGX_DESCRIPTOR_GENERATION_UNKNOWN;
   }
}

enum agx_sampler_generation
agx_chip_sampler_generation(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
      return AGX_SAMPLER_GENERATION_GEN4;

   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
   case AGX_CHIP_G17A:
      return AGX_SAMPLER_GENERATION_GEN4_HAL200;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return AGX_SAMPLER_GENERATION_GEN5;

   case AGX_CHIP_G15P:
   case AGX_CHIP_UNKNOWN:
   default:
      return AGX_SAMPLER_GENERATION_UNKNOWN;
   }
}

enum agx_submission_model
agx_chip_submission_model(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16G:
      return AGX_SUBMISSION_MODEL_CLASSIC;

   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
      return AGX_SUBMISSION_MODEL_DUAL;

   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return AGX_SUBMISSION_MODEL_SKSM;

   /* No corresponding accelerator class was present in the Mac corpus. */
   case AGX_CHIP_G15P:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G17A:
   case AGX_CHIP_UNKNOWN:
   default:
      return AGX_SUBMISSION_MODEL_UNKNOWN;
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
agx_chip_driver_supported(enum agx_chip chip)
{
   /* Only the existing AGX2 command stream and descriptor path is wired into
    * the driver today. Keep all independently-versioned axes in the gate so a
    * newly-added packer cannot accidentally enable an incomplete chip. */
   return agx_chip_codegen_supported(chip) &&
          agx_chip_command_stream_generation(chip) == AGX_CS_GENERATION_AGX2 &&
          agx_chip_descriptor_generation(chip) !=
             AGX_DESCRIPTOR_GENERATION_UNKNOWN &&
          agx_chip_sampler_generation(chip) != AGX_SAMPLER_GENERATION_UNKNOWN;
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
    * agx3b table with G15S/C/D, every G16 part, and G17P/A, none of which
    * pad. */
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
   case AGX_CHIP_G15P: return "G15P";
   case AGX_CHIP_G15G: return "G15G";
   case AGX_CHIP_G15S: return "G15S";
   case AGX_CHIP_G15C: return "G15C";
   case AGX_CHIP_G15D: return "G15D";
   case AGX_CHIP_G16P: return "G16P";
   case AGX_CHIP_G16G: return "G16G";
   case AGX_CHIP_G16S: return "G16S";
   case AGX_CHIP_G16C: return "G16C";
   case AGX_CHIP_G17P: return "G17P";
   case AGX_CHIP_G17A: return "G17A";
   case AGX_CHIP_G17G: return "G17G";
   case AGX_CHIP_G17S: return "G17S";
   case AGX_CHIP_G17C: return "G17C";
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

const char *
agx_command_stream_generation_name(
   enum agx_command_stream_generation generation)
{
   switch (generation) {
   case AGX_CS_GENERATION_AGX2:
      return "AGX2";
   case AGX_CS_GENERATION_G15:
      return "G15";
   case AGX_CS_GENERATION_HAL200:
      return "HAL200";
   case AGX_CS_GENERATION_HAL300:
      return "HAL300";
   case AGX_CS_GENERATION_UNKNOWN:
   default:
      return "unknown";
   }
}
