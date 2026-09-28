/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Byte-exact replay of the witnessed AGX3 ray-traversal instructions.
 *
 * Every expected byte string below is a pinned witness: the intersect_ray /
 * continue_ray strings are the decoded G16G/G17S corpus vectors in
 * agx3_compose_corpus_vectors.c and the five-kernel corpus ladder bytes that
 * agx3_pack_{intersect,continue}_ray reproduce; the incStk / barrier /
 * update_node / update_bvh strings are the exact fixed witnesses pinned in
 * agx3_pack.c.  If this core ever stops reproducing them it is emitting
 * something Apple never emitted.
 */

#include "agx3_rt_core.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;
static unsigned checks;

static void
expect_result(const char *what, bool ok, const uint8_t *bytes, uint8_t len,
       const char *expected_hex)
{
   char got[2 * AGX3_RT_CORE_MAX_LENGTH + 1] = {0};

   checks++;

   if (!ok) {
      fprintf(stderr, "%s: refused\n", what);
      failures++;
      return;
   }

   for (unsigned i = 0; i < len; ++i)
      snprintf(got + 2 * i, 3, "%02x", bytes[i]);

   if (strcmp(got, expected_hex) != 0) {
      fprintf(stderr, "%s: got %s, expected %s\n", what, got, expected_hex);
      failures++;
   }
}

static void
expect_refused_result(const char *what, bool ok, const uint8_t *bytes, uint8_t len)
{
   checks++;
   if (ok) {
      fprintf(stderr, "%s: accepted, expected refusal\n", what);
      failures++;
      return;
   }
   /* Refusal must zero the transaction. */
   for (unsigned i = 0; i < AGX3_RT_CORE_MAX_LENGTH; ++i) {
      if (bytes[i] != 0) {
         fprintf(stderr, "%s: refusal left byte %u nonzero\n", what, i);
         failures++;
         return;
      }
   }
   if (len != 0) {
      fprintf(stderr, "%s: refusal left length %u\n", what, len);
      failures++;
   }
}

/* C does not sequence function arguments. Read the output length only after
 * the encoder writes it; otherwise a call can compare the previous encoding's
 * length (or the uninitialized first length) against an unrelated witness.
 */
#define expect(what, encode, bytes, length, hex)                                \
   do {                                                                        \
      const bool encoded_ = (encode);                                          \
      expect_result((what), encoded_, (bytes), (length), (hex));                 \
   } while (0)

#define expect_refused(what, encode, bytes, length)                             \
   do {                                                                        \
      const bool encoded_ = (encode);                                          \
      expect_refused_result((what), encoded_, (bytes), (length));               \
   } while (0)

