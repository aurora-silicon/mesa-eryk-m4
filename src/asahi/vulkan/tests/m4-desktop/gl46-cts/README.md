# OpenGL 4.6 on Zink / M4

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
peak 108.25 C. The two later ranges and isolated LOD case remain pending.


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
