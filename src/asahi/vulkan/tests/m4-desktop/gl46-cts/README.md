# OpenGL 4.6 on Zink / M4

## CTS setup fixes completed (2026-09-22)

All six remaining failures are fixed in CTS commit `de63a4f`, under
`reference/vk-gl-cts-1.4.6.2`. The corrected 1,914-case feature selection passes
**1,914/1,914**, with no Mesa changes. The tests retain their depth-sampling
comparisons. [Fix, provenance and results](optional-features/reference/README.md).

Including these corrected-CTS outcomes, latest mixed-build tracking is
**18,106 Pass, 1,607 NotSupported, 1 CompatibilityWarning**, with no unresolved
Fail/Crash/Timeout. This is not a fresh full-suite run or certification.
Original CTS results and the earlier six failures remain preserved below and
in their original artifacts; corrected-CTS records identify their source and
binary separately.

## Before the CTS setup correction (2026-09-22)

Shadow LOD, post-depth coverage, sparse multisampling, and native coherent
advanced blending are now implemented. The final combined selection records
**1,908 Pass / 6 Fail**; all six failures are CTS depth-texture setup bugs and
pass in a separately patched diagnostic CTS binary. Original failures remain
recorded. See [implementation and validation](optional-features/README.md).

Latest mixed-build main-catalog tracking is **18,100 Pass, 1,607 NotSupported,
6 CTS-setup Fail, 1 CompatibilityWarning**. This is not a full rerun on one
final build. The previous snapshot and classifications below are preserved;
updated per-case records are under `optional-features/`.

## Baseline before optional-feature work (2026-09-22)

All **19,714** frozen main-list cases have completed records. Latest outcomes
across the recorded builds/configurations: **16,279 Pass, 3,434 NotSupported,
1 CompatibilityWarning; zero unresolved Fail/Crash/Timeout and zero unrun**.
All 32 catalog cases with an earlier failing/crashing/timed-out attempt have
a passing recheck. Historical failures and canceled workers remain preserved.
This is development coverage, not a single final-build conformance run or a
Khronos certification claim.

Twenty-two initially skipped cases now pass: 18 with a depth-buffer test
configuration, three with the required 256x256 viewport, and one after the
shader-interface, transform-feedback capacity and depth-control fixes.

Remaining NotSupported classifications (`skip-audit.tsv` has every case and
its original index; `skip-audit.json.gz` also records source explanations):

| Classification | Cases | Meaning |
| --- | ---: | --- |
| Optional extensions | 3,134 | Sparse texture2/clamp (needs sparse MSAA), fragment shading rate, EXT DSA, shadow LOD, coherent advanced blending, post-depth coverage |
| Inapplicable combinations | 211 | ES-only cases or access/primitive combinations explicitly excluded by CTS |
| Permitted sample-count limits | 76 | Requests for 8 samples above supported 4; lower sample counts execute |
| Test extension-name gates | 12 | Obsolete ARB_depth_texture name is not exposed in a core profile; depth textures themselves are supported |
| Test GLSL-version gate | 1 | Inherited GLSL330 integer-mix prototype test; the shaders45 counterpart passes |

The compatibility warning is DSA framebuffer completeness: CTS accepts the
implementation returning COMPLETE for the tested multisample/layer mixtures.
It is retained as a warning, not relabeled Pass. The original test comments
in gl4cDirectStateAccessFramebuffersAndRenderbuffersTests.cpp:3182 explain
this classification.

`final-summary.json`, `latest-coverage.json.gz`, `coverage.json.gz`, and
`results.json.gz` retain counts, latest outcomes and all supplied attempts.
`failures.json.gz` deliberately retains historical failures, including rejected
intermediate fixes. The oversized diagnostic shader hang described below is
outside the advertised output limit and is not claimed fixed by this report.

The existing guard and CTS tooling also accepts `--api gl46` on
`tools/gl33-cts-run.py` and `tools/gl-cts-probe.py`. GL46 test processes remove
inherited GL/GLES/GLSL version overrides; they do not force unsupported
versions or modify desktop settings. `cts-bulk.py --unset-env KEY` forwards
explicit removals to the recorded CTS environment after normal overrides.

The initial guarded probe reports OpenGL 4.6 core and GLSL 4.60 on native
ARM64 Zink over Honeykrisp / Apple M4. This is capability discovery, not a
conformance result. Evidence: `20260922T003548369477Z-gl46-zink-plan`.

