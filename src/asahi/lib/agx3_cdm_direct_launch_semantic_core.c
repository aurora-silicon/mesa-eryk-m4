/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_cdm_direct_launch_semantic_core.h"

#include "asahi/genxml/agx3_cmdstream.h"

#include <string.h>

/* Pinned CommandEncodingNext performEnqueueKernel emitters in the G15G,
 * G15X, G16G, G16X, G17P, G17G and G17X plugin families all construct this
 * direct 40-byte record.  G16P and G17A have no corresponding direct witness
 * in that corpus and deliberately remain closed. */
bool
agx3_cdm_direct_launch_target_grounded(enum agx_chip chip)
{
   return agx3_cdm_launch_target_grounded(chip);
}

bool
agx3_cdm_direct_template_build(
   const struct agx3_cdm_direct_template_fields *fields,
   struct agx3_cdm_direct_template *result)
{
   if (result)
      memset(result, 0, sizeof(*result));

   if (!fields || !result || fields->word0_bits_3_14 > 0xfff ||
       (fields->word0_bits_3_14 & 1) != 0 ||
       fields->word1_bits_3_20 > 0x3ffff)
      return false;

   /* ComputeProgramVariant::finalize has the same bit construction on the
    * G15/HAL200 and HAL300 sides of the command-stream boundary.  The source
    * object offsets differ, but their recovered values occupy these exact
    * template ranges in both implementations. */
   result->word0 = ((uint32_t)fields->word0_bits_3_14 << 3) |
                   ((uint32_t)fields->compiler_reply_word0_bit23 << 23) |
                   ((uint32_t)fields->subkernel_x_target_workitems << 19);
   result->word1 = fields->word1_bits_3_20 << 3;
   return true;
}

bool
agx3_cdm_direct_template_build_from_sources(
   const struct agx3_cdm_direct_template_sources *sources,
   struct agx3_cdm_direct_template *result)
{
   if (result)
      memset(result, 0, sizeof(*result));

   if (!sources || !result)
      return false;

   /* HAL200 uses state-load fields +0x128/+0x12c and HAL300 uses
    * +0x158/+0x15c, but both finalize implementations apply these exact
    * transforms before inserting the values in the launch template. */
   const uint64_t word1_value =
      ((uint64_t)sources->load_shader_word1_source_bytes + 3) >> 2;
   const uint64_t word0_rounded =
      ((uint64_t)sources->load_shader_word0_source_bytes + 3) >> 2;
   const uint64_t word0_value = (word0_rounded + 1) & ~UINT64_C(1);

   if (word0_value > 0xfff || word1_value > 0x3ffff)
      return false;

   const struct agx3_cdm_direct_template_fields fields = {
      .word0_bits_3_14 = (uint16_t)word0_value,
      .word1_bits_3_20 = (uint32_t)word1_value,
      .compiler_reply_word0_bit23 = sources->compiler_reply_word0_bit23,
      .subkernel_x_target_workitems =
         sources->subkernel_x_target_workitems,
   };
   return agx3_cdm_direct_template_build(&fields, result);
}

bool
agx3_cdm_direct_template_decode(
   const struct agx3_cdm_direct_template *template_words,
   struct agx3_cdm_direct_template_fields *fields)
{
   if (fields)
      memset(fields, 0, sizeof(*fields));

   if (!template_words || !fields ||
       (template_words->word0 & ~AGX3_CDM_DIRECT_TEMPLATE_WORD0_MASK) != 0 ||
       (template_words->word1 & ~AGX3_CDM_DIRECT_TEMPLATE_WORD1_MASK) != 0)
      return false;

   fields->word0_bits_3_14 = (template_words->word0 >> 3) & 0xfff;
   fields->word1_bits_3_20 = (template_words->word1 >> 3) & 0x3ffff;
   fields->compiler_reply_word0_bit23 =
      (template_words->word0 >> 23) & 1;
   fields->subkernel_x_target_workitems =
      (template_words->word0 >> 19) & 1;
   return true;
}

static bool
agx3_cdm_direct_dimension_word(uint8_t dimensions, uint32_t *word)
{
   if (!word)
      return false;

   switch (dimensions) {
   case 1:
      *word = UINT32_C(0x28);
      return true;
   case 2:
      *word = UINT32_C(0x30);
      return true;
   case 3:
      *word = UINT32_C(0x34);
      return true;
   default:
      *word = 0;
      return false;
   }
}

static bool
agx3_cdm_direct_g15x_bit31_allowed(enum agx_chip chip)
{
   return chip == AGX_CHIP_G15S || chip == AGX_CHIP_G15C ||
          chip == AGX_CHIP_G15D;
}

