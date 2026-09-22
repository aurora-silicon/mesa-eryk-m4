# M3 shadow-gather custom borders (2026-09-22)

Two depth32f/stencil8 border-clamp gather cases failed image verification.
The serial reproducer fails at negative and infinite custom border values;
8908 POT and 9244 NPOT pixels are wrong. The compiled NIR used a native
shadow gather without custom-border handling: the border lowering explicitly
skipped shadow operations. The sampler's white replacement border therefore
reached the comparison instead of the requested custom value.

For shadow gathers, preserve the native shadow result for interior taps.
Identify border taps with two non-shadow, unfiltered gathers using the existing
zero/one border sampler pair, then substitute comparison against the actual
custom border. Bitwise comparison of the probes preserves interior NaNs.
The sampler comparison function is decoded, UNORM depth references/borders
are saturated, and sparse residency comes from the original shadow result.
Only gathers are changed; this is not a claim about all PCF shadow sampling.

The software view-minimum sampler representation also retains the comparison
function in previously unused bits 29:31. Lowering selects the native or
software representation using the existing pipeline feature key. The focused
GLES tests use the native sampler representation; the view-minimum variant
has not received separate Vulkan hardware qualification here.

An initial experimental patch removed a cloned texture source before inserting
its instruction in NIR. This caused a userspace compiler SIGSEGV in
nir_tex_instr_remove_src. The core/backtrace is retained under `shadow-border`;
the candidate was never installed for the desktop. Inserting the clone before
removing its comparator fixes that candidate bug. No kernel fault or reboot
occurred. `shadow-border2` passes both original cases, and the final build
passes all 619 border-clamp cases with four workers (zero skips/failures).

All experiments retained the M3 45-second guard, 60-second watchdog and five
PMP channels sampled every 0.5 seconds, with a 95 C stop. This is focused
regression coverage, not full GLES/Vulkan conformance.

## Final-build evidence

- `build/m3-gles31-20260922/triage-border-depth/neighbors-00/run`: 128 Pass, peak 47.50 C; results SHA256 `2b4fdca3d01ea678a06010426eb9df3f7fc327154580807f8807447c85055a57`.
- `build/m3-gles31-20260922/triage-border-depth/neighbors-01/run`: 128 Pass, peak 48.75 C; results SHA256 `60fb81a22cff237e686a8ac455ee96de946d83cf5f7391ab0e434fd589fd8e40`.
- `build/m3-gles31-20260922/triage-border-depth/neighbors-02/run`: 128 Pass, peak 50.50 C; results SHA256 `bb3955b7ed1951a15b5146f4d50f30b49b922fea2ee5b0b0e90dc67f0be1d904`.
- `build/m3-gles31-20260922/triage-border-depth/neighbors-03/run`: 128 Pass, peak 49.50 C; results SHA256 `a394a8fb94e13775fb68e1f996f755fd2cefb4037f3ed1f4b2a852520d90dc4a`.
- `build/m3-gles31-20260922/triage-border-depth/neighbors-04/run`: 107 Pass, peak 50.50 C; results SHA256 `87ceb981a6814b13c0f551309498f9b2c6bc374f4fcf8d1d44356b951ae35382`.