`build/gl46-zink-20260922/catalog.json` freezes the Khronos GL46 main list:
19,714 cases from vk-gl-cts v1.4.6.2, commit
`f6a29701220f34dd1407513bfe80d74ca7b392ce`. Case-list SHA256:
`e28bbbbfd0f6c8d711554a01aa45819bdc7ca963c9426e997dfd1daaeb1d7b17`.

Initial coverage uses seed 1, 64x64, private headless EGL Wayland at 1000 Hz.
Use consecutive 5,000-case catalog ranges. The long texture LOD bias case
runs separately with one worker at its original index because concurrent
GL33 execution exceeded the unchanged 60-second case bound. Preserve every
failure and cancellation; do not automatically retry or discard it.

The full Khronos runner plan is retained under the probe's `remote/plan/`.
It includes additional configurations and lists (including no-default-context,
GTF and ES coverage); completing the GL46 main list alone is not certification.

## First driver fix

`27306c42c7c` admits derivative-free base-level gathers in the indexed
sampler path and native encoder for vertex and compute-lowered stages.
All 75 `KHR-GL46.texture_gather.*` cases pass on M4. No Zink change was
needed for the pipeline assertion caused by the rejected shader.

The retained failure capture is **after** Honeykrisp rejected indexed
sampler selection, and therefore contains the old fallback descriptor form.
A backend-only replay still refuses that form; it cannot validate the earlier
selection fix. The CTS group re-enters the complete compilation path and
passes. Do not describe this original backend bundle as a passing replay.

## Main-list progress

Range [0,5000), excluding the separately scheduled LOD case at index 866,
completed 4,984 Pass / 15 NotSupported in 440.95 s, peak 92.0 C.
All three inherited Mesa version override keys are absent from its recorded
CTS environment. Range [5000,10000) completed 4,760 Pass / 240 NotSupported in 305.87 s,
peak 108.25 C. This was the initial checkpoint; subsequent ranges and the
isolated LOD case are included in the final recorded coverage above.


## Additional failures and fixes

Range [10000,15000) stopped after a copy-image case exceeded 60 seconds.
The first attempt retains 272 Pass, 130 NotSupported, six Fail, one Timeout,
three SIGKILL Crash records and two Interrupted records; 4,586 were unrun.
The SIGKILL records occurred during coordinated termination, not an assertion.
They need explicit rechecks, as does the timed-out case. Original indices
remain in catalog.json. Untouched non-copy cases continue separately while
copy cases are isolated to investigate scheduling/duration.

`b3885da1600` copies XFB metadata before native sampler lowering clones and
frees the input NIR. The final VS/TES variant previously used the freed
metadata, producing corrupt transform-feedback results or an output_count
assertion. The original failure and the subsequent assertion are both kept.
With the lifetime fix, texture_view.view_classes narrows to representation
mismatches; the same Zink on lavapipe passes. `7845f806b22` uses raw tiled
copies for half-float, R11G11B10, and SNORM formats so NaN payloads and the
two SNORM encodings of -1 survive. The original texture-view case now passes.

Sparse 3D cases exposed a mismatch between format queries and the disabled
sparseResidencyImage3D feature. Rejecting the format query alone does not
satisfy GL_ARB_sparse_texture. Enabling M4's existing slice-based sparse
mapping with explicit nonstandard 3D block sizes passes all 120 supported
GL46 sparse 3D cases (40 further cases are NotSupported). Direct Vulkan
sparse validation also passes: `257d111219d` is validated by all 2,642 frozen
Vulkan sparse_resources cases containing `.3d.`: 2,022 Pass / 620 NotSupported,
28.92 s, peak 104.25 C. Evidence: 20260922T011124390097Z-gl46-vk-sparse3d-all.
No other chip was enabled.

Combined guarded validation: 20260922T010718398173Z-gl46-view-sparse3d-fixed,
121 Pass / 40 NotSupported, 9.83 s, peak 68.25 C. The six failing first-attempt
cases are preserved rather than overwritten by these rechecks.


The non-copy continuation completed 2,038 Pass / 776 NotSupported before a
new native driver rejection. shader_subroutine.globals_sampling_output_discard_function_calls
uses a GS triangle strip with a PointCoord-reading FS. Those values are
undefined outside points, but the program is legal. `32b25c018bf` removes
the topology restriction while retaining coefficient shape checks. All 28
GL46 shader_subroutine cases pass (12.37 s, peak 83 C). The first assertion
and three SIGKILL peer records are retained under range3-rest. Remaining
untouched/canceled cases continue under range3-rest2.


