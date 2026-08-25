/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_raytracing_core.h"

bool
hk_agx3_continue_ray_instruction_generate(
   enum agx_chip chip, uint16_t state_register,
   struct hk_agx3_continue_ray_instruction *out)
{
   if (!out)
      return false;

   *out = (struct hk_agx3_continue_ray_instruction){0};

   struct agx3_encoded_continue_ray shared;
   if (!agx3_pack_continue_ray(
          agx3_continue_ray_witnessed_chip_encoding(chip), state_register,
          &shared) ||
       shared.length > sizeof(out->bytes))
      return false;

   for (uint8_t i = 0; i < shared.length; ++i)
      out->bytes[i] = shared.bytes[i];
   out->length = shared.length;
   return true;
}

bool
hk_agx3_continue_ray_instruction_decode(
   enum agx_chip chip, const void *instruction, size_t instruction_size,
   uint16_t *state_register)
{
   if (!instruction || !state_register)
      return false;

   *state_register = 0;
   const enum agx_isa_encoding encoding =
      agx3_continue_ray_witnessed_chip_encoding(chip);
   uint16_t decoded = 0;
   uint8_t decoded_length = 0;
   struct agx3_encoded_continue_ray canonical;

   if (!agx3_unpack_continue_ray(encoding, instruction, instruction_size,
                                 &decoded, &decoded_length) ||
       instruction_size != decoded_length ||
       !agx3_pack_continue_ray(encoding, decoded, &canonical) ||
       canonical.length != decoded_length)
      return false;

   const uint8_t *code = instruction;
   for (uint8_t i = 0; i < canonical.length; ++i) {
      if (code[i] != canonical.bytes[i])
         return false;
   }

   *state_register = decoded;
   return true;
}
