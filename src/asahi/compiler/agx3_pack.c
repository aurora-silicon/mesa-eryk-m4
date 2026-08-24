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
          ((uint32_t)((word1 >> 9) & 0x7f) << 25);
   *length = 8;
   return true;
}

bool
agx3_pack_iadd_u32_imm8(enum agx_isa_encoding encoding, uint16_t dest,
                        uint16_t src, uint8_t imm,
                        struct agx3_encoded_iadd_u32_imm8 *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || (dest & 1) != 0 ||
       (src & 1) != 0)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* This encoding point has 96 addressable 32-bit registers. */
      if (dest >= 192 || src >= 192)
         return false;

      const uint8_t canonical[] = {
         0x9f, 0x11, 0x54, 0x00, 0x02, 0x00, 0x00, 0x88, 0x11, 0x04,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2 is deposited contiguously for this AGX3b encoding point. */
      out->bytes[3] = dest;

      /* f31: s[1:5] -> b6[3:7], s[6:7] -> b7[0:1]. */
      out->bytes[6] |= (src & 0x3e) << 2;
      out->bytes[7] |= src >> 6;

      /* f15: i[0:6] -> b5[1:7], i[7] -> b6[0]. */
      out->bytes[5] |= (imm & 0x7f) << 1;
      out->bytes[6] |= imm >> 7;
      out->length = sizeof(canonical);
      return true;
   }

   if (dest >= 256 || src >= 256)
      return false;

   const uint8_t canonical[] = {
      0x27, 0x04, 0x04, 0x1a, 0x25, 0x00,
      0xa3, 0x02, 0x20, 0x80, 0x01, 0x00,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1,4] -> b0[4,7], d[2:3,5] -> b7[3:5], d[6:7] -> b2[7,3]. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f31: s[1] -> b8[7], s[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src >> 1) & 0x1) << 7;
   out->bytes[9] |= (src >> 2) & 0x3f;

   /* f15: i[0:1], i[2:4], i[5], i[6:7] are scattered over four bytes. */
   out->bytes[1] |= imm & 0x3;
   out->bytes[3] |= (imm & 0x1c) << 3;
   out->bytes[5] |= (imm & 0x20) << 2;
   out->bytes[8] |= imm >> 6;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_iadd_u32_imm8(enum agx_isa_encoding encoding,
                          const uint8_t *bytes, size_t available,
                          uint16_t *dest, uint16_t *src, uint8_t *imm,
                          uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src == NULL || imm == NULL || length == NULL)
      return false;

   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 10 : 12;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_src;
   uint8_t decoded_imm;

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = bytes[3];
      decoded_src = ((bytes[6] >> 2) & 0x3e) |
                    ((bytes[7] & 0x3) << 6);
      decoded_imm = ((bytes[5] >> 1) & 0x7f) | ((bytes[6] & 0x1) << 7);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src = (((bytes[8] >> 7) & 0x1) << 1) |
                    ((bytes[9] & 0x3f) << 2);
      decoded_imm = (bytes[1] & 0x3) | (((bytes[3] >> 5) & 0x7) << 2) |
                    (((bytes[5] >> 7) & 0x1) << 5) |
                    ((bytes[8] & 0x3) << 6);
   }

   struct agx3_encoded_iadd_u32_imm8 canonical;
   if (!agx3_pack_iadd_u32_imm8(encoding, decoded_dest, decoded_src,
                                decoded_imm, &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *src = decoded_src;
   *imm = decoded_imm;
   *length = expected_length;
   return true;
}

bool
agx3_pack_iadd_u32_reg_reg(enum agx_isa_encoding encoding, uint16_t dest,
                           uint16_t src0, uint16_t src1,
                           struct agx3_encoded_iadd_u32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || (dest & 1) != 0 ||
       (src0 & 1) != 0 || (src1 & 1) != 0)
      return false;

   const uint16_t register_limit =
      encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;
   if (dest >= register_limit || src0 >= register_limit ||
       src1 >= register_limit)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target Apple compiler replay. */
      const uint8_t canonical[] = {
         0x9f, 0x01, 0x56, 0x00, 0x02, 0x00, 0x00, 0xa8, 0x13, 0x05,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2 is contiguous at b3 for this AGX3b encoding point. */
      out->bytes[3] = dest;

      /* f16: s0[1:6] -> b5[2:7], s0[7] -> b6[0]. */
      out->bytes[5] |= (src0 & 0x7e) << 1;
      out->bytes[6] |= src0 >> 7;

      /* f31: s1[1:5] -> b6[3:7], s1[6:7] -> b7[0:1]. */
      out->bytes[6] |= (src1 & 0x3e) << 2;
      out->bytes[7] |= src1 >> 6;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S Apple compiler replay. */
   const uint8_t canonical[] = {
      0x2f, 0x00, 0x04, 0x1a, 0x21, 0x00,
      0xa3, 0x02, 0x28, 0x80, 0x01, 0x00,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1,4] -> b0[4,7], d[2:3,5] -> b7[3:5], d[6:7] -> b2[7,3]. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16: s0[1], s0[2:4], s0[5], s0[6:7] use four byte fragments. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[3] |= (src0 & 0x1c) << 3;
   out->bytes[5] |= (src0 & 0x20) << 2;
   out->bytes[8] |= (src0 >> 6) & 0x3;

   /* f31: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_iadd_u32_reg_reg(enum agx_isa_encoding encoding,
                             const uint8_t *bytes, size_t available,
                             uint16_t *dest, uint16_t *src0, uint16_t *src1,
                             uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL)
      return false;

   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 10 : 12;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = bytes[3];
      decoded_src0 = ((bytes[5] >> 1) & 0x7e) |
                     ((bytes[6] & 0x1) << 7);
      decoded_src1 = ((bytes[6] >> 2) & 0x3e) |
                     ((bytes[7] & 0x3) << 6);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[3] >> 5) & 0x7) << 2) |
                     (((bytes[5] >> 7) & 0x1) << 5) |
                     ((bytes[8] & 0x3) << 6);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_iadd_u32_reg_reg canonical;
   if (!agx3_pack_iadd_u32_reg_reg(encoding, decoded_dest, decoded_src0,
                                   decoded_src1, &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = expected_length;
   return true;
}

bool
agx3_pack_fadd_f32_reg_reg(enum agx_isa_encoding encoding, uint16_t dest,
                           uint16_t src0, uint16_t src1,
                           struct agx3_encoded_fadd_f32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || dest >= 256 ||
       src0 >= 256 || src1 >= 256 || (dest & 1) != 0 || (src0 & 1) != 0 ||
       (src1 & 1) != 0)
      return false;

   /*
    * Exact common compact selection from the G15G/G16G/G17S delayed-use
    * replay.  Apple fields f2/f35/f18 are destination/source0/source1.
    * Anonymous context fields remain fixed rather than being named here.
    */
   const uint8_t canonical[AGX3_FADD_F32_REG_REG_LENGTH] = {
      0x09, 0x01, 0x04, 0x01, 0x00, 0x20,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1:4] -> b0[4:7], d[5:6] -> b2[6:7], d[7] -> b5[4]. */
   out->bytes[0] |= (dest & 0x1e) << 3;
   out->bytes[2] |= (dest & 0x60) << 1;
   out->bytes[5] |= (dest & 0x80) >> 3;

   /* f35: s0[1:6] -> b3[1:6], s0[7] -> b5[2]. */
   out->bytes[3] |= src0 & 0x7e;
   out->bytes[5] |= (src0 & 0x80) >> 5;

   /* f18: s1[1:6] -> b1[1:6], s1[7] -> b5[0]. */
   out->bytes[1] |= src1 & 0x7e;
   out->bytes[5] |= (src1 & 0x80) >> 7;

   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_fadd_f32_reg_reg(enum agx_isa_encoding encoding,
                             const uint8_t *bytes, size_t available,
                             uint16_t *dest, uint16_t *src0, uint16_t *src1,
                             uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL ||
       available < AGX3_FADD_F32_REG_REG_LENGTH)
      return false;

   const uint16_t decoded_dest = ((bytes[0] >> 3) & 0x1e) |
                                 ((bytes[2] >> 1) & 0x60) |
                                 ((bytes[5] << 3) & 0x80);
   const uint16_t decoded_src0 = (bytes[3] & 0x7e) |
                                 ((bytes[5] << 5) & 0x80);
   const uint16_t decoded_src1 = (bytes[1] & 0x7e) |
                                 ((bytes[5] << 7) & 0x80);

   struct agx3_encoded_fadd_f32_reg_reg canonical;
   if (!agx3_pack_fadd_f32_reg_reg(encoding, decoded_dest, decoded_src0,
                                   decoded_src1, &canonical) ||
       memcmp(bytes, canonical.bytes, canonical.length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = canonical.length;
   return true;
}

bool
agx3_pack_fmul_f32_reg_reg(enum agx_isa_encoding encoding, uint16_t dest,
                           uint16_t src0, uint16_t src1,
                           struct agx3_encoded_fmul_f32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || dest >= 128 ||
       src0 >= 128 || src1 >= 128 || (dest & 1) != 0 || (src0 & 1) != 0 ||
       (src1 & 1) != 0)
      return false;

   /*
    * Exact compact selections from the pinned multiply ladders.  Apple
    * fields f2/f35/f18 are destination/source0/source1.  The selected AGX3b
    * L/P2 witness and selected G17S AGX3c Q witness differ in their fixed
    * anonymous/context trailer, so keep those exact selections distinct.
    */
   const uint8_t canonical_b[AGX3_FMUL_F32_REG_REG_LENGTH] = {
      0x09, 0x01, 0x1d, 0x01, 0x00, 0xc0,
   };
   const uint8_t canonical_c[AGX3_FMUL_F32_REG_REG_LENGTH] = {
      0x09, 0x01, 0x1d, 0x01, 0x00, 0x20,
   };
   const uint8_t *canonical =
      encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b : canonical_c;
   memcpy(out->bytes, canonical, AGX3_FMUL_F32_REG_REG_LENGTH);

   /* f2: d[1:4] -> b0[4:7], d[5:6] -> b2[6:7]. */
   out->bytes[0] |= (dest & 0x1e) << 3;
   out->bytes[2] |= (dest & 0x60) << 1;

   /* f35: s0[1:6] -> b3[1:6]. */
   out->bytes[3] |= src0 & 0x7e;

   /* f18: s1[1:6] -> b1[1:6]. */
   out->bytes[1] |= src1 & 0x7e;

   out->length = AGX3_FMUL_F32_REG_REG_LENGTH;
   return true;
}

bool
agx3_unpack_fmul_f32_reg_reg(enum agx_isa_encoding encoding,
                             const uint8_t *bytes, size_t available,
                             uint16_t *dest, uint16_t *src0, uint16_t *src1,
                             uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL ||
       available < AGX3_FMUL_F32_REG_REG_LENGTH)
      return false;

   const uint16_t decoded_dest = ((bytes[0] >> 3) & 0x1e) |
                                 ((bytes[2] >> 1) & 0x60);
   const uint16_t decoded_src0 = bytes[3] & 0x7e;
   const uint16_t decoded_src1 = bytes[1] & 0x7e;

   struct agx3_encoded_fmul_f32_reg_reg canonical;
   if (!agx3_pack_fmul_f32_reg_reg(encoding, decoded_dest, decoded_src0,
                                   decoded_src1, &canonical) ||
       memcmp(bytes, canonical.bytes, canonical.length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = canonical.length;
   return true;
}

bool
agx3_pack_fmul_f32_reg_reg_long(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_fmul_f32_reg_reg_long *out)
{
   if (out == NULL || encoding != AGX_ISA_ENCODING_AGX3C || dest >= 128 ||
       src0 >= 128 || src1 >= 128 || (dest & 1) != 0 || (src0 & 1) != 0 ||
       (src1 & 1) != 0)
      return false;

   /*
    * Exact f32mul.ep3 selection from the final G17G/G17S L operation and
    * G17S P2f_mul.  Anonymous selector fields, including f46, remain fixed.
    */
   const uint8_t canonical[AGX3_FMUL_F32_REG_REG_LONG_LENGTH] = {
      0x29, 0x00, 0x05, 0x10, 0x22, 0x80, 0xa0,
      0x02, 0x1c, 0x00, 0x20, 0x00, 0x00, 0x10,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d1 -> b0[4], d2..3 -> b7[3:4], d4 -> b0[7],
    *     d5 -> b7[5], d6 -> b2[7]. */
   out->bytes[0] |= (dest & 0x02) << 3;
   out->bytes[7] |= (dest & 0x0c) << 1;
   out->bytes[0] |= (dest & 0x10) << 3;
   out->bytes[7] |= dest & 0x20;
   out->bytes[2] |= (dest & 0x40) << 1;

   /* f35: s0[1] -> b8[7], s0[2:6] -> b9[0:4]. */
   out->bytes[8] |= (src0 & 0x02) << 6;
   out->bytes[9] |= (src0 & 0x7c) >> 2;

   /* f18: s1[1] -> b1[1], s1[2:4] -> b3[5:7],
    *      s1[5] -> b12[1], s1[6] -> b8[0]. */
   out->bytes[1] |= src1 & 0x02;
   out->bytes[3] |= (src1 & 0x1c) << 3;
   out->bytes[12] |= (src1 & 0x20) >> 4;
   out->bytes[8] |= (src1 & 0x40) >> 6;

   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_fmul_f32_reg_reg_long(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length)
{
   if (encoding != AGX_ISA_ENCODING_AGX3C || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL ||
       available < AGX3_FMUL_F32_REG_REG_LONG_LENGTH)
      return false;

   const uint16_t decoded_dest = ((bytes[0] >> 3) & 0x02) |
                                 ((bytes[7] >> 1) & 0x0c) |
                                 ((bytes[0] >> 3) & 0x10) |
                                 (bytes[7] & 0x20) |
                                 ((bytes[2] >> 1) & 0x40);
   const uint16_t decoded_src0 = ((bytes[8] >> 6) & 0x02) |
                                 ((bytes[9] << 2) & 0x7c);
   const uint16_t decoded_src1 = (bytes[1] & 0x02) |
                                 ((bytes[3] >> 3) & 0x1c) |
                                 ((bytes[12] << 4) & 0x20) |
                                 ((bytes[8] << 6) & 0x40);

   struct agx3_encoded_fmul_f32_reg_reg_long canonical;
   if (!agx3_pack_fmul_f32_reg_reg_long(
          encoding, decoded_dest, decoded_src0, decoded_src1, &canonical) ||
       memcmp(bytes, canonical.bytes, canonical.length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = canonical.length;
   return true;
}

bool
agx3_pack_fma_f32_reg_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t mul0,
   uint16_t mul1, uint16_t addend,
   struct agx3_encoded_fma_f32_reg_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || dest >= 128 ||
       mul0 >= 128 || mul1 >= 128 || addend >= 128 || (dest & 1) != 0 ||
       (mul0 & 1) != 0 || (mul1 & 1) != 0 || (addend & 1) != 0)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact f32fma.ep3 P3/L anonymous/context selection. */
      const uint8_t canonical[] = {
         0x09, 0x01, 0x1e, 0x01, 0x81, 0x00, 0x02, 0xc0,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2: d[1:4] -> b0[4:7], d[5:6] -> b2[6:7]. */
      out->bytes[0] |= (dest & 0x1e) << 3;
      out->bytes[2] |= (dest & 0x60) << 1;

      /* f35/f18/f52: operand[1:6] -> the matching byte[1:6]. */
      out->bytes[3] |= mul0 & 0x7e;
      out->bytes[1] |= mul1 & 0x7e;
      out->bytes[5] |= addend & 0x7e;

      out->length = sizeof(canonical);
      return true;
   }

   /* Exact f32fma.ep7 P3/L anonymous/context selection. */
   const uint8_t canonical[] = {
      0x29, 0x00, 0x07, 0x0a, 0x20, 0x20, 0xa3, 0x02,
      0x38, 0x00, 0xa0, 0x00, 0x00, 0x08, 0x00, 0x80,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d1 -> b0[4], d2..3 -> b7[3:4], d4 -> b0[7],
    *     d5 -> b7[5], d6 -> b2[7]. */
   out->bytes[0] |= (dest & 0x02) << 3;
   out->bytes[7] |= (dest & 0x0c) << 1;
   out->bytes[0] |= (dest & 0x10) << 3;
   out->bytes[7] |= dest & 0x20;
   out->bytes[2] |= (dest & 0x40) << 1;

   /* f35: m0[1] -> b8[7], m0[2:6] -> b9[0:4]. */
   out->bytes[8] |= (mul0 & 0x02) << 6;
   out->bytes[9] |= (mul0 & 0x7c) >> 2;

   /* f18 uses the exact AGX3c table permutation for m1[2:5]. */
   out->bytes[1] |= mul1 & 0x02;
   out->bytes[3] |= (mul1 & 0x04) << 2;
   out->bytes[3] |= (mul1 & 0x08) << 2;
   out->bytes[3] |= (mul1 & 0x10) << 3;
   out->bytes[3] |= (mul1 & 0x20) << 1;
   out->bytes[8] |= (mul1 & 0x40) >> 6;

   /* f52: a[1:5] -> b12[1:5], a[6] -> b10[0]. */
   out->bytes[12] |= addend & 0x3e;
   out->bytes[10] |= (addend & 0x40) >> 6;

   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_fma_f32_reg_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *mul0, uint16_t *mul1, uint16_t *addend,
   uint8_t *length)
{
   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 8 :
      encoding == AGX_ISA_ENCODING_AGX3C ? 16 : 0;
   if (expected_length == 0 || bytes == NULL || dest == NULL || mul0 == NULL ||
       mul1 == NULL || addend == NULL || length == NULL ||
       available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_mul0, decoded_mul1, decoded_addend;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = ((bytes[0] >> 3) & 0x1e) |
                     ((bytes[2] >> 1) & 0x60);
      decoded_mul0 = bytes[3] & 0x7e;
      decoded_mul1 = bytes[1] & 0x7e;
      decoded_addend = bytes[5] & 0x7e;
   } else {
      decoded_dest = ((bytes[0] >> 3) & 0x02) |
                     ((bytes[7] >> 1) & 0x0c) |
                     ((bytes[0] >> 3) & 0x10) | (bytes[7] & 0x20) |
                     ((bytes[2] >> 1) & 0x40);
      decoded_mul0 = ((bytes[8] >> 6) & 0x02) |
                     ((bytes[9] << 2) & 0x7c);
      decoded_mul1 = (bytes[1] & 0x02) |
                     ((bytes[3] >> 2) & 0x0c) |
                     ((bytes[3] >> 3) & 0x10) |
                     ((bytes[3] >> 1) & 0x20) |
                     ((bytes[8] << 6) & 0x40);
      decoded_addend = (bytes[12] & 0x3e) |
                       ((bytes[10] << 6) & 0x40);
   }

   struct agx3_encoded_fma_f32_reg_reg_reg canonical;
   if (!agx3_pack_fma_f32_reg_reg_reg(
          encoding, decoded_dest, decoded_mul0, decoded_mul1, decoded_addend,
          &canonical) ||
       memcmp(bytes, canonical.bytes, canonical.length) != 0)
      return false;

   *dest = decoded_dest;
   *mul0 = decoded_mul0;
   *mul1 = decoded_mul1;
   *addend = decoded_addend;
   *length = canonical.length;
   return true;
}

bool
agx3_pack_bit_and_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_bit_and_u32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || dest >= 256 ||
       src0 >= 256 || src1 >= 256 || (dest & 1) != 0 || (src0 & 1) != 0 ||
       (src1 & 1) != 0)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target Apple compiler replay. */
      const uint8_t canonical[AGX3_BIT_AND_U32_REG_REG_LENGTH] = {
         0x0b, 0x01, 0x0f, 0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2: d[1:4] -> b0[4:7], d[5:6] -> b2[6:7], d[7] -> b5[4]. */
      out->bytes[0] |= (dest & 0x1e) << 3;
      out->bytes[2] |= (dest & 0x60) << 1;
      out->bytes[5] |= (dest & 0x80) >> 3;

      /* f16: s0[1:6] -> b1[1:6], s0[7] -> b5[0]. */
      out->bytes[1] |= src0 & 0x7e;
      out->bytes[5] |= src0 >> 7;

      /* f30: s1[1:6] -> b3[1:6], s1[7] -> b5[2]. */
      out->bytes[3] |= src1 & 0x7e;
      out->bytes[5] |= (src1 & 0x80) >> 5;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S Apple compiler replay. */
   const uint8_t canonical[AGX3_BIT_AND_U32_REG_REG_LENGTH] = {
      0x2b, 0x00, 0x07, 0x80, 0x30, 0x30, 0xa0, 0x02, 0x04, 0x80,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2 uses the bounded eight-bit AGX3c destination scatter. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16 has generation-C-specific fragments in b1, b3, and b5. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[5] |= (src0 & 0x0c) << 4;
   out->bytes[3] |= (src0 & 0x10) >> 3;
   out->bytes[3] |= (src0 & 0xe0) >> 2;

   /* f30: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_bit_and_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL ||
       available < AGX3_BIT_AND_U32_REG_REG_LENGTH)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = ((bytes[0] >> 3) & 0x1e) |
                     ((bytes[2] >> 1) & 0x60) |
                     ((bytes[5] & 0x10) << 3);
      decoded_src0 = (bytes[1] & 0x7e) | ((bytes[5] & 0x1) << 7);
      decoded_src1 = (bytes[3] & 0x7e) | ((bytes[5] & 0x4) << 5);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[5] >> 6) & 0x3) << 2) |
                     (((bytes[3] >> 1) & 0x1) << 4) |
                     (((bytes[3] >> 3) & 0x7) << 5);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_bit_and_u32_reg_reg canonical;
   if (!agx3_pack_bit_and_u32_reg_reg(encoding, decoded_dest, decoded_src0,
                                      decoded_src1, &canonical) ||
       canonical.length != AGX3_BIT_AND_U32_REG_REG_LENGTH ||
       memcmp(bytes, canonical.bytes, canonical.length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = canonical.length;
   return true;
}

bool
agx3_pack_bit_or_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_bit_or_u32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || dest >= 256 ||
       src0 >= 256 || src1 >= 256 || (dest & 1) != 0 || (src0 & 1) != 0 ||
       (src1 & 1) != 0)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target Apple compiler replay. */
      const uint8_t canonical[AGX3_BIT_OR_U32_REG_REG_LENGTH] = {
         0x0b, 0x01, 0x0f, 0x01, 0x02, 0x08, 0x00, 0x80, 0x00, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2: d[1:4] -> b0[4:7], d[5:6] -> b2[6:7], d[7] -> b5[4]. */
      out->bytes[0] |= (dest & 0x1e) << 3;
      out->bytes[2] |= (dest & 0x60) << 1;
      out->bytes[5] |= (dest & 0x80) >> 3;

      /* f16: s0[1:6] -> b1[1:6], s0[7] -> b5[0]. */
      out->bytes[1] |= src0 & 0x7e;
      out->bytes[5] |= src0 >> 7;

      /* f30: s1[1:6] -> b3[1:6], s1[7] -> b5[2]. */
      out->bytes[3] |= src1 & 0x7e;
      out->bytes[5] |= (src1 & 0x80) >> 5;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S Apple compiler replay. */
   const uint8_t canonical[AGX3_BIT_OR_U32_REG_REG_LENGTH] = {
      0x2b, 0x00, 0x07, 0x80, 0x32, 0x38, 0xa0, 0x02, 0x04, 0x80,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2 uses the bounded eight-bit AGX3c destination scatter. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16 has generation-C-specific fragments in b1, b3, and b5. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[5] |= (src0 & 0x0c) << 4;
   out->bytes[3] |= (src0 & 0x10) >> 3;
   out->bytes[3] |= (src0 & 0xe0) >> 2;

   /* f30: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_bit_or_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL ||
       available < AGX3_BIT_OR_U32_REG_REG_LENGTH)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = ((bytes[0] >> 3) & 0x1e) |
                     ((bytes[2] >> 1) & 0x60) |
                     ((bytes[5] & 0x10) << 3);
      decoded_src0 = (bytes[1] & 0x7e) | ((bytes[5] & 0x1) << 7);
      decoded_src1 = (bytes[3] & 0x7e) | ((bytes[5] & 0x4) << 5);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[5] >> 6) & 0x3) << 2) |
                     (((bytes[3] >> 1) & 0x1) << 4) |
                     (((bytes[3] >> 3) & 0x7) << 5);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_bit_or_u32_reg_reg canonical;
   if (!agx3_pack_bit_or_u32_reg_reg(encoding, decoded_dest, decoded_src0,
                                     decoded_src1, &canonical) ||
       canonical.length != AGX3_BIT_OR_U32_REG_REG_LENGTH ||
       memcmp(bytes, canonical.bytes, canonical.length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = canonical.length;
   return true;
}

bool
agx3_pack_bit_xor_u32_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t src0,
   uint16_t src1, struct agx3_encoded_bit_xor_u32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || dest >= 256 ||
       src0 >= 256 || src1 >= 256 || (dest & 1) != 0 || (src0 & 1) != 0 ||
       (src1 & 1) != 0)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target Apple compiler replay. */
      const uint8_t canonical[AGX3_BIT_XOR_U32_REG_REG_LENGTH] = {
         0x0b, 0x01, 0x0e, 0x01, 0x02, 0x08, 0x00, 0x80, 0x00, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2: d[1:4] -> b0[4:7], d[5:6] -> b2[6:7], d[7] -> b5[4]. */
      out->bytes[0] |= (dest & 0x1e) << 3;
      out->bytes[2] |= (dest & 0x60) << 1;
      out->bytes[5] |= (dest & 0x80) >> 3;

      /* f16: s0[1:6] -> b1[1:6], s0[7] -> b5[0]. */
      out->bytes[1] |= src0 & 0x7e;
      out->bytes[5] |= src0 >> 7;

      /* f30: s1[1:6] -> b3[1:6], s1[7] -> b5[2]. */
      out->bytes[3] |= src1 & 0x7e;
      out->bytes[5] |= (src1 & 0x80) >> 5;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S Apple compiler replay. */
   const uint8_t canonical[AGX3_BIT_XOR_U32_REG_REG_LENGTH] = {
      0x2b, 0x00, 0x06, 0x80, 0x32, 0x38, 0xa0, 0x02, 0x04, 0x80,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2 uses the bounded eight-bit AGX3c destination scatter. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16 has generation-C-specific fragments in b1, b3, and b5. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[5] |= (src0 & 0x0c) << 4;
   out->bytes[3] |= (src0 & 0x10) >> 3;
   out->bytes[3] |= (src0 & 0xe0) >> 2;

   /* f30: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_bit_xor_u32_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *src0, uint16_t *src1, uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL ||
       available < AGX3_BIT_XOR_U32_REG_REG_LENGTH)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = ((bytes[0] >> 3) & 0x1e) |
                     ((bytes[2] >> 1) & 0x60) |
                     ((bytes[5] & 0x10) << 3);
      decoded_src0 = (bytes[1] & 0x7e) | ((bytes[5] & 0x1) << 7);
      decoded_src1 = (bytes[3] & 0x7e) | ((bytes[5] & 0x4) << 5);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[5] >> 6) & 0x3) << 2) |
                     (((bytes[3] >> 1) & 0x1) << 4) |
                     (((bytes[3] >> 3) & 0x7) << 5);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_bit_xor_u32_reg_reg canonical;
   if (!agx3_pack_bit_xor_u32_reg_reg(encoding, decoded_dest, decoded_src0,
                                      decoded_src1, &canonical) ||
       canonical.length != AGX3_BIT_XOR_U32_REG_REG_LENGTH ||
       memcmp(bytes, canonical.bytes, canonical.length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = canonical.length;
   return true;
}

bool
agx3_pack_bit_not_u32_reg(enum agx_isa_encoding encoding, uint16_t dest,
                          uint16_t src,
                          struct agx3_encoded_bit_not_u32_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || dest >= 256 ||
       src >= 256 || (dest & 1) != 0 || (src & 1) != 0)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target forced-live replay. */
      const uint8_t canonical[AGX3_BIT_NOT_U32_REG_MAX_LENGTH] = {
         0x0b, 0x01, 0x26, 0x00, 0x01, 0x08, 0x00, 0x80, 0x00, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2: d[1:4] -> b0[4:7], d[5:6] -> b2[6:7], d[7] -> b5[4]. */
      out->bytes[0] |= (dest & 0x1e) << 3;
      out->bytes[2] |= (dest & 0x60) << 1;
      out->bytes[5] |= (dest & 0x80) >> 3;

      /* f16: s[1:6] -> b1[1:6], s[7] -> b5[0]. */
      out->bytes[1] |= src & 0x7e;
      out->bytes[5] |= src >> 7;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S forced-live replay. */
   const uint8_t canonical[] = {
      0x2b, 0x00, 0x06, 0x1a, 0x29, 0x08, 0xa8, 0x02,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2 uses the bounded eight-bit AGX3c destination scatter. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16: s[1] in b1, s[2:4] in b3, s[5] in b5, s[6:7] in b6. */
   out->bytes[1] |= src & 0x2;
   out->bytes[3] |= (src & 0x1c) << 3;
   out->bytes[5] |= (src & 0x20) << 2;
   out->bytes[6] |= (src & 0xc0) >> 6;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_bit_not_u32_reg(enum agx_isa_encoding encoding,
                            const uint8_t *bytes, size_t available,
                            uint16_t *dest, uint16_t *src, uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src == NULL || length == NULL)
      return false;

   const uint8_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 10 : 8;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_src;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = ((bytes[0] >> 3) & 0x1e) |
                     ((bytes[2] >> 1) & 0x60) |
                     ((bytes[5] & 0x10) << 3);
      decoded_src = (bytes[1] & 0x7e) | ((bytes[5] & 0x1) << 7);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src = (bytes[1] & 0x2) |
                    ((bytes[3] & 0xe0) >> 3) |
                    ((bytes[5] & 0x80) >> 2) |
                    ((bytes[6] & 0x3) << 6);
   }

   struct agx3_encoded_bit_not_u32_reg canonical;
   if (!agx3_pack_bit_not_u32_reg(encoding, decoded_dest, decoded_src,
                                  &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *src = decoded_src;
   *length = expected_length;
   return true;
}

bool
agx3_pack_isub_u32_reg_reg(enum agx_isa_encoding encoding, uint16_t dest,
                           uint16_t src0, uint16_t src1,
                           struct agx3_encoded_isub_u32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || (dest & 1) != 0 ||
       (src0 & 1) != 0 || (src1 & 1) != 0)
      return false;

   const uint16_t register_limit =
      encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;
   if (dest >= register_limit || src0 >= register_limit ||
       src1 >= register_limit)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target forced-live replay. */
      const uint8_t canonical[] = {
         0x1f, 0x01, 0x56, 0x00, 0x02, 0x00, 0x00, 0xa8, 0x11, 0x05,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2 is contiguous at b3 for this AGX3b encoding point. */
      out->bytes[3] = dest;

      /* f16: s0[1:6] -> b5[2:7], s0[7] -> b6[0]. */
      out->bytes[5] |= (src0 & 0x7e) << 1;
      out->bytes[6] |= src0 >> 7;

      /* f31: s1[1:5] -> b6[3:7], s1[6:7] -> b7[0:1]. */
      out->bytes[6] |= (src1 & 0x3e) << 2;
      out->bytes[7] |= src1 >> 6;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S forced-live replay. */
   const uint8_t canonical[] = {
      0x2f, 0x00, 0x04, 0x1a, 0x21, 0x00,
      0xa2, 0x02, 0x08, 0x80, 0x01, 0x00,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1,4] -> b0[4,7], d[2:3,5] -> b7[3:5], d[6:7] -> b2[7,3]. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16: s0[1], s0[2:4], s0[5], s0[6:7] use four byte fragments. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[3] |= (src0 & 0x1c) << 3;
   out->bytes[5] |= (src0 & 0x20) << 2;
   out->bytes[8] |= (src0 >> 6) & 0x3;

   /* f31: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_isub_u32_reg_reg(enum agx_isa_encoding encoding,
                             const uint8_t *bytes, size_t available,
                             uint16_t *dest, uint16_t *src0, uint16_t *src1,
                             uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL)
      return false;

   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 10 : 12;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = bytes[3];
      decoded_src0 = ((bytes[5] >> 1) & 0x7e) |
                     ((bytes[6] & 0x1) << 7);
      decoded_src1 = ((bytes[6] >> 2) & 0x3e) |
                     ((bytes[7] & 0x3) << 6);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[3] >> 5) & 0x7) << 2) |
                     (((bytes[5] >> 7) & 0x1) << 5) |
                     ((bytes[8] & 0x3) << 6);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_isub_u32_reg_reg canonical;
   if (!agx3_pack_isub_u32_reg_reg(encoding, decoded_dest, decoded_src0,
                                   decoded_src1, &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = expected_length;
   return true;
}

bool
agx3_pack_sll_u32_reg_reg(enum agx_isa_encoding encoding, uint16_t dest,
                          uint16_t src0, uint16_t src1,
                          struct agx3_encoded_sll_u32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || (dest & 1) != 0 ||
       (src0 & 1) != 0 || (src1 & 1) != 0)
      return false;

   const uint16_t register_limit =
      encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;
   if (dest >= register_limit || src0 >= register_limit ||
       src1 >= register_limit)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target forced-live replay. */
      const uint8_t canonical[] = {
         0x27, 0x10, 0x54, 0x00, 0x02, 0x00,
         0x00, 0x00, 0xf0, 0x10, 0x05, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2 is contiguous at b3 for this AGX3b encoding point. */
      out->bytes[3] = dest;

      /* f16: s0[1:6] -> b5[2:7], s0[7] -> b6[0]. */
      out->bytes[5] |= (src0 & 0x7e) << 1;
      out->bytes[6] |= src0 >> 7;

      /* f31: s1[1:5] -> b6[3:7], s1[6:7] -> b7[0:1]. */
      out->bytes[6] |= (src1 & 0x3e) << 2;
      out->bytes[7] |= src1 >> 6;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S forced-live replay. */
   const uint8_t canonical[] = {
      0x27, 0x04, 0x00, 0x1a, 0x22, 0x00, 0xa0,
      0x02, 0x08, 0x80, 0x00, 0x34, 0x00, 0x00,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1,4] -> b0[4,7], d[2:3,5] -> b7[3:5], d[6:7] -> b2[7,3]. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16: s0[1], s0[2:4], s0[5], s0[6:7] use four byte fragments. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[3] |= (src0 & 0x1c) << 3;
   out->bytes[5] |= (src0 & 0x20) << 2;
   out->bytes[8] |= (src0 >> 6) & 0x3;

   /* f31: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_sll_u32_reg_reg(enum agx_isa_encoding encoding,
                            const uint8_t *bytes, size_t available,
                            uint16_t *dest, uint16_t *src0, uint16_t *src1,
                            uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL)
      return false;

   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 12 : 14;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = bytes[3];
      decoded_src0 = ((bytes[5] >> 1) & 0x7e) |
                     ((bytes[6] & 0x1) << 7);
      decoded_src1 = ((bytes[6] >> 2) & 0x3e) |
                     ((bytes[7] & 0x3) << 6);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[3] >> 5) & 0x7) << 2) |
                     (((bytes[5] >> 7) & 0x1) << 5) |
                     ((bytes[8] & 0x3) << 6);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_sll_u32_reg_reg canonical;
   if (!agx3_pack_sll_u32_reg_reg(encoding, decoded_dest, decoded_src0,
                                  decoded_src1, &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = expected_length;
   return true;
}

bool
agx3_pack_asr_i32_reg_reg(enum agx_isa_encoding encoding, uint16_t dest,
                          uint16_t src0, uint16_t src1,
                          struct agx3_encoded_asr_i32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || (dest & 1) != 0 ||
       (src0 & 1) != 0 || (src1 & 1) != 0)
      return false;

   const uint16_t register_limit =
      encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;
   if (dest >= register_limit || src0 >= register_limit ||
       src1 >= register_limit)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target forced-live replay. */
      const uint8_t canonical[] = {
         0xa7, 0x11, 0x54, 0x00, 0x02, 0x00, 0x00, 0x18, 0xe2, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2 is contiguous at b3 for this AGX3b encoding point. */
      out->bytes[3] = dest;

      /* f16: s0[1:6] -> b5[2:7], s0[7] -> b6[0]. */
      out->bytes[5] |= (src0 & 0x7e) << 1;
      out->bytes[6] |= src0 >> 7;

      /* f31: s1[1:5] -> b6[3:7], s1[6:7] -> b7[0:1]. */
      out->bytes[6] |= (src1 & 0x3e) << 2;
      out->bytes[7] |= src1 >> 6;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S forced-live replay. */
   const uint8_t canonical[] = {
      0x27, 0x04, 0x00, 0x1a, 0x22, 0x00,
      0xa3, 0x02, 0x08, 0x00, 0x40, 0x00,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1,4] -> b0[4,7], d[2:3,5] -> b7[3:5], d[6:7] -> b2[7,3]. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16: s0[1], s0[2:4], s0[5], s0[6:7] use four byte fragments. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[3] |= (src0 & 0x1c) << 3;
   out->bytes[5] |= (src0 & 0x20) << 2;
   out->bytes[8] |= (src0 >> 6) & 0x3;

   /* f31: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_asr_i32_reg_reg(enum agx_isa_encoding encoding,
                            const uint8_t *bytes, size_t available,
                            uint16_t *dest, uint16_t *src0, uint16_t *src1,
                            uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL)
      return false;

   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 10 : 12;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = bytes[3];
      decoded_src0 = ((bytes[5] >> 1) & 0x7e) |
                     ((bytes[6] & 0x1) << 7);
      decoded_src1 = ((bytes[6] >> 2) & 0x3e) |
                     ((bytes[7] & 0x3) << 6);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[3] >> 5) & 0x7) << 2) |
                     (((bytes[5] >> 7) & 0x1) << 5) |
                     ((bytes[8] & 0x3) << 6);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_asr_i32_reg_reg canonical;
   if (!agx3_pack_asr_i32_reg_reg(encoding, decoded_dest, decoded_src0,
                                  decoded_src1, &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = expected_length;
   return true;
}

bool
agx3_pack_lsr_u32_reg_reg(enum agx_isa_encoding encoding, uint16_t dest,
                          uint16_t src0, uint16_t src1,
                          struct agx3_encoded_lsr_u32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || (dest & 1) != 0 ||
       (src0 & 1) != 0 || (src1 & 1) != 0)
      return false;

   const uint16_t register_limit =
      encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;
   if (dest >= register_limit || src0 >= register_limit ||
       src1 >= register_limit)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact canonical fields from the seven-target forced-live replay. */
      const uint8_t canonical[] = {
         0xa7, 0x10, 0x54, 0x00, 0x02, 0x00,
         0x00, 0x00, 0xf0, 0x10, 0x05, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2 is contiguous at b3 for this AGX3b encoding point. */
      out->bytes[3] = dest;

      /* f16: s0[1:6] -> b5[2:7], s0[7] -> b6[0]. */
      out->bytes[5] |= (src0 & 0x7e) << 1;
      out->bytes[6] |= src0 >> 7;

      /* f31: s1[1:5] -> b6[3:7], s1[6:7] -> b7[0:1]. */
      out->bytes[6] |= (src1 & 0x3e) << 2;
      out->bytes[7] |= src1 >> 6;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact canonical fields from the G17G/G17S forced-live replay. */
   const uint8_t canonical[] = {
      0x27, 0x04, 0x00, 0x1a, 0x22, 0x00, 0xa1,
      0x02, 0x08, 0x80, 0x00, 0x34, 0x00, 0x00,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1,4] -> b0[4,7], d[2:3,5] -> b7[3:5], d[6:7] -> b2[7,3]. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16: s0[1], s0[2:4], s0[5], s0[6:7] use four byte fragments. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[3] |= (src0 & 0x1c) << 3;
   out->bytes[5] |= (src0 & 0x20) << 2;
   out->bytes[8] |= (src0 >> 6) & 0x3;

   /* f31: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_lsr_u32_reg_reg(enum agx_isa_encoding encoding,
                            const uint8_t *bytes, size_t available,
                            uint16_t *dest, uint16_t *src0, uint16_t *src1,
                            uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL)
      return false;

   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 12 : 14;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = bytes[3];
      decoded_src0 = ((bytes[5] >> 1) & 0x7e) |
                     ((bytes[6] & 0x1) << 7);
      decoded_src1 = ((bytes[6] >> 2) & 0x3e) |
                     ((bytes[7] & 0x3) << 6);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[3] >> 5) & 0x7) << 2) |
                     (((bytes[5] >> 7) & 0x1) << 5) |
                     ((bytes[8] & 0x3) << 6);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_lsr_u32_reg_reg canonical;
   if (!agx3_pack_lsr_u32_reg_reg(encoding, decoded_dest, decoded_src0,
                                  decoded_src1, &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = expected_length;
   return true;
}

bool
agx3_pack_imul_u32_reg_reg(enum agx_isa_encoding encoding, uint16_t dest,
                           uint16_t src0, uint16_t src1,
                           struct agx3_encoded_imul_u32_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || (dest & 1) != 0 ||
       (src0 & 1) != 0 || (src1 & 1) != 0)
      return false;

   const uint16_t register_limit =
      encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;
   if (dest >= register_limit || src0 >= register_limit ||
       src1 >= register_limit)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact implicit-zero addend fields from seven forced-live targets. */
      const uint8_t canonical[] = {
         0x9f, 0x00, 0x56, 0x00, 0x02, 0x00,
         0x00, 0x00, 0xd0, 0x20, 0x0a, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2 is contiguous at b3 for this AGX3b encoding point. */
      out->bytes[3] = dest;

      /* f16: s0[1:6] -> b5[2:7], s0[7] -> b6[0]. */
      out->bytes[5] |= (src0 & 0x7e) << 1;
      out->bytes[6] |= src0 >> 7;

      /* f31: s1[1:5] -> b6[3:7], s1[6:7] -> b7[0:1]. */
      out->bytes[6] |= (src1 & 0x3e) << 2;
      out->bytes[7] |= src1 >> 6;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact implicit-zero addend fields from the G17G/G17S replay. */
   const uint8_t canonical[] = {
      0x2f, 0x00, 0x04, 0x1a, 0x21, 0x00, 0xa1,
      0x02, 0x08, 0x80, 0x00, 0x00, 0x01, 0x02,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1,4] -> b0[4,7], d[2:3,5] -> b7[3:5], d[6:7] -> b2[7,3]. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16: s0[1], s0[2:4], s0[5], s0[6:7] use four byte fragments. */
   out->bytes[1] |= src0 & 0x2;
   out->bytes[3] |= (src0 & 0x1c) << 3;
   out->bytes[5] |= (src0 & 0x20) << 2;
   out->bytes[8] |= (src0 >> 6) & 0x3;

   /* f31: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((src1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (src1 >> 2) & 0x3f;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_imul_u32_reg_reg(enum agx_isa_encoding encoding,
                             const uint8_t *bytes, size_t available,
                             uint16_t *dest, uint16_t *src0, uint16_t *src1,
                             uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       src0 == NULL || src1 == NULL || length == NULL)
      return false;

   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 12 : 14;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_src0, decoded_src1;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = bytes[3];
      decoded_src0 = ((bytes[5] >> 1) & 0x7e) |
                     ((bytes[6] & 0x1) << 7);
      decoded_src1 = ((bytes[6] >> 2) & 0x3e) |
                     ((bytes[7] & 0x3) << 6);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_src0 = (bytes[1] & 0x2) |
                     (((bytes[3] >> 5) & 0x7) << 2) |
                     (((bytes[5] >> 7) & 0x1) << 5) |
                     ((bytes[8] & 0x3) << 6);
      decoded_src1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
   }

   struct agx3_encoded_imul_u32_reg_reg canonical;
   if (!agx3_pack_imul_u32_reg_reg(encoding, decoded_dest, decoded_src0,
                                   decoded_src1, &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *src0 = decoded_src0;
   *src1 = decoded_src1;
   *length = expected_length;
   return true;
}

bool
agx3_pack_imadd_u32_reg_reg_reg(
   enum agx_isa_encoding encoding, uint16_t dest, uint16_t mul0,
   uint16_t mul1, uint16_t addend,
   struct agx3_encoded_imadd_u32_reg_reg_reg *out)
{
   if (out == NULL || !is_agx3_encoding(encoding) || (dest & 1) != 0 ||
       (mul0 & 1) != 0 || (mul1 & 1) != 0 || (addend & 1) != 0)
      return false;

   const uint16_t register_limit =
      encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256;
   if (dest >= register_limit || mul0 >= register_limit ||
       mul1 >= register_limit || addend >= register_limit)
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      /* Exact real-register addend fields from seven forced-live targets. */
      const uint8_t canonical[] = {
         0x9f, 0x00, 0x56, 0x00, 0x02, 0x00,
         0x00, 0x00, 0xd0, 0x21, 0x2a, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2 is contiguous at b3 for this AGX3b encoding point. */
      out->bytes[3] = dest;

      /* f16: s0[1:6] -> b5[2:7], s0[7] -> b6[0]. */
      out->bytes[5] |= (mul0 & 0x7e) << 1;
      out->bytes[6] |= mul0 >> 7;

      /* f31: s1[1:5] -> b6[3:7], s1[6:7] -> b7[0:1]. */
      out->bytes[6] |= (mul1 & 0x3e) << 2;
      out->bytes[7] |= mul1 >> 6;

      /* f46: add[1:4] -> b7[4:7], add[5:7] -> b8[0:2]. */
      out->bytes[7] |= (addend & 0x1e) << 3;
      out->bytes[8] |= addend >> 5;
      out->length = sizeof(canonical);
      return true;
   }

   /* Exact real-register addend fields from the G17G/G17S replay. */
   const uint8_t canonical[] = {
      0x2f, 0x00, 0x04, 0x1a, 0x21, 0x00, 0xa1,
      0x02, 0x08, 0x80, 0x40, 0x00, 0x00, 0x00,
   };
   memcpy(out->bytes, canonical, sizeof(canonical));

   /* f2: d[1,4] -> b0[4,7], d[2:3,5] -> b7[3:5], d[6:7] -> b2[7,3]. */
   out->bytes[0] |= ((dest >> 1) & 0x1) << 4;
   out->bytes[0] |= ((dest >> 4) & 0x1) << 7;
   out->bytes[7] |= ((dest >> 2) & 0x3) << 3;
   out->bytes[7] |= ((dest >> 5) & 0x1) << 5;
   out->bytes[2] |= ((dest >> 6) & 0x1) << 7;
   out->bytes[2] |= ((dest >> 7) & 0x1) << 3;

   /* f16: s0[1], s0[2:4], s0[5], s0[6:7] use four byte fragments. */
   out->bytes[1] |= mul0 & 0x2;
   out->bytes[3] |= (mul0 & 0x1c) << 3;
   out->bytes[5] |= (mul0 & 0x20) << 2;
   out->bytes[8] |= (mul0 >> 6) & 0x3;

   /* f31: s1[1] -> b8[7], s1[2:7] -> b9[0:5]. */
   out->bytes[8] |= ((mul1 >> 1) & 0x1) << 7;
   out->bytes[9] |= (mul1 >> 2) & 0x3f;

   /* f46: add[1:3,5,7] -> b12; add[4] -> b8; add[6] -> b10. */
   out->bytes[8] |= addend & 0x10;
   out->bytes[10] |= (addend >> 6) & 0x1;
   out->bytes[12] |= addend & 0xae;
   out->length = sizeof(canonical);
   return true;
}

bool
agx3_unpack_imadd_u32_reg_reg_reg(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint16_t *dest, uint16_t *mul0, uint16_t *mul1, uint16_t *addend,
   uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || dest == NULL ||
       mul0 == NULL || mul1 == NULL || addend == NULL || length == NULL)
      return false;

   const size_t expected_length =
      encoding == AGX_ISA_ENCODING_AGX3B ? 12 : 14;
   if (available < expected_length)
      return false;

   uint16_t decoded_dest, decoded_mul0, decoded_mul1, decoded_addend;
   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      decoded_dest = bytes[3];
      decoded_mul0 = ((bytes[5] >> 1) & 0x7e) |
                     ((bytes[6] & 0x1) << 7);
      decoded_mul1 = ((bytes[6] >> 2) & 0x3e) |
                     ((bytes[7] & 0x3) << 6);
      decoded_addend = ((bytes[7] >> 3) & 0x1e) |
                       ((bytes[8] & 0x7) << 5);
   } else {
      decoded_dest = (((bytes[0] >> 4) & 0x1) << 1) |
                     (((bytes[7] >> 3) & 0x3) << 2) |
                     (((bytes[0] >> 7) & 0x1) << 4) |
                     (((bytes[7] >> 5) & 0x1) << 5) |
                     (((bytes[2] >> 7) & 0x1) << 6) |
                     (((bytes[2] >> 3) & 0x1) << 7);
      decoded_mul0 = (bytes[1] & 0x2) |
                     (((bytes[3] >> 5) & 0x7) << 2) |
                     (((bytes[5] >> 7) & 0x1) << 5) |
                     ((bytes[8] & 0x3) << 6);
      decoded_mul1 = (((bytes[8] >> 7) & 0x1) << 1) |
                     ((bytes[9] & 0x3f) << 2);
      decoded_addend = (bytes[8] & 0x10) |
                       ((bytes[10] & 0x1) << 6) |
                       (bytes[12] & 0xae);
   }

   struct agx3_encoded_imadd_u32_reg_reg_reg canonical;
   if (!agx3_pack_imadd_u32_reg_reg_reg(
          encoding, decoded_dest, decoded_mul0, decoded_mul1, decoded_addend,
          &canonical) ||
       canonical.length != expected_length ||
       memcmp(bytes, canonical.bytes, expected_length) != 0)
      return false;

   *dest = decoded_dest;
   *mul0 = decoded_mul0;
   *mul1 = decoded_mul1;
   *addend = decoded_addend;
   *length = expected_length;
   return true;
}

bool
agx3_pack_yield_i16_reg(enum agx_isa_encoding encoding, uint16_t src,
                        struct agx3_encoded_yield_i16_reg *out)
{
   if (!is_agx3_encoding(encoding) || out == NULL ||
       src >= (encoding == AGX_ISA_ENCODING_AGX3B ? 192 : 256))
      return false;

   memset(out, 0, sizeof(*out));

   if (encoding == AGX_ISA_ENCODING_AGX3B) {
      const uint8_t canonical[AGX3_YIELD_I16_REG_LENGTH] = {
         0x87, 0x04, 0x54, 0x00, 0x04, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));
      out->bytes[3] = src;
   } else {
      const uint8_t canonical[AGX3_YIELD_I16_REG_LENGTH] = {
         0x0f, 0x00, 0x00, 0x02, 0x00, 0x00,
      };
      memcpy(out->bytes, canonical, sizeof(canonical));

      /* f2[0:6] -> w0[8:14], f2[7] -> w1[4].  The table also
       * contains f2[8], but that register space is deliberately closed. */
      out->bytes[1] = src & 0x7f;
      out->bytes[2] = (src >> 3) & 0x10;
   }

   out->length = AGX3_YIELD_I16_REG_LENGTH;
   return true;
}

bool
agx3_unpack_yield_i16_reg(enum agx_isa_encoding encoding,
                          const uint8_t *bytes, size_t available,
                          uint16_t *src, uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL || src == NULL ||
       length == NULL || available < AGX3_YIELD_I16_REG_LENGTH)
      return false;

   const uint16_t decoded_src = encoding == AGX_ISA_ENCODING_AGX3B
                                   ? bytes[3]
                                   : (bytes[1] & 0x7f) |
                                        ((bytes[2] & 0x10) << 3);
   struct agx3_encoded_yield_i16_reg canonical;
   if (!agx3_pack_yield_i16_reg(encoding, decoded_src, &canonical) ||
       memcmp(bytes, canonical.bytes, AGX3_YIELD_I16_REG_LENGTH) != 0)
      return false;

   *src = decoded_src;
   *length = AGX3_YIELD_I16_REG_LENGTH;
   return true;
}

bool
agx3_pack_nop(enum agx_isa_encoding encoding, struct agx3_encoded_nop *out)
{
   if (!is_agx3_encoding(encoding) || out == NULL)
      return false;

   memset(out, 0, sizeof(*out));
   out->bytes[0] = 0x06;
   out->length = AGX3_NOP_LENGTH;
   return true;
}

bool
agx3_unpack_nop(enum agx_isa_encoding encoding, const uint8_t *bytes,
                size_t available, uint8_t *length)
{
   static const uint8_t canonical[AGX3_NOP_LENGTH] = {0x06, 0x00};

   if (!is_agx3_encoding(encoding) || bytes == NULL || length == NULL ||
       available < AGX3_NOP_LENGTH ||
       memcmp(bytes, canonical, sizeof(canonical)) != 0)
      return false;

   *length = AGX3_NOP_LENGTH;
   return true;
}

bool
agx3_pack_threadgroup_barrier(
   enum agx_isa_encoding encoding,
   struct agx3_encoded_threadgroup_barrier *out)
{
   static const uint8_t canonical_b[AGX3_THREADGROUP_BARRIER_LENGTH] = {
      0x07, 0x04, 0x54, 0x61, 0x09, 0x00,
   };
   static const uint8_t canonical_c[AGX3_THREADGROUP_BARRIER_LENGTH] = {
      0x27, 0x51, 0x00, 0x06, 0x00, 0x00,
   };
   const uint8_t *canonical = encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b :
                              encoding == AGX_ISA_ENCODING_AGX3C ? canonical_c :
                                                                   NULL;

   if (canonical == NULL || out == NULL)
      return false;

   memset(out, 0, sizeof(*out));
   memcpy(out->bytes, canonical, AGX3_THREADGROUP_BARRIER_LENGTH);
   out->length = AGX3_THREADGROUP_BARRIER_LENGTH;
   return true;
}

bool
agx3_unpack_threadgroup_barrier(enum agx_isa_encoding encoding,
                                const uint8_t *bytes, size_t available,
                                uint8_t *length)
{
   static const uint8_t canonical_b[AGX3_THREADGROUP_BARRIER_LENGTH] = {
      0x07, 0x04, 0x54, 0x61, 0x09, 0x00,
   };
   static const uint8_t canonical_c[AGX3_THREADGROUP_BARRIER_LENGTH] = {
      0x27, 0x51, 0x00, 0x06, 0x00, 0x00,
   };
   const uint8_t *canonical = encoding == AGX_ISA_ENCODING_AGX3B ? canonical_b :
                              encoding == AGX_ISA_ENCODING_AGX3C ? canonical_c :
                                                                   NULL;

   if (canonical == NULL || bytes == NULL || length == NULL ||
       available < AGX3_THREADGROUP_BARRIER_LENGTH ||
       memcmp(bytes, canonical, AGX3_THREADGROUP_BARRIER_LENGTH) != 0)
      return false;

   *length = AGX3_THREADGROUP_BARRIER_LENGTH;
   return true;
}

bool
agx3_pack_end(enum agx_isa_encoding encoding, struct agx3_encoded_end *out)
{
   if (!is_agx3_encoding(encoding) || out == NULL)
      return false;

   memset(out, 0, sizeof(*out));
   out->bytes[0] = 0x0e;
   out->length = AGX3_END_LENGTH;
   return true;
}

bool
agx3_unpack_end(enum agx_isa_encoding encoding, const uint8_t *bytes,
                size_t available, uint8_t *length)
{
   static const uint8_t canonical[AGX3_END_LENGTH] = {0x0e, 0x00, 0x00,
                                                       0x00};

   if (!is_agx3_encoding(encoding) || bytes == NULL || length == NULL ||
       available < AGX3_END_LENGTH ||
       memcmp(bytes, canonical, sizeof(canonical)) != 0)
      return false;

   *length = AGX3_END_LENGTH;
   return true;
}

bool
agx3_pack_end_with_wait_fence_mask(enum agx_isa_encoding encoding,
                                   uint8_t wait_fence_mask,
                                   struct agx3_encoded_end *out)
{
   if (!is_agx3_encoding(encoding) || out == NULL ||
       (encoding == AGX_ISA_ENCODING_AGX3B && (wait_fence_mask & 0xc0)))
      return false;

   memset(out, 0, sizeof(*out));
   out->bytes[0] = 0x0e;
   out->bytes[1] = (wait_fence_mask & 0x3f) << 2;
   out->bytes[2] = ((wait_fence_mask >> 6) & 0x03) << 1;
   out->length = AGX3_END_LENGTH;
   return true;
}

bool
agx3_unpack_end_with_wait_fence_mask(enum agx_isa_encoding encoding,
                                     const uint8_t *bytes, size_t available,
                                     uint8_t *wait_fence_mask,
                                     uint8_t *length)
{
   if (!is_agx3_encoding(encoding) || bytes == NULL ||
       wait_fence_mask == NULL || length == NULL ||
       available < AGX3_END_LENGTH)
      return false;

   const uint8_t decoded_mask =
      ((bytes[1] >> 2) & 0x3f) | ((bytes[2] & 0x06) << 5);
   struct agx3_encoded_end canonical;
   if (!agx3_pack_end_with_wait_fence_mask(encoding, decoded_mask,
                                           &canonical) ||
       memcmp(bytes, canonical.bytes, AGX3_END_LENGTH) != 0)
      return false;

   *wait_fence_mask = decoded_mask;
   *length = AGX3_END_LENGTH;
   return true;
}

static const uint8_t enable_all_threads_agx3b[] = {
   0x6f, 0x23, 0x54, 0x00, 0x00, 0x12,
};

static const uint8_t enable_all_threads_agx3c[] = {
   0x07, 0x07, 0x05, 0x44, 0x03, 0x00, 0x0b, 0x00,
};

static const uint8_t *
enable_all_threads_encoding(enum agx_isa_encoding encoding, size_t *length)
{
   switch (encoding) {
   case AGX_ISA_ENCODING_AGX3B:
      *length = sizeof(enable_all_threads_agx3b);
      return enable_all_threads_agx3b;
   case AGX_ISA_ENCODING_AGX3C:
      *length = sizeof(enable_all_threads_agx3c);
      return enable_all_threads_agx3c;
   default:
      *length = 0;
      return NULL;
   }
}

bool
agx3_pack_enable_all_threads(
   enum agx_isa_encoding encoding,
   struct agx3_encoded_enable_all_threads *out)
{
   size_t length = 0;
   const uint8_t *canonical =
      enable_all_threads_encoding(encoding, &length);

   if (canonical == NULL || out == NULL)
      return false;

   memset(out, 0, sizeof(*out));
   memcpy(out->bytes, canonical, length);
   out->length = length;
   return true;
}

bool
agx3_unpack_enable_all_threads(enum agx_isa_encoding encoding,
                               const uint8_t *bytes, size_t available,
                               uint8_t *length)
{
   size_t canonical_length = 0;
   const uint8_t *canonical =
      enable_all_threads_encoding(encoding, &canonical_length);

   if (canonical == NULL || bytes == NULL || length == NULL ||
       available < canonical_length ||
       memcmp(bytes, canonical, canonical_length) != 0)
      return false;

   *length = canonical_length;
   return true;
}

static const uint8_t update_node_agx3b[] = {
   0x07, 0x04, 0x54, 0x08, 0x09, 0x00, 0xdf, 0x05, 0x54, 0x00,
};

static const uint8_t update_node_agx3c[] = {
   0x17, 0x09, 0x00, 0x06, 0x00, 0x00, 0x27,
   0x00, 0x01, 0x00, 0x22, 0x00, 0xad, 0x02,
};

static const uint8_t *
update_node_encoding(enum agx_isa_encoding encoding, size_t *length)
{
   switch (encoding) {
   case AGX_ISA_ENCODING_AGX3B:
      *length = sizeof(update_node_agx3b);
      return update_node_agx3b;
   case AGX_ISA_ENCODING_AGX3C:
      *length = sizeof(update_node_agx3c);
      return update_node_agx3c;
   default:
      *length = 0;
      return NULL;
   }
}

bool
agx3_pack_update_node(enum agx_isa_encoding encoding,
                      struct agx3_encoded_update_node *out)
{
   size_t length = 0;
   const uint8_t *canonical = update_node_encoding(encoding, &length);

   if (canonical == NULL || out == NULL)
      return false;

   memset(out, 0, sizeof(*out));
   memcpy(out->bytes, canonical, length);
   out->length = length;
   return true;
}

bool
agx3_unpack_update_node(enum agx_isa_encoding encoding, const uint8_t *bytes,
                        size_t available, uint8_t *length)
{
   size_t canonical_length = 0;
   const uint8_t *canonical =
      update_node_encoding(encoding, &canonical_length);

   if (canonical == NULL || bytes == NULL || length == NULL ||
       available < canonical_length ||
       memcmp(bytes, canonical, canonical_length) != 0)
      return false;

   *length = canonical_length;
   return true;
}

static const uint8_t continue_ray_fixed_first_site_witness_agx3b[] = {
   0x5f, 0x11, 0x54, 0x00, 0x00, 0x00,
};

static const uint8_t continue_ray_fixed_first_site_witness_agx3c[] = {
   0x27, 0x04, 0x09, 0x00, 0x22, 0xc0, 0xaf, 0x02,
};

static const uint8_t *
continue_ray_fixed_first_site_witness_encoding(
   enum agx_isa_encoding encoding, size_t *length)
{
   switch (encoding) {
   case AGX_ISA_ENCODING_AGX3B:
      *length = sizeof(continue_ray_fixed_first_site_witness_agx3b);
      return continue_ray_fixed_first_site_witness_agx3b;
   case AGX_ISA_ENCODING_AGX3C:
      *length = sizeof(continue_ray_fixed_first_site_witness_agx3c);
      return continue_ray_fixed_first_site_witness_agx3c;
   default:
      *length = 0;
      return NULL;
   }
}

bool
agx3_pack_continue_ray_fixed_first_site_witness(
   enum agx_isa_encoding encoding,
   struct agx3_encoded_continue_ray_fixed_first_site_witness *out)
{
   size_t length = 0;
   const uint8_t *canonical =
      continue_ray_fixed_first_site_witness_encoding(encoding, &length);

   if (canonical == NULL || out == NULL)
      return false;

   memset(out, 0, sizeof(*out));
   memcpy(out->bytes, canonical, length);
   out->length = length;
   return true;
}

bool
agx3_unpack_continue_ray_fixed_first_site_witness(
   enum agx_isa_encoding encoding, const uint8_t *bytes, size_t available,
   uint8_t *length)
{
   size_t canonical_length = 0;
   const uint8_t *canonical =
      continue_ray_fixed_first_site_witness_encoding(encoding,
                                                      &canonical_length);

   if (canonical == NULL || bytes == NULL || length == NULL ||
       available < canonical_length ||
       memcmp(bytes, canonical, canonical_length) != 0)
      return false;

   *length = canonical_length;
   return true;
}

static const uint8_t
   update_bvh_fixed_witness_agx3c[AGX3_UPDATE_BVH_FIXED_WITNESS_LENGTH] = {
      /* barrier */
      0x17, 0x09, 0x00, 0x06, 0x00, 0x00,
      /* update_bvh */
      0x27, 0x00, 0x21, 0x00, 0x22, 0x80, 0xac, 0x42,
   };

bool
agx3_pack_update_bvh_fixed_witness(
   enum agx_isa_encoding encoding,
   struct agx3_encoded_update_bvh_fixed_witness *out)
{
   if (encoding != AGX_ISA_ENCODING_AGX3C || out == NULL)
      return false;

   memset(out, 0, sizeof(*out));
   memcpy(out->bytes, update_bvh_fixed_witness_agx3c,
          sizeof(update_bvh_fixed_witness_agx3c));
   out->length = sizeof(update_bvh_fixed_witness_agx3c);
   return true;
}

bool
agx3_unpack_update_bvh_fixed_witness(enum agx_isa_encoding encoding,
                                     const uint8_t *bytes, size_t available,
                                     uint8_t *length)
{
   if (encoding != AGX_ISA_ENCODING_AGX3C || bytes == NULL || length == NULL ||
       available < sizeof(update_bvh_fixed_witness_agx3c) ||
       memcmp(bytes, update_bvh_fixed_witness_agx3c,
              sizeof(update_bvh_fixed_witness_agx3c)) != 0)
      return false;

   *length = sizeof(update_bvh_fixed_witness_agx3c);
   return true;
}