int
main(void)
{
   uint8_t b[AGX3_RT_CORE_MAX_LENGTH];
   uint8_t len;

   /* intersect_ray: the corpus differential pairs, varying only f5. */
   static const struct {
      uint16_t reg;
      const char *hex;
   } b_isect[] = {
      {0, "5f10540000412640c7504c00"},
      {4, "5f10540400412640c7504c00"},
   }, c_isect[] = {
      {0, "27042901a2c1ae02019827000020"},
      {8, "27042901a2c1ae02019827000022"},
   };

   for (unsigned i = 0; i < 2; ++i) {
      expect("AGX3b intersect_ray",
             agx3_rt_intersect_ray(AGX_ISA_ENCODING_AGX3B, b_isect[i].reg, b,
                                   &len),
             b, len, b_isect[i].hex);
      expect("AGX3c intersect_ray",
             agx3_rt_intersect_ray(AGX_ISA_ENCODING_AGX3C, c_isect[i].reg, b,
                                   &len),
             b, len, c_isect[i].hex);
   }

   /* Every witnessed intersect_ray site, including the anonymous
    * scheduling-context variants (corpus vectors 263, 1006, 1453). */
   static const char *b_sites[] = {
      "5f10540000412640c7504c00", /* 561 */
      "5f10540400412640c7504c00", /* 470/651 */
      "5f1054000041264047504c00", /* 263: f92=130 */
   };
   static const char *c_sites[] = {
      "27042901a2c1ae02019827000020", /* 1351 */
      "27042901a2c1ae02019827000022", /* 1252 */
      "27042901a2c1ae020198020000a0", /* 1006: f17=4, f28=1 */
      "27042901a2c1ae020198270000a2", /* 1453: f28=1 */
   };

   if (agx3_rt_intersect_ray_num_witnessed_sites(AGX_ISA_ENCODING_AGX3B) != 3 ||
       agx3_rt_intersect_ray_num_witnessed_sites(AGX_ISA_ENCODING_AGX3C) != 4 ||
       agx3_rt_intersect_ray_num_witnessed_sites(AGX_ISA_ENCODING_AGX2) != 0) {
      fprintf(stderr, "witnessed site counts wrong\n");
      failures++;
   }
   checks++;

   for (unsigned i = 0; i < 3; ++i) {
      expect("AGX3b intersect_ray site",
             agx3_rt_intersect_ray_witnessed_site(AGX_ISA_ENCODING_AGX3B, i, b,
                                                  &len),
             b, len, b_sites[i]);
   }
   for (unsigned i = 0; i < 4; ++i) {
      expect("AGX3c intersect_ray site",
             agx3_rt_intersect_ray_witnessed_site(AGX_ISA_ENCODING_AGX3C, i, b,
                                                  &len),
             b, len, c_sites[i]);
   }

   /* continue_ray: the full accepted ladders, byte-identical to the
    * agx3_pack_continue_ray deposit scheme on every value. */
   static const struct {
      uint16_t reg;
      const char *hex;
   } b_cont[] = {
      {0, "5f1154000000"},  {2, "5f1154020000"}, {4, "5f1154040000"},
      {6, "5f1154060000"},  {8, "5f1154080000"}, {10, "5f11540a0000"},
   }, c_cont[] = {
      {0, "2704090022c0af02"},  {2, "2704090022c0af42"},
      {8, "2704090032c0af02"},  {10, "2704090032c0af42"},
      {12, "270409003ac0af02"}, {16, "27040900a2c0af02"},
      {18, "27040900a2c0af42"},
   };

   for (unsigned i = 0; i < 6; ++i) {
      expect("AGX3b continue_ray",
             agx3_rt_continue_ray(AGX_ISA_ENCODING_AGX3B, b_cont[i].reg, b,
                                  &len),
             b, len, b_cont[i].hex);
   }
   for (unsigned i = 0; i < 7; ++i) {
      expect("AGX3c continue_ray",
             agx3_rt_continue_ray(AGX_ISA_ENCODING_AGX3C, c_cont[i].reg, b,
                                  &len),
             b, len, c_cont[i].hex);
   }

   /* The fixed forms: exact agx3_pack.c witnesses. */
   expect("AGX3b incStk", agx3_rt_inc_stk(AGX_ISA_ENCODING_AGX3B, b, &len), b,
          len, "0700541280000000");
   expect("AGX3c incStk", agx3_rt_inc_stk(AGX_ISA_ENCODING_AGX3C, b, &len), b,
          len, "9f04000000000000");
   expect("AGX3b update barrier",
          agx3_rt_update_barrier(AGX_ISA_ENCODING_AGX3B, b, &len), b, len,
          "070454080900");
   expect("AGX3c update barrier",
          agx3_rt_update_barrier(AGX_ISA_ENCODING_AGX3C, b, &len), b, len,
          "170900060000");
   expect("AGX3b update_node",
          agx3_rt_update_node(AGX_ISA_ENCODING_AGX3B, b, &len), b, len,
          "df055400");
   expect("AGX3c update_node",
          agx3_rt_update_node(AGX_ISA_ENCODING_AGX3C, b, &len), b, len,
          "270001002200ad02");
   expect("AGX3c update_bvh",
          agx3_rt_update_bvh(AGX_ISA_ENCODING_AGX3C, b, &len), b, len,
          "270021002280ac42");

   /* The complete expansions, byte-identical to agx3_pack_update_node and
    * agx3_pack_update_bvh_fixed_witness. */
   expect("AGX3b update_node expansion",
          agx3_rt_update_node_expansion(AGX_ISA_ENCODING_AGX3B, b, &len), b,
          len, "070454080900df055400");
   expect("AGX3c update_node expansion",
          agx3_rt_update_node_expansion(AGX_ISA_ENCODING_AGX3C, b, &len), b,
          len, "170900060000270001002200ad02");
   expect("AGX3c update_bvh expansion",
          agx3_rt_update_bvh_expansion(AGX_ISA_ENCODING_AGX3C, b, &len), b,
          len, "170900060000270021002280ac42");

   /* Fail-closed: unwitnessed state registers, cross-encoding ladders, the
    * unwitnessed AGX3b update_bvh, out-of-range sites, and AGX2. */
   expect_refused("intersect_ray b reg 2",
                  agx3_rt_intersect_ray(AGX_ISA_ENCODING_AGX3B, 2, b, &len), b,
                  len);
   expect_refused("intersect_ray b reg 8",
                  agx3_rt_intersect_ray(AGX_ISA_ENCODING_AGX3B, 8, b, &len), b,
                  len);
   expect_refused("intersect_ray c reg 4",
                  agx3_rt_intersect_ray(AGX_ISA_ENCODING_AGX3C, 4, b, &len), b,
                  len);
   expect_refused("intersect_ray on AGX2",
                  agx3_rt_intersect_ray(AGX_ISA_ENCODING_AGX2, 0, b, &len), b,
                  len);
   expect_refused("continue_ray b reg 1",
                  agx3_rt_continue_ray(AGX_ISA_ENCODING_AGX3B, 1, b, &len), b,
                  len);
   expect_refused("continue_ray b reg 12",
                  agx3_rt_continue_ray(AGX_ISA_ENCODING_AGX3B, 12, b, &len), b,
                  len);
   expect_refused("continue_ray c reg 4",
                  agx3_rt_continue_ray(AGX_ISA_ENCODING_AGX3C, 4, b, &len), b,
                  len);
   expect_refused("continue_ray c reg 6",
                  agx3_rt_continue_ray(AGX_ISA_ENCODING_AGX3C, 6, b, &len), b,
                  len);
   expect_refused("continue_ray on UNKNOWN",
                  agx3_rt_continue_ray(AGX_ISA_ENCODING_UNKNOWN, 0, b, &len),
                  b, len);
   expect_refused("update_bvh on AGX3b",
                  agx3_rt_update_bvh(AGX_ISA_ENCODING_AGX3B, b, &len), b, len);
   expect_refused("update_bvh expansion on AGX3b",
                  agx3_rt_update_bvh_expansion(AGX_ISA_ENCODING_AGX3B, b,
                                               &len),
                  b, len);
   expect_refused("intersect_ray b site 3",
                  agx3_rt_intersect_ray_witnessed_site(AGX_ISA_ENCODING_AGX3B,
                                                       3, b, &len),
                  b, len);
   expect_refused("intersect_ray c site 4",
                  agx3_rt_intersect_ray_witnessed_site(AGX_ISA_ENCODING_AGX3C,
                                                       4, b, &len),
                  b, len);
   expect_refused("intersect_ray site on AGX2",
                  agx3_rt_intersect_ray_witnessed_site(AGX_ISA_ENCODING_AGX2,
                                                       0, b, &len),
                  b, len);
   expect_refused("inc_stk on AGX2",
                  agx3_rt_inc_stk(AGX_ISA_ENCODING_AGX2, b, &len), b, len);
   expect_refused("update barrier on AGX2",
                  agx3_rt_update_barrier(AGX_ISA_ENCODING_AGX2, b, &len), b,
                  len);
   expect_refused("update_node on UNKNOWN",
                  agx3_rt_update_node(AGX_ISA_ENCODING_UNKNOWN, b, &len), b,
                  len);

   /* NULL-argument hardening. */
   checks++;
   if (agx3_rt_intersect_ray(AGX_ISA_ENCODING_AGX3B, 0, NULL, &len) ||
       agx3_rt_intersect_ray(AGX_ISA_ENCODING_AGX3B, 0, b, NULL) ||
       agx3_rt_continue_ray(AGX_ISA_ENCODING_AGX3B, 0, NULL, &len) ||
       agx3_rt_update_node_expansion(AGX_ISA_ENCODING_AGX3B, b, NULL)) {
      fprintf(stderr, "NULL argument accepted\n");
      failures++;
   }

   if (failures) {
      fprintf(stderr, "agx3_rt_core: %u/%u checks failed\n", failures, checks);
      return 1;
   }

   printf("agx3_rt_core: all %u checks passed\n", checks);
   return 0;
}
