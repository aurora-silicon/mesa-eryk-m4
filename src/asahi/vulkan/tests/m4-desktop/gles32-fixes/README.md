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
defaults to one worker and the mandatory bounded watchdog/thermal/evidence guards,
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

## Acquired EGL buffers surviving asynchronous swapchain replacement

Both remaining EGL partial-update failures were a Zink lifetime bug. The
retained GFXReconstruct trace shows an asynchronous present returning
SUBOPTIMAL, followed by acquisition of an old-chain image, then replacement
of the swapchain during that same frame. A new undefined image was presented
although EGL had reported the age of the old acquired image. Native hardware
triggered this path; the same Zink over lavapipe did not in the reference run.

Keep an already acquired image until presentation; defer the pending recreation
to the next acquisition. Do not suppress SUBOPTIMAL or disable partial updates.
The retained before-fix trace is diagnostic evidence, not itself a regression
replay: it records the bad application's Vulkan sequence, which a Vulkan
implementation cannot repair. The unmodified EGL CTS cases test the Zink fix.

Both failures now Pass. All 251 selected buffer-age, partial-update and
swap-buffers-with-damage cases finish: 149 Pass, 102 NotSupported, zero Fail.
This includes supported resize cases. Guards Pass, peak 94 C. See egl-retest.json
and egl-preservation.json. Gallium SHA256:
`5b43d98b960517e4e7c649cece10d2a7c26a65158fec55c985be97fc7dc06dac`.

## Final combined validation

Commits: 5b6f6ebe3fb (half operands), d5fb5c438d4 (array offsets),
7b71d55edd9 (depth borders), 55d3953fe18 (Zink acquired-image lifetime).
No kernel changes were made in this fix batch.

Every original Fail and Crash configuration/case pair now Passes with all fixes
installed: **406/406**, comprising 103 original Fail and 303 original Crash.
The final GL runs use four workers, with one case per process; EGL retains its
single worker. `gl-cts-retest.py --jobs 4` reproduces this layout. Original
configuration arguments, indices, binary, bounds and guards are retained.
The original ledger is unchanged. See final-results.json for the index-by-index
mapping and final-summary.json for runs and installed library hashes.

The earlier serial rerun did not reproduce 89 scattered image failures. A
controlled parallel comparison then restored only the old Gallium library,
leaving the fixed Vulkan library installed: three runs of 28 original GLES2
failure cases produced 10 Fail / 74 Pass. Restoring fixed Gallium produced
84/84 Pass under the same four-worker, fresh-process arrangement. This confirms
the Zink bug for the reproduced failures and supports, without proving for each
case, the same explanation for the other intermittent baseline image failures.
Raw attempts remain separate; see parallel-ab.json. The upstream candidate
patch is also in workspace reference/mesa-snippets/zink-acquired-buffer-age-20260922/.

All five original per-case timeouts Pass serially without increasing the
60-second bound: texture_lod_bias_all about 39 seconds, tessellation vertex
ordering about 20.4–20.7 seconds. Keep these cases out of a contended worker pool
in future campaigns. They are substantial cases (the LOD test performs two
128-by-128 bias sweeps), not reproduced GPU hangs.

The sole-active GPUTimeout case also Passes serially in 0.067 seconds. This is
**not a GPU-hang fix**. The decoded original fault snapshot retains matching
published compute stamps and consumed compute queue cursors but zero command
timestamps; the kernel correctly keeps DMA ownership when retirement is
unproven. Do not remove that protection to make a test pass. Both original
campaign GPU incidents remain open pending reproduction/root cause. The first
incident had multiple active cases, so it is not attributed to one test.

All 412 original abnormal case records therefore have passing focused reruns,
but the full ES campaign remains incomplete: 85,993 configuration/case pairs
have not run. This is not a full OpenGL ES 3.2 conformance claim. The original
NotSupported and warning outcomes have not been reclassified in this task.

The original session-53 packed-pixel group was also retested in its original
catalog order with one worker and normal 50-case process groups: **2302/2302
Pass** in 31.865 runner seconds, guard Pass, peak 71 C. See packed-pixels.json.
This did not reproduce the rare kernel incident. No guards or retirement
checks were relaxed, and no campaign cases were silently retried or replaced.

Final state: normal display manager and desktop active; no CTS or private
compositor remains. Watchdog armed, 60 seconds, idle. CPU limits restored to
4104000/2892000 kHz. target-final.txt records the boot and installed hashes.
Existing desktop processes retain their earlier compatible Mesa mappings;
focused tests used fresh processes. No reboot or desktop replacement occurred.
