# OpenGL ES failure fixes, 2026-09-22

The user pivoted from the full run to fixing failures. The baseline stopped
with 240,609 of 326,602 configuration/case pairs recorded: 230,046 Pass,
10,118 NotSupported, 103 Fail, 303 Crash, 5 Timeout, one GPUTimeout,
15 QualityWarning and 18 CompatibilityWarning. Another GPU incident with
four interrupted cases is tracked separately. 85,993 pairs remain unrun.
The baseline files here are immutable input for retests, not final full-suite
results. No failed baseline outcome is overwritten.

`gl-cts-retest.py` is mirrored from workspace tools/. Run it there with
`--campaign build/gles32-zink-20260922/full --failures <baseline-failures.json>
--output <new-directory> --status Crash` (or Fail/Timeout/GPUTimeout). It
preserves each original case/configuration index, binary and environment,
uses one worker and the mandatory bounded watchdog/thermal/evidence guards,
and stops on fatal errors for review. Every run retains source and image logs.

## Half operands in the native compiler

The 303 compiler crashes are repeated configurations of shaders rejected at
DFDX or FMA after copy propagation folded an F16 input into an F32 operation.
Derivatives were missing from the existing unary widening pass; unsaturated
FMA incorrectly excluded half inputs carrying abs/neg. Materializing the exact
conversion before register allocation keeps native F32 operations and their
result width. No shader-specific matching or Zink fallback is used.

All 98 distinct retained compiler inputs reject before the fix and compile
afterward. The archive retains their serialized input, manifest and failed
backend IR. Replay with workspace tools/compiler-corpus.py. These trusted
internal-ABI captures are compiler regression evidence; hardware test results
are recorded separately. The existing unary-half-input compile test and four
new mixed-half FMA modifier variants pass. The broad fail-closed test also
encounters an unrelated old expectation that preambles must be rejected; it
is not reported as passing.

Hardware validation: all **303/303** recorded Crash configuration/case pairs
now Pass, with the original CTS executable and original per-session arguments.
All guarded runs passed; see crash-retest.json for exact evidence paths, times
and temperatures. Vulkan library SHA256:
`ca5a076a44714cddbe6f131e156c46e992ed9f10e89804c6e3bf4a554c2cf65c`.

## Native gradient offsets

All ten array textureGradOffset failures reproduced serially. The old native
offset filter selected generic NIR offset lowering for array gradients without
a view minimum; this adds offset/base-level-size before sampling, which is
wrong when gradients choose a different mip. Native array offset staging is
already implemented and witnessed. Keeping offsets for the sampler fixes all
ten cases. All 38 failed session-2 cases and the complete 63-case ES3 gradient
function group now Pass. Host native texture encoding tests: 6,417 checks pass.
See array-offset-retest.json and gradient-group.json.

Of the other 103 baseline image failures, 89 passed in the first serial
focused rerun. Their original failures remain recorded; this is not evidence
that a driver fix resolved them. Two EGL partial-update and two float-depth
gather failures still reproduce on hardware, and all four Pass with the same
Zink build over lavapipe.

## Float-depth custom borders before shadow comparison

The two depth32f/stencil8 shadow-gather failures are fixed in Honeykrisp.
Custom border reconstruction previously skipped all shadow operations. A
shadow gather must recover the raw border depth before comparing each sample;
already-compared zero/one probes lose that distinction for references outside
[0, 1]. The native M4 path now gathers raw depths with both border probes,
substitutes the custom value, and applies the sampler comparison. Sparse
residency remains separate. Other shadow operations retain their existing path.
Sampler software state retains the comparison function for image-view min-LOD;
that combination has not been separately hardware-qualified by these ES cases.

Both failing cases and the complete 24-case depth-border comparison group Pass
with the final library SHA256
`98891f7b35a025eba6271b30e1f58beb5fc03a2a4c55e52b546eb56daf47075d`.
See depth-border-group.json. Guards pass, peak 66 C. An intermediate development
build crashed due to removing a NIR source from an unlinked clone; the clone is
now inserted before source removal. That development attempt remains in the
workspace evidence and is not confused with the baseline compiler crashes.
