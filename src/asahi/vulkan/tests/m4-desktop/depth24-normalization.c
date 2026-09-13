/* SPDX-License-Identifier: MIT
 * Host arithmetic model for D24 -> float storage -> D24 conversion.
 * Exhaustive (all 2^24 depth words); not hardware qualification.
 * cc -O2 -ffp-contract=off depth24-normalization.c -lm -o depth24-normalization
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
int main(void)
{
   unsigned bad = 0;
   for (uint32_t n = 0; n < (1u << 24); ++n) {
      float x = (float)n * 0x1p-24f;
      float f = fmaf(x, (float)(1.0 / 16777215.0), x);
      float reference = (float)((double)n / 16777215.0);
      uint32_t result = (uint32_t)nearbyintf(f * 16777215.0f);
      if (f != reference || result != n) {
         if (bad++ < 4)
            fprintf(stderr, "%u: %.9g expected %.9g returned %u\n", n, f, reference, result);
      }
   }
   printf("%s: D24 arithmetic, 16777216 inputs, %u errors\n", bad ? "FAIL" : "PASS", bad);
   return bad != 0;
}
