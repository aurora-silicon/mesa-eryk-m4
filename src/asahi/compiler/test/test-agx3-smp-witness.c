/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Apple's own G17P `smp` instructions, captured from a real A18 Pro.
 *
 * These come from a Metal binary archive built on the device (macOS 26.6.2,
 * MTLBinaryArchive + serializeToURL:) and decoded to field vectors with the
 * offline composer; every one re-encodes byte-exactly here. They are the
 * ground truth for AGX3 texture addressing, which the 1494-instruction
 * command-stream corpus does not cover at all -- it contains zero `smp`.
 *
 * What they establish. The texture and sampler each occupy an operand slot
 * whose KIND selects which field carries the value, following the same
 * 1=immediate / 2=register / 3=uniform convention documented in
 * agx3_copy_core.h, over three consecutive fields:
 *
 *     slot      kind 1 (imm)   kind 2 (reg)   kind 3 (uniform)
 *     texture   f95            f96            f97      (selector f103)
 *     sampler   f124           f125           f126     (selector f132)
 *
 * `ffixed` is ep3 and uses the immediate pair (f95/f124) -- the form
 * agx3_tex_core.c already encodes. `fbindless` and `cbindless` are ep4 and
 * use the uniform pair (f97/f126).
 *
 * All three kinds are now witnessed, from two captures on the same device
 * with `ffixed` held byte-identical across both as the control:
 *
 *   capture 1  `constant uint& idx`   -- uniform across the draw, so Metal
 *              folded the index into the base: f103=3/f97, f132=3/f126.
 *   capture 2  `idx` derived per-thread (`gid.x & 3u`) -- cannot be hoisted:
 *              f103=2/f96, f132=2/f125.
 *
 * The register and uniform kinds name 64-bit argument slots. Native UMD
 * producer evidence now identifies their values: setTexture:atIndex:
 * (0x33dcc8) writes the zero-extended TextureGen4 heap allocation index from
 * +0x78; setSamplerState:atIndex: (0x33db40) similarly writes the sampler
 * heap index. They are not raw texture or sampler descriptor payloads.
 *
 * A 16-byte argument-buffer entry contains two such indices. Per-thread
 * loading places texture at half 0 and sampler at half 4. The shader still
 * needs the native global texture/sampler heap pointers in its prefix.
 * AGX2 base-plus-byte-offset addressing cannot be passed through unchanged.
 */
#include "agx3_binder.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x)                                                               \
   do {                                                                        \
      checks++;                                                                \
      if (!(x)) {                                                              \
         fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x);           \
         failures++;                                                           \
      }                                                                        \
   } while (0)

struct smp_witness {
   const char *name;
   uint8_t ep;
   unsigned nfields;
   const uint16_t *fields;
   const uint64_t *values;
   unsigned nbytes;
   const uint8_t *bytes;
};

/* ffixed: fixed [[texture(0)]]/[[sampler(0)]] -- IMMEDIATE kind, f95/f124 */
static const uint16_t smp_f0[] = {2,9,10,11,12,13,19,23,26,27,28,37,38,39,40,41,42,51,52,53,54,55,62,63,64,65,66,67,74,75,76,77,78,79,83,89,90,91,92,93,95,103,104,105,106,107,108,112,118,119,120,121,122,124,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,156};
static const uint64_t smp_v0[] = {0,2,2,0,1,0,0,5,1,1,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,15,1,0,2,0,0,3,3,0,0,0,0,1,1,0,0,0,0,4,3,3,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,0,0,0,0,2,0,0,1};
static const uint8_t smp_b0[] = {0x05,0x80,0x0c,0xb8,0xb0,0x00,0x00,0x00,0x01,0x00,0x10,0x00,0x01,0x00};

/* fbindless_uniform: argument buffer, uniform index -- UNIFORM kind, f97/f126 */
static const uint16_t smp_f1[] = {2,9,10,11,12,13,19,23,26,27,28,37,38,39,40,41,42,51,52,53,54,55,62,63,64,65,66,67,74,75,76,77,78,79,83,89,90,91,92,93,97,103,104,105,106,107,108,112,118,119,120,121,122,126,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,156};
static const uint64_t smp_v1[] = {0,2,2,0,1,0,0,5,1,1,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,15,1,0,2,0,0,3,3,0,0,0,16,3,2,0,0,0,0,4,3,3,0,0,0,20,3,2,0,0,0,0,1,0,0,0,0,0,0,0,0,2,0,0,1};
static const uint8_t smp_b1[] = {0x05,0x80,0x0c,0xb8,0xb4,0x00,0x00,0x00,0x93,0x03,0x90,0x01,0x01,0x00,0x80,0x20,0x00,0x00,0x00,0x00};

