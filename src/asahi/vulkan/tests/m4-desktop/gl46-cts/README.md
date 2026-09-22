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
CTS environment. Remaining ranges and the isolated LOD case are pending.
