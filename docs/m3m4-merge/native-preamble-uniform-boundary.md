# Native preamble input/output boundary

Native preambles remain experimental and disabled by default. This fixes one
reproducible corruption; it does not qualify the complete memory-preamble path.

With `HK_M4_NATIVE_PREAMBLE=true AGX_NATIVE_MEMORY_PREAMBLE=true`, a 256x4
ordinary/custom-border sampler test returns incorrect channels on M3 Pro and
M4. It alternates clamp-to-edge and custom-border samplers across 16 draws,
with a 1x1 RGBA8 texture and an all-255 integer UBO. Baseline Mesa without
preambles passes. UBO-only also passes with preambles; sampler-only fails.

Direct diagnostic readback of the preamble's output uniforms showed correct
border-color values but intermittent zeroes in scaling constants at uniform
halves 44 and 46. Those outputs shared the 128-bit block containing the input
pointer at halves 40..43. Rounding the reserved input extent to eight halves
moves output allocation to half 48 and fixes both sampler-only and combined
sampler/UBO probes. A 16-half reservation gave no additional improvement.
The exact hardware ordering/cache mechanism behind this boundary is not yet
identified; the readback result is established on both chips.

Validation on 2026-09-26:

- M3 Pro, boot `5faeff66-3e0c-4a38-931a-14c012650626`: sampler-only
  8,448 mismatches -> 0; combined sampler/UBO 12,608 -> 0; UBO-only remains 0.
- M4, boot `7b02370e-2a9b-445b-935f-db0f87b80aa7`: sampler-only
  8,448 -> 0; combined sampler/UBO 12,672 -> 0; UBO-only remains 0.
- All runs used 60-second watchdogs and temperature guards; no reboot.
- 297 host backend tests pass. ARM Vulkan driver builds.
- Saved Satisfactory gameplay still differs from the exact baseline when
  memory preambles are enabled (maximum channel differences 94/94/96 in
  frames 1/16/31). No timing approval or default enablement.

The standalone reproducer is `tests/m3m4-preamble/sampler-ubo.c`:

```
cc -O2 sampler-ubo.c -lEGL -lGL -o sampler-ubo
```

Run via the target's watchdog/thermal wrapper, selecting Zink and the candidate
ICD. By default numeric mismatch exits nonzero. `--diagnostic` preserves explicit
FAIL output but exits zero after successful readback, so a healthy device can
remain available for diagnosis. API failures still exit nonzero. Never classify
that diagnostic exit code as correctness success.

Private evidence: `20260926-m3-preamble-blockalign`, M4
`20260926T035031389957Z-preamble-blockalign-sampler`; full replay checks
`20260926T034903742826Z` and `20260926T035051613306Z`.