range3-rest2 completes all 1,463 selected cases: 1,061 Pass / 402 NotSupported
in 59.05 s, peak 104 C. The original LOD case at index 866 passes alone
in 40.01 s with the unchanged 60 s case limit. The previously timed-out
R11G11B10 3D-to-rectangle copy passes in 6.47 s after the raw copy fix with
one worker. This recheck changed both the driver and concurrency; it does
not independently attribute the original timeout to contention alone.
Evidence: 20260922T011443181526Z-gl46-duration-recheck. Remaining copy cases
run with two workers; all first-attempt outcomes remain recorded.

## Maximum shader interfaces

UVSW destinations and staging sources have different limits. The old compiler
rejected output 128 because it treated the 256-half staging file as the output
address space. Higher destinations now use the existing serialized COPY/UVSW
path with a bounded six-slot staging bank; ordinary bundles retain their layout.
21 vertex backend tests pass, including destinations through 255.

Guarded CPU-checked GL probes render 31 vec4 outputs (124 components), and
29 vec4 outputs plus eight live clip distances. The latter checks 58 draws
with changing UBO values and every selected output:
`20260922T024631859137Z-gl46-maximum-io-clips`, peak 63.25 C.
These probes used the separate experimental Vulkan limit increases.

The 32-vec4 GL probe remains unresolved: it compiles but stalls the fragment
stage, including with a summing FS. Retained failures:
`20260922T023808812411Z-gl46-maximum-io-streamed`,
`20260922T024110212541Z-gl46-maximum-io-capture`,
`20260922T024339307948Z-gl46-maximum-io-sum`. Each guard saved evidence and
recovered by reboot. It exceeds the proposed Vulkan output limit once position
is included, but Zink accepts this GL shader. Do not call the entire enlarged
GL interface qualified on the strength of limit-query tests alone.

The M4 Vulkan interface limits now expose 128 components with corresponding
UVS/coefficient guard bounds. Guarded validation: all 149 GL46 shader-limit
cases Pass in 7.01 s (peak 67.75 C), including the formerly failing limit
queries and formerly timed-out geometry max-output case. Direct Vulkan
interface-boundary checks: 7 Pass / 8 NotSupported; the latter exceed the
limit after built-in outputs are counted. Evidence:
`20260922T024659615465Z-gl46-gl-io-limits-fixed`,
`20260922T024856920494Z-gl46-vk-io-limit-fixed`.

OpenGL 4.6 section 11.1.2.1 also counts all written outputs against
MAX_VERTEX_OUTPUT_COMPONENTS, including position. Its separate
MAX_VARYING_COMPONENTS limit excludes position. The diagnostic 32-vec4 plus
position shader exceeds the former limit (132 > 128), so it is not a required
128-component transport test. Its acceptance followed by a hang remains
recorded; it must not be reported as a newly fixed case.
Source: https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf

The retained later main-list results include the remaining copy timeout,
compressed-1D fix/recheck, final-range failures, and continuation. These are
separate attempts across builds, not one conformance run.

## Cull-distance prolog with sample masks

The GLSL reproducer renders all black only when cull distance is present.
The discard-only prolog expressed (killed, ~killed), but the native backend
classified its dynamic live operand as running tests and acquired/released
the depth/stencil dependency before the main sample-mask shader. Canonicalize
the live operand to zero when the part is explicitly not running tests.

Before: `20260922T025638608557Z-gl46-sample-mask-clips` and retained compiler
inputs in `20260922T025833829270Z-gl46-cull-mask-capture`. Without cull distance
the same shader passes. After: `20260922T030052720433Z-gl46-cull-mask-kill-fixed`
passes all four mask combinations. All 417 GL46 SPIR-V, cull-distance and
sample-variable cases complete: 363 Pass / 54 NotSupported, no failures,
11.11 s, peak 72.5 C. This includes the original SPIR-V built-in validation
failure. No Zink or SPIR-V frontend workaround was used.

The original tessellation vertex-ordering timeout passes alone in 21.52 s
with the unchanged 60 s bound (`20260922T025347600490Z-gl46-tess-ordering-isolated`).
The 3D framebuffer-layer failure still reproduces in isolation and remains
open; byte readback passes while float-converting readback fails.

## Native 3D sampler extent

The final framebuffer-layer failure is a real advertised-capability error.
A distinct-color-per-slice probe at depth 8192 samples slice floor(z/4), and
at depth 16384 samples floor(z/8), while byte copies return the requested
slice. Depths 192 and 2048 match exactly. G16G normalizes sampled coordinates
against at most 2048 slices. Evidence:
`20260922T031646213685Z-gl46-3d-depth-tags` (24 diagnostic mismatches).
Apple also documents a maximum 3D dimension of 2048:
https://developer.apple.com/metal/capabilities/

