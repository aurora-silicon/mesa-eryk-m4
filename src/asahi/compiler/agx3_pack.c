/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "agx3_pack.h"

#include <string.h>

static bool
is_agx3_encoding(enum agx_isa_encoding encoding)
{
   return encoding == AGX_ISA_ENCODING_AGX3B ||
          encoding == AGX_ISA_ENCODING_AGX3C;
}

static uint16_t
read_le16(const uint8_t *bytes)
{
   return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void
write_le16(uint8_t *bytes, uint16_t value)
{
   bytes[0] = value;
   bytes[1] = value >> 8;
}

bool
agx3_pack_ldimm_u32(enum agx_isa_encoding encoding, uint16_t dest,
                    uint32_t imm, struct agx3_encoded_ldimm *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || dest >= 256 ||
       (dest & 1) != 0)
      return false;

   memset(out, 0, sizeof(*out));

   /*
    * The compact encoding has a four-bit destination in units of 32-bit
    * registers and a seven-bit immediate.  Only its opcode nibble differs:
    * AGX3b uses 0xc and AGX3c uses 0x4.  These bytes match every mi_1..mi_127
    * probe on G15/G16 and G17 respectively.
    */
   if (dest < 32 && imm < 128) {
      const uint8_t opcode =
         encoding == AGX_ISA_ENCODING_AGX3B ? 0x0c : 0x04;

      out->bytes[0] = opcode | ((dest >> 1) << 4);
      out->bytes[1] = imm;
      out->length = 2;
      return true;
   }

   /*
    * The full u32 form is byte-identical in AGX3b and AGX3c.  Apple's f14
    * immediate is scattered across four 16-bit words:
    *
    *   i[0:6]   -> w0[8:14]
    *   i[7:10]  -> w2[1:4]
    *   i[11:12] -> w2[10:11]
    *   i[13:16] -> w3[0:3]
    *   i[17:24] -> w3[4:11]
    *   i[25:31] -> w1[9:15]
    *
    * f2 is the destination half-register: bits 0, 1:4, 5:6 and 7 land in
    * w1[2], w0[4:7], w1[6:7] and w1[3].  Bit 0 is zero for this u32 form.
    */
   const uint16_t words[] = {
      0x800c | (((dest >> 1) & 0xf) << 4) | ((imm & 0x7f) << 8),
      0x0002 | ((dest & 1) << 2) | (((dest >> 5) & 0x3) << 6) |
         (((dest >> 7) & 0x1) << 3) | (((imm >> 25) & 0x7f) << 9),
      (((imm >> 7) & 0xf) << 1) | (((imm >> 11) & 0x3) << 10),
      ((imm >> 13) & 0xf) | (((imm >> 17) & 0xff) << 4),
   };

   for (unsigned i = 0; i < 4; ++i)
      write_le16(&out->bytes[2 * i], words[i]);

   out->length = 8;
   return true;
}

bool
agx3_unpack_ldimm_u32(enum agx_isa_encoding encoding, const uint8_t *bytes,
                      size_t available, uint16_t *dest, uint32_t *imm,
                      uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       imm == NULL || length == NULL || available < 2)
      return false;

   const uint16_t word0 = read_le16(bytes);
   const uint8_t short_opcode =
      encoding == AGX_ISA_ENCODING_AGX3B ? 0x0c : 0x04;

   if ((word0 & 0x800f) == short_opcode) {
      *dest = ((word0 >> 4) & 0xf) << 1;
      *imm = (word0 >> 8) & 0x7f;
      *length = 2;
      return true;
   }

   if (available < 8 || (word0 & 0x800f) != 0x800c)
      return false;

   const uint16_t word1 = read_le16(bytes + 2);
   const uint16_t word2 = read_le16(bytes + 4);
   const uint16_t word3 = read_le16(bytes + 6);

   /* Encoding point 1/2, f10=2 (u32 register), with every unassigned bit 0. */
   const uint16_t word1_allowed =
      0x0003 | (1u << 2) | (1u << 3) | (3u << 6) | (0x7fu << 9);
   const uint16_t word2_allowed = (0xfu << 1) | (3u << 10);

   if ((word1 & 0x0003) != 0x0002 || (word1 & ~word1_allowed) != 0 ||
       (word2 & ~word2_allowed) != 0 || (word3 & 0xf000) != 0)
      return false;

   const uint16_t decoded_dest =
      ((word1 >> 2) & 0x1) | (((word0 >> 4) & 0xf) << 1) |
      (((word1 >> 6) & 0x3) << 5) | (((word1 >> 3) & 0x1) << 7);

   /* Odd values select a different destination-size form. */
   if (decoded_dest >= 256 || (decoded_dest & 1) != 0)
      return false;

   *dest = decoded_dest;
   *imm = ((word0 >> 8) & 0x7f) | (((word2 >> 1) & 0xf) << 7) |
          (((word2 >> 10) & 0x3) << 11) | ((word3 & 0xf) << 13) |
          (((word3 >> 4) & 0xff) << 17) |
          (((word1 >> 9) & 0x7f) << 25);
   *length = 8;
   return true;
}
