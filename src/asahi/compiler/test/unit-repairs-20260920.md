# Compiler unit-test repair, 2026-09-20

The 47 failures reported after the Vulkan CTS fixes were in `agx_tests`, not
47 additional Vulkan CTS failures. Rebuilding Mesa 291e1ff58d8 reproduced
244 passes / 47 failures in 291 tests. The earlier 290-test log omitted the
new diagnostic-printer test. The repaired suite passes all 291 tests with
no disabled tests, filters, or removed tests. Eight tests were renamed to
reflect their updated acceptance domains.

## Audit

All 47 were stale expectations or incomplete test setup, audited against the
current compiler contracts and native encoder witnesses. No production compiler,
Vulkan capability, installed driver, or CTS case was changed in this follow-up.
Exact byte checks remain, either against native bytes or independently tested
encoder cores. Newly supported cases get positive encoding checks; malformed
operands still require transactional refusal.

| Commit | Tests repaired | Contract checked |
| --- | ---: | --- |
| 327f40eb691 | 6 | Retained ALU operands, TPS reload lifetime, r1 shuffle scratch, compact float modifiers. |
| bb2d6c1d359 | 10 | Live subgroup operands, all 32 broadcast lanes, non-destructive scoreboard waits. |
| 53ca9eb7b87 | 9 | CLR/ATOMIC/RLD returns, post-RA cache analysis, signed conversions and supported saturation/negation. |
| d743270db3a | 12 | Literal sample masks, SNORM interlocks, centroid coordinates, memory scaling/slots, low-half FSEL destination and nested CNDCF. |
| 9fb3ee742d8 | 9 | Texture coordinate staging, register LOD, full source-register range, drains before control flow. |
| 5eddcdcdfc3 | 1 | Image-load retirement before write-coordinate staging, final-read coordinate hints, coherent storage opcode. |

The broader encoder check found an additional defect in `test-agx3-pixel-core.c`:
the C argument list read `len` while another argument called the encoder that
writes it. Argument evaluation order is unspecified. Commit 54dc3f802f8 separates
the calls; all 39 byte-exact pixel checks pass without changing their expected
encodings.

## Reproduction

```
meson test -C build/mesa-m4-asahi-host --print-errorlogs \
  agx_tests agx3_alu_core agx3_subgroup_core agx3_atomic_core \
  agx3_falu_mod_core agx3_static_spill_core agx3_tex_core \
  agx3_g17p_producers agx3_vary_core agx3_cf_core agx3_mem_core \
  agx3_tile_core agx3_slot_core agx3_pixel_core
```

All 14 selected executables pass: the complete 291-case `agx_tests` executable
plus 13 related encoder/producer suites. This is a host test result, not a new
GPU run or a claim about every Mesa test executable. Normal desktop libraries
and the earlier guarded hardware CTS validation remain unchanged.

Workspace logs: `build/compiler-unit-fixes-20260920/`.
Preserved baseline/final GoogleTest JSON, the 47-case old/new-name audit, and
encoder logs: canonical kernel `reference/cts/compiler-unit-fixes-20260920/`.