static bool
agx3_cdm_direct_word3_build(enum agx_chip chip,
                            enum agx3_cdm_direct_grid_mode mode,
                            uint8_t dimensions, bool g15x_bit31,
                            uint32_t *word3)
{
   uint32_t value = 0;
   if (!agx3_cdm_direct_dimension_word(dimensions, &value))
      return false;

   switch (mode) {
   case AGX3_CDM_DIRECT_GRID_THREADGROUPS:
      value |= UINT32_C(1) << 30;
      if (g15x_bit31) {
         if (!agx3_cdm_direct_g15x_bit31_allowed(chip))
            return false;
         value |= UINT32_C(1) << 31;
      }
      break;
   case AGX3_CDM_DIRECT_GRID_THREADS:
      /* The pinned emitters select a separate 24-byte template for direct
       * thread dispatch.  It cannot be represented by this 40-byte API. */
      return false;
   default:
      return false;
   }

   *word3 = value;
   return true;
}

bool
agx3_cdm_direct_launch_pack(
   const struct agx3_cdm_direct_launch_info *info,
   uint8_t packed[AGX3_CDM_DIRECT_LAUNCH_SIZE])
{
   if (!packed)
      return false;

   memset(packed, 0, AGX3_CDM_DIRECT_LAUNCH_SIZE);
   if (!info || !agx3_cdm_direct_launch_target_grounded(info->chip))
      return false;

   struct agx3_cdm_direct_template_fields fields;
   if (!agx3_cdm_direct_template_decode(&info->template_words, &fields))
      return false;

   uint32_t word3 = 0;
   if (!agx3_cdm_direct_word3_build(info->chip, info->grid_mode,
                                    info->dimensions,
                                    info->g15x_word3_bit31, &word3))
      return false;

   /* performEnqueueKernel clears template bits 17..19 when both higher grid
    * axes contain fewer than two threads.  Bits 17 and 18 are already outside
    * the proven template mask, leaving the directly recovered bit 19. */
   uint32_t emitted_word0 = info->template_words.word0;
   if (info->global_size[1] < 2 && info->global_size[2] < 2)
      emitted_word0 &= UINT32_C(0xfff1ffff);

   const struct agx3_cdm_launch_info raw = {
      .chip = info->chip,
      .word0_payload = emitted_word0,
      .word1_payload = info->template_words.word1,
      .pipeline = info->pipeline,
      .opaque_word3 = word3,
      .global_size = {info->global_size[0], info->global_size[1],
                      info->global_size[2]},
      .local_size = {info->local_size[0], info->local_size[1],
                     info->local_size[2]},
   };

   return agx3_cdm_launch_pack(&raw, packed);
}

bool
agx3_cdm_direct_launch_unpack(
   enum agx_chip chip, const void *packed, size_t available,
   struct agx3_cdm_direct_launch_info *info)
{
   if (info)
      memset(info, 0, sizeof(*info));

   if (!packed || !info ||
       !agx3_cdm_direct_launch_target_grounded(chip))
      return false;

   struct agx3_cdm_launch_info raw;
   if (!agx3_cdm_launch_unpack(chip, packed, available, &raw))
      return false;

   const struct agx3_cdm_direct_template template_words = {
      .word0 = raw.word0_payload,
      .word1 = raw.word1_payload,
   };
   struct agx3_cdm_direct_template_fields fields;
   if (!agx3_cdm_direct_template_decode(&template_words, &fields))
      return false;

   if ((raw.opaque_word3 & (UINT32_C(1) << 30)) == 0)
      return false;

   const enum agx3_cdm_direct_grid_mode mode =
      AGX3_CDM_DIRECT_GRID_THREADGROUPS;
   const bool g15x_bit31 = (raw.opaque_word3 >> 31) & 1;
   const uint32_t dimension_word =
      raw.opaque_word3 & ~(UINT32_C(3) << 30);

   uint8_t dimensions = 0;
   switch (dimension_word) {
   case UINT32_C(0x28):
      dimensions = 1;
      break;
   case UINT32_C(0x30):
      dimensions = 2;
      break;
   case UINT32_C(0x34):
      dimensions = 3;
      break;
   default:
      return false;
   }

   uint32_t canonical_word3 = 0;
   if (!agx3_cdm_direct_word3_build(chip, mode, dimensions, g15x_bit31,
                                    &canonical_word3) ||
       canonical_word3 != raw.opaque_word3 ||
       (raw.global_size[1] < 2 && raw.global_size[2] < 2 &&
        fields.subkernel_x_target_workitems))
      return false;

   *info = (struct agx3_cdm_direct_launch_info){
      .chip = chip,
      .template_words = template_words,
      .pipeline = raw.pipeline,
      .grid_mode = mode,
      .dimensions = dimensions,
      .g15x_word3_bit31 = g15x_bit31,
      .global_size = {raw.global_size[0], raw.global_size[1],
                      raw.global_size[2]},
      .local_size = {raw.local_size[0], raw.local_size[1],
                     raw.local_size[2]},
   };
   return true;
}
