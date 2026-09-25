/*
 * Copyright 2026 Satya Benson
 * SPDX-License-Identifier: MIT
 *
 * G15G scheduling-stall pass against Apple's own placement.
 *
 * Each vector is one Apple G15G shader: the input is that code with every
 * stall removed (and b/bn displacements recomputed for the shorter stream),
 * the expectation is Apple's G15G bytes. The pass must reproduce them
 * exactly, be idempotent, and map offsets consistently.
 *
 * With an argument, also replays a vectors file ("name input expected" per
 * line) -- the full corpus produced by
 * test/probes/g15g-stalls/run-corpus.sh on a Mac with Apple's toolchain.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agx3_g15g_stalls.h"
#include "agx3_g15g_stall_vectors.h"

static unsigned failures;

static size_t
unhex(const char *h, uint8_t **out)
{
   const size_t n = strlen(h) / 2;
   uint8_t *b = malloc(n ? n : 1);
   for (size_t i = 0; i < n; ++i) {
      unsigned v;
      sscanf(h + 2 * i, "%2x", &v);
      b[i] = (uint8_t)v;
   }
   *out = b;
   return n;
}

static int
check(const char *name, const char *in_hex, const char *exp_hex, bool verbose)
{
   uint8_t *in, *exp, *out = NULL, *again = NULL;
   const size_t ni = unhex(in_hex, &in), ne = unhex(exp_hex, &exp);
   size_t no = 0, na = 0;
   uint32_t *map = calloc(ni / 2 + 1, sizeof(*map));
   int stalls = agx3_g15g_insert_stalls(in, ni, &out, &no, map);
   bool ok = stalls >= 0 && no == ne && !memcmp(out, exp, ne);

   if (ok) {
      /* Every stall is 8 bytes; the stream end maps to the new size. */
      ok = no == ni + (size_t)stalls * AGX3_G15G_STALL_SIZE &&
           map[ni / 2] == no;
   }
   if (ok) {
      /* Re-running over padded code is a no-op. */
      int again_stalls = agx3_g15g_insert_stalls(out, no, &again, &na, NULL);
      ok = again_stalls == 0 && na == no && !memcmp(again, out, no);
   }

   if (!ok) {
      failures++;
      fprintf(stderr, "FAIL %s: stalls=%d size %zu, expected %zu\n", name,
              stalls, no, ne);
   } else if (verbose) {
      printf("ok   %s (%d stalls)\n", name, stalls);
   }

   free(in);
   free(exp);
   free(out);
   free(again);
   free(map);
   return ok ? stalls : -1;
}

static void
test_rejects_garbage(void)
{
   /* Odd length and an undecodable halfword must fail closed. */
   static const uint8_t odd[] = {0x06};
   static const uint8_t bad[] = {0xff, 0xff, 0xff, 0xff};
   uint8_t *out = (uint8_t *)1;
   size_t size = 1;
   if (agx3_g15g_insert_stalls(odd, sizeof(odd), &out, &size, NULL) != -1 ||
       out != NULL || size != 0) {
      failures++;
      fprintf(stderr, "FAIL odd-length input accepted\n");
   }
   out = (uint8_t *)1;
   if (agx3_g15g_insert_stalls(bad, sizeof(bad), &out, &size, NULL) != -1 ||
       out != NULL) {
      failures++;
      fprintf(stderr, "FAIL undecodable input accepted\n");
   }
}

static void
test_no_producer_no_stall(void)
{
   /* Apple's constant program (copy; end) and an ALU-only body need none. */
   static const uint8_t constant[] = {
      0x03, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x60, 0x00,
      0x0e, 0x00, 0x00, 0x00,
   };
   uint8_t *out;
   size_t size;
   if (agx3_g15g_insert_stalls(constant, sizeof(constant), &out, &size,
                               NULL) != 0 ||
       size != sizeof(constant) || memcmp(out, constant, size)) {
      failures++;
      fprintf(stderr, "FAIL constant program changed\n");
   }
   free(out);
}

int
main(int argc, char **argv)
{
   unsigned total = 0, stalls = 0;
   for (unsigned i = 0;
        i < sizeof(g15g_stall_vectors) / sizeof(g15g_stall_vectors[0]); ++i) {
      const struct g15g_stall_vector *v = &g15g_stall_vectors[i];
      int s = check(v->name, v->input, v->expected, true);
      total++;
      if (s > 0)
         stalls += s;
   }
   test_rejects_garbage();
   test_no_producer_no_stall();

   if (argc > 1) {
      FILE *fp = fopen(argv[1], "r");
      if (!fp) {
         fprintf(stderr, "cannot open %s\n", argv[1]);
         return 1;
      }
      size_t cap = 1 << 20;
      char *line = malloc(cap);
      while (fgets(line, cap, fp)) {
         char *name = strtok(line, " ");
         char *in = strtok(NULL, " ");
         char *exp = strtok(NULL, " \r\n");
         if (!name || !in || !exp)
            continue;
         int s = check(name, in, exp, false);
         total++;
         if (s > 0)
            stalls += s;
      }
      free(line);
      fclose(fp);
   }

   printf("%u shaders, %u stalls, %u failures\n", total, stalls, failures);
   return failures ? 1 : 0;
}