/* cbindless_uniform: same, compute -- UNIFORM kind, f97/f126 */
static const uint16_t smp_f2[] = {2,9,10,11,12,13,19,23,26,27,29,37,38,39,40,41,42,51,52,53,54,55,62,63,64,65,66,67,74,75,76,77,78,79,83,89,90,91,92,93,97,103,104,105,106,107,108,112,118,119,120,121,122,126,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,156};
static const uint64_t smp_v2[] = {0,2,2,0,1,0,0,5,1,1,0,1,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,15,1,0,2,0,0,3,3,0,0,0,20,3,2,0,0,0,0,4,3,3,0,0,0,24,3,2,0,0,0,0,1,2,0,0,0,0,0,0,0,2,0,0,0};
static const uint8_t smp_b2[] = {0x05,0xa0,0x0c,0xb8,0xb8,0x00,0x09,0x00,0x92,0x03,0x90,0x01,0x01,0x00,0x80,0x20,0x00,0x00,0x00,0x00};

/* fbindless_register: argument buffer, PER-THREAD index -- REGISTER kind, f96/f125 */
static const uint16_t smp_f3[] = {2,9,10,11,12,13,19,23,26,27,28,37,38,39,40,41,42,51,52,53,54,55,62,63,64,65,66,67,74,75,76,77,78,79,83,89,90,91,92,93,96,103,104,105,106,107,108,112,118,119,120,121,122,125,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,156};
static const uint64_t smp_v3[] = {0,2,2,0,1,0,0,5,1,1,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,1,15,1,0,2,0,0,3,3,0,0,0,0,2,2,0,0,1,0,4,3,3,0,0,0,4,2,2,0,0,1,0,1,0,0,0,0,0,0,0,0,2,0,0,1};
static const uint8_t smp_b3[] = {0x05,0x80,0x0c,0xb8,0x34,0x00,0x00,0x00,0x01,0x02,0x90,0x01,0x01,0x00,0xa0,0x28,0x00,0x00,0x40,0x00};

/* cbindless_register: same, compute -- REGISTER kind, f96/f125 */
static const uint16_t smp_f4[] = {2,9,10,11,12,13,19,23,26,27,29,37,38,39,40,41,42,51,52,53,54,55,62,63,64,65,66,67,74,75,76,77,78,79,83,89,90,91,92,93,96,103,104,105,106,107,108,112,118,119,120,121,122,125,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,156};
static const uint64_t smp_v4[] = {0,2,2,0,1,0,0,5,1,1,0,1,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,1,15,1,0,2,0,0,3,3,0,0,0,0,2,2,0,0,1,0,4,3,3,0,0,0,4,2,2,0,0,1,0,1,2,0,0,0,0,0,0,0,2,0,0,0};
static const uint8_t smp_b4[] = {0x05,0x80,0x0c,0xb8,0x34,0x00,0x09,0x00,0x00,0x02,0x90,0x01,0x01,0x00,0xa0,0x28,0x00,0x00,0x40,0x00};

static const struct smp_witness witnesses[] = {
   {"ffixed", 3, 73, smp_f0, smp_v0, 14, smp_b0},
   {"fbindless_uniform", 4, 73, smp_f1, smp_v1, 20, smp_b1},
   {"cbindless_uniform", 4, 73, smp_f2, smp_v2, 20, smp_b2},
   {"fbindless_register", 4, 73, smp_f3, smp_v3, 20, smp_b3},
   {"cbindless_register", 4, 73, smp_f4, smp_v4, 20, smp_b4},
};

int
main(void)
{
   for (unsigned i = 0; i < sizeof(witnesses) / sizeof(witnesses[0]); ++i) {
      const struct smp_witness *w = &witnesses[i];
      struct agx3_field_kv kv[96];
      if (w->nfields > sizeof(kv) / sizeof(kv[0])) {
         fprintf(stderr, "FAIL %s: too many fields\n", w->name);
         ++failures;
         ++checks;
         continue;
      }
      for (unsigned k = 0; k < w->nfields; ++k) {
         kv[k].field = w->fields[k];
         kv[k].value = w->values[k];
      }

      uint8_t out[32];
      uint8_t len = 0;
      const bool ok = agx3_bind_compose(AGX_ISA_ENCODING_AGX3B, "smp", w->ep,
                                        kv, w->nfields, NULL, 0, out, &len);
      CHECK(ok);
      if (!ok)
         continue;
      CHECK(len == w->nbytes);
      if (len != w->nbytes)
         continue;
      if (memcmp(out, w->bytes, len) != 0) {
         fprintf(stderr, "FAIL %s bytes:\n  got   ", w->name);
         for (unsigned b = 0; b < len; ++b)
            fprintf(stderr, "%02x", out[b]);
         fprintf(stderr, "\n  apple ");
         for (unsigned b = 0; b < w->nbytes; ++b)
            fprintf(stderr, "%02x", w->bytes[b]);
         fprintf(stderr, "\n");
         ++failures;
      }
      ++checks;
   }

   if (failures) {
      fprintf(stderr, "agx3_smp_witness: %u/%u failed\n", failures, checks);
      return 1;
   }
   printf("agx3_smp_witness: all %u checks passed\n", checks);
   return 0;
}
