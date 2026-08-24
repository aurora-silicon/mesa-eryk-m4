/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../d12_sample_position_core.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;

static void
require(int condition, const char *message)
{
   checks++;
   if (!condition) {
      fprintf(stderr, "FAIL: %s\n", message);
      exit(1);
   }
}

/*
 * The load-bearing test. These three values are what Honeykrisp programs into
 * PPP_MULTISAMPLECTL for the default patterns, arrived at independently of
 * this encoder. Packing D3D12's own standard positions has to reproduce them
 * exactly, or the nibble layout and the sign bias are wrong and every
 * programmed pattern would sample somewhere else.
 */
static void
test_standard_positions_reproduce_the_defaults(void)
{
   const struct {
      uint32_t count;
      uint32_t expected;
   } cases[] = {
      {1, 0x88u},
      {2, 0x44ccu},
      {4, 0xeaa26e26u},
   };

   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      struct d12_sample_position positions[D12_SAMPLE_POSITION_MAX_SAMPLES];
      uint32_t packed = 0;

      require(d12_sample_positions_standard(cases[i].count, positions),
              "a supported sample count has standard positions");
      require(d12_sample_positions_pack(cases[i].count, positions, &packed),
              "standard positions are encodable");
      require(packed == cases[i].expected,
              "packing the standard positions reproduces the hardware "
              "default for that sample count");
   }
}

static void
test_bias_and_layout(void)
{
   struct d12_sample_position centre[1] = {{0, 0}};
   struct d12_sample_position corner[1] = {{-8, -8}};
   struct d12_sample_position far_corner[1] = {{7, 7}};
   uint32_t packed = 0;

   require(d12_sample_positions_pack(1, centre, &packed) && packed == 0x88u,
           "the pixel centre is the middle of the nibble range");
   require(d12_sample_positions_pack(1, corner, &packed) && packed == 0x00u,
           "the most negative position is nibble zero on both axes");
   require(d12_sample_positions_pack(1, far_corner, &packed) &&
              packed == 0xffu,
           "the most positive position is nibble fifteen on both axes");

   /* X is the low nibble and Y the high one; sample zero is the low byte. */
   struct d12_sample_position asymmetric[2] = {{-8, 7}, {7, -8}};
   require(d12_sample_positions_pack(2, asymmetric, &packed) &&
              packed == 0x0ff0u,
           "X occupies the low nibble and sample zero the low byte");
}

static void
test_unencodable_requests_are_refused(void)
{
   struct d12_sample_position positions[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
   uint32_t packed = 0;

   require(!d12_sample_positions_pack(0, positions, &packed),
           "a zero sample count is refused");
   require(!d12_sample_positions_pack(3, positions, &packed),
           "a sample count AGX cannot rasterize is refused");
   require(!d12_sample_positions_pack(8, positions, &packed),
           "eight samples are refused");
   require(!d12_sample_positions_pack(1, NULL, &packed),
           "a null position array is refused");
   require(!d12_sample_positions_pack(1, positions, NULL),
           "a null output is refused");

   /* A coordinate the nibbles cannot hold must not be silently clamped:
    * that would move where the hardware samples with nothing to detect it. */
   struct d12_sample_position too_low[1] = {{-9, 0}};
   struct d12_sample_position too_high[1] = {{0, 8}};
   require(!d12_sample_positions_pack(1, too_low, &packed),
           "a position below the encodable range is refused, not clamped");
   require(!d12_sample_positions_pack(1, too_high, &packed),
           "a position above the encodable range is refused, not clamped");

   require(!d12_sample_positions_standard(3, positions),
           "an unsupported count has no standard positions");
   require(!d12_sample_positions_standard(1, NULL),
           "a null destination is refused");
}

static void
test_tier1_request_rules(void)
{
   /* Reset to defaults. */
   require(d12_sample_positions_tier1_request_valid(0, 0, 4),
           "a zeroed request resets to the default pattern");

   /* Tier 1 is one pattern for every pixel. */
   require(d12_sample_positions_tier1_request_valid(4, 1, 4),
           "one pixel with a matching sample count is a tier 1 request");
   require(!d12_sample_positions_tier1_request_valid(4, 2, 4),
           "more than one pixel is tier 2 and is refused");
   require(!d12_sample_positions_tier1_request_valid(4, 4, 4),
           "a per-pixel pattern is refused");

   /* The pattern must describe the bound target's samples. */
   require(!d12_sample_positions_tier1_request_valid(2, 1, 4),
           "a pattern with fewer entries than the target has is refused");
   require(d12_sample_positions_tier1_request_valid(2, 1, 0),
           "with no target bound the count cannot be checked here");

   require(!d12_sample_positions_tier1_request_valid(8, 1, 8),
           "a sample count AGX cannot rasterize is refused");
   require(!d12_sample_positions_tier1_request_valid(3, 1, 3),
           "a non-power-of-two count is refused");
   /* A count with no pixels, or pixels with no count, is neither a reset nor
    * a valid pattern. */
   require(!d12_sample_positions_tier1_request_valid(4, 0, 4),
           "a sample count with no pixels is refused");
   require(!d12_sample_positions_tier1_request_valid(0, 1, 4),
           "pixels with no sample count are refused");
}

/* Every encodable pattern must round-trip through the register layout. */
static void
test_exhaustive_round_trip(void)
{
   for (int x = D12_SAMPLE_POSITION_MIN; x <= D12_SAMPLE_POSITION_MAX; x++) {
      for (int y = D12_SAMPLE_POSITION_MIN; y <= D12_SAMPLE_POSITION_MAX;
           y++) {
         struct d12_sample_position p[1] = {{(int8_t)x, (int8_t)y}};
         uint32_t packed = 0;

         require(d12_sample_positions_pack(1, p, &packed),
                 "every in-range position encodes");
         require((int)(packed & 0xf) - 8 == x,
                 "X survives the round trip");
         require((int)((packed >> 4) & 0xf) - 8 == y,
                 "Y survives the round trip");
         require((packed & ~0xffu) == 0,
                 "a single sample occupies only the low byte");
      }
   }
}

int
main(void)
{
   test_standard_positions_reproduce_the_defaults();
   test_bias_and_layout();
   test_unencodable_requests_are_refused();
   test_tier1_request_rules();
   test_exhaustive_round_trip();

   printf("d12_sample_position_core_tests: PASS (%u checks)\n", checks);
   return 0;
}