Report 2048 for M4 maxImageDimension3D and use that same property for 3D
image-format maximum extents and mip levels. 1D/2D and other chips retain
their previous values. This corrects an unsupported claim; it does not
remove the test or hide an execution failure. The original DSA case passes
in 28.01 s (`20260922T031810639318Z-gl46-dsa-layer-fixed`). The diagnostic
probe defaults to advertised limits; PROBE_OVER_LIMIT=1 explicitly enables
negative oversized allocation experiments.

All 181 remaining copy cases pass in 754.33 s, peak 93 C:
`20260922T030347838553Z-gl46-copy-rest`. This completes recorded coverage of
the frozen 19,714-case main list. Prior failures and peer cancellations
remain in the retained attempts; these rechecks span multiple builds.

Vulkan 3D image-format queries and RGBA8 3D striped blits: 394 Pass /
30 NotSupported, no failures, 0.82 s runner time, peak 67.25 C
(`20260922T031854579985Z-gl46-vk-3d-format-fixed`).

## Repeated named framebuffer resolve crash

`PROBE_DSA=1` in gl-sample-mask-probe.c reproduces an assertion in tc_blit:
`has_resolve` survives _tc_sync while `ended` is reset, so the next resolve
is treated as a duplicate. Retire the completed resolve flag and resource
references on synchronization, while preserving attachment-use metadata.
Baseline: `20260922T031711097359Z-gl46-dsa-resolve-baseline` (SIGABRT).
Fixed: `20260922T032346882258Z-gl46-dsa-resolve-only` (four masks pass).

An initial broader reset of all ended-pass metadata regressed scissor_blit
(zink_clear depth-use assertion), and was rejected before commit. The original
library passes that case. Both failed and baseline attempts are retained.
The narrower final change completes all 755 framebuffer-blit, DSA,
multisample and sample-variable cases: 678 Pass / 76 NotSupported /
1 CompatibilityWarning, 35.30 s, peak 69.75 C
(`20260922T032405722078Z-gl46-resolve-regressions-fixed`).

Required-core skips: all 18 fragdepth cases pass with the D24S8 FBO config;
all three texture_query_lod cases pass with a 256x256 viewport. Original
64x64/default-depth skips remain separate records. The tessellation interface
case now reaches its next gate: transform-feedback interleaved components
are limited to Gallium's default 64 despite Vulkan supporting 128.

## Capture position before depth conversion; expose native depth control

Raising Zink's interleaved XFB capacity from its generic 64-component default
to the Vulkan-supported 128 exposes the maximum TES interface case. All
user attributes match, but captured position.z is 0.5 instead of 0.0: Zink's
shader fallback for missing VK_EXT_depth_clip_control runs before capture.

Implement M4 depthClipControl and its dynamic state in Honeykrisp. Like the
existing Gallium Asahi path, convert [-1,1] depth only in the hardware vertex
or generated geometry copy shader, after software VS/TES/GS outputs and XFB.
Reuse a root-table padding half for the dynamic coefficient; include the
feature in shader keys. The final lowering collects partial/conditional
position writes in a temporary before conversion, and preserves Z exactly
when negativeOneToOne is false. Other chips retain their feature boundary.

The first vec4-only lowering exposed scalar TES position stores in Vulkan
transform-feedback tests; its assertion and canceled peers are retained.
The corrected implementation completes **521/521 Vulkan depth-clip-control
cases**, covering monolithic pipelines, both library modes, all four shader
object modes, dynamic updates, and VS/TES/GS transform feedback. Runner time
2.65 s; peak 100.75 C under the authorized thermal regulation and watchdog.
Evidence: `20260922T033552331492Z-gl46-vk-depth-clip-control-fixed`.
The GL transform-feedback group plus newly enabled maximum TES interface
case completes **39/39 Pass** (`20260922T033141816684Z-gl46-xfb-clip-fixed`).

Final conversion build: 422 GL clipping/cull-distance, tessellation,
transform-feedback and fragment-depth cases complete 406 Pass /
16 inapplicable NotSupported in 65.57 s, peak 102.75 C
(`20260922T033631999754Z-gl46-clip-regressions`).

The raw float/SNORM copy fix also completes its 566-case Vulkan regression
selection: **566 Pass**, 1.02 s runner time, peak 69 C
(`20260922T033946817892Z-gl46-vk-raw-copy-fixed`).
