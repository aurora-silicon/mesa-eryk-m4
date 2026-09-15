# CTS catalog range 0-1,000,000 on the M4 desktop (2026-09-15)

Ordered Vulkan 1.4 CTS catalog (vulkan-cts-1.4.6.2 binary `deqp-vk-seed30-scaled-blit`), cases 0 to 999,999 in 100 segments of 10,000, run with `tools/cts-bulk-series.py` under the mandatory 60 s watchdog and 0.5 s thermal guard (20 workers, 50 cases per group, 60 s per case, first-attempt policy). Batch directory: `build/cts-vulkan14-ordered-perf/run4/0000000-1000000` (retained first attempts as `run-attempt*`, recoveries under `build/claude-fix/seg*-recover-run*`, final rechecks in `build/claude-fix/final-recheck-run1` and `build/claude-fix/s64-pf/`).

## Result

| Status | Cases |
|---|---|
| Pass | 428,519 |
| NotSupported | 571,349 |
| Fail | 106 |
| QualityWarning | 4 |
| Timeout | 11 |
| Unrun | 9 |
| Interrupted | 2 |

Total counted: 1,000,000; the 22 early-fragment cases whose unterminated `printf` confuses deqp-runner were run one per process (3 of them fail and are counted as Fail). NotSupported means the feature is not advertised and, per the campaign rule, was not implemented.

## Crashes and stops

- Segment 24 (230k): GPU hang in kernel compute pipelining with parked owners; fixed by pipelining only same-packet commands (kernel `861d021ceb6b`), recovered.
- Segment 27 (260k): CTS process crash from a stray Vulkan layer; fixed with an empty layer path in the CTS environment, recovered.
- Segment 64 (630k): deqp-runner max-fails stop after misparsing a test that prints without a newline; rerun after the fixes below (`cts-1m-r4i-s64`), the 22 early-fragment printf cases run individually.
- Segment 77 (760k): GPU fault from geometry shaders emitting no complete primitive; fixed (`ae5205b2b87`), recovered.
- Segment 95 (940k): one GPU fault during memory_model shared tests, not reproducible with the ten interrupted cases run alone; recovered.
- Segment 100 (990k): dual-source multi-attachment throughput timeouts (known); the other 3,456 cases recovered.

## Fixes (m4-asahi-port)

- `143ae36a540` hk: raw copies for A2B10G10R10/A2R10G10B10 SINT (api.copy_and_blit, segment 20)
- `cc434d83c05` hk: report the qualifying CTS release as the conformance version (segment 27)
- `26952797a5e` hk: advertise packed float formats single-sampled on G16G (image_clearing MSAA b10g11r11/e5b9g9r9, segments 28/30)
- `024f8ca490f` hk: raw-copy between 24-bit depth and colour images (api.ds_color_copy, segment 28)
- `c54b80c69c1` agx: reserve a VA region for capture/replay buffers (capture_replay_stress.seed_3)
- `ae4fe376649` agx3: keep the 8-byte half-FMA wait selector intact (compute.pipeline.basic.pk_immediate; a load wait was silently dropped)
- `ae5205b2b87` hk: complete the native bind tail when geometry lowering drops every primitive (23 geometry.emit cases and siblings, GPU fault)
- `b35d24c916f` agx3: admit shadow gradients, cube bias and cube/3D LOD queries natively (171 texture_functions cases; Metal witnesses in compiler/test/probes)
- `e5a5a26cd7c` agx: keep AGX3 threadgroup memory bases in GPRs (29 shared-atomic and memory_model.shared cases; probe tools/shared-atomic-probe.c)
- `bf3daaa6dd5` hk: program the dynamic line width into the native G16G face records (49 dynamic_state cases)
- `cd5a25269d9` agx: clamp the AGX3 sine polynomial to unit magnitude (8 precision.cos cases)
- `183b84c977d` hk: give Gen9 storage texel buffers a software PBE copy (28 sRGB storage texel buffer cases)
- `c429156e146` hk: keep integer texels in scaled-format storage read descriptors (144 texel_view_compatible uscaled cases)
- `36542e13479` hk: follow the image advertisement for unformatted storage texel buffers on Gen9 (12 a8/a2*10 sint buffer cases become NotSupported)
- `64d138e4f81` hk/agx3: sample textures natively from geometry and tessellation shaders (2 geometry.basic cases)

Each fix was verified against the 1,000-case regression set (`build/claude-perf/reg1000/cases.txt`, 815 Pass / 186 NotSupported) before installation.

## Open failures

132 cases remain non-passing after rechecking every original failure with the final library:

| Family | Cases | Diagnosis |
|---|---|---|
| glsl.texture_functions | 40 | cube and cube-array `texturequerylod`: the projected-face query form (native f74=10 face pack, f146 query) returns an integer LOD exactly one level above the reference. 3D queries, cube bias and every shadow gradient form now pass. |
| api.image_clearing | 24 | `clear_color_attachment.multiple_layers.r32g32b32{,a32}_*_sample_count_4`: layered 4x 96/128-bit clears resolve with the blue and alpha channels zero (single-layer and 1x pass); the layered multisample store of 16x16-tile formats writes 64 of the 128 bits. |
| pipeline.fast_linked_library | 22 | `blend.dual_source.multi_attachments.*` and their neighbours: throughput timeouts under 20 workers (known from the 1.0M-1.2M range), not a rendering defect. |
| graphicsfuzz | 17 | packer refusals: `stack_store` with a register index (dynamically addressed private-array stores have no native witness) and `break_if_icmp` shapes the control-flow packer does not admit (32-bit register compares with inversion, nested flag slots). |
| fragment_operations.early_fragment | 7 | `sample_count_early_fragment_tests_depth_samples_*` and `discard_early_fragment_tests_*`: with `early_fragment_tests`, samples killed by `gl_SampleMask = 0`/`discard` after the early test still store colour on G16G (1 or 2 of 4 samples). The samplemask/discard variants also print without a newline, which the runner misreads as a crash; they were run one per process. |
| draw.dynamic_rendering | 6 | `multiple_interpolation.*.with_sample_decoration.1_sample`: the `sample`-qualified reference render writes alpha 0.5 on edge pixels at one sample (smooth and centroid match). |
| draw.renderpass | 5 | `shader_invocation.helper_invocation_volatile{,_mem_model}` and the interpolation case above: after `demote`, helper status and quad operations do not follow the demoted lanes on G16G. |
| glsl.demote | 5 | `*_deriv`: derivatives and `helperInvocationEXT()` after `demote` (same root cause as helper_invocation_volatile). |
| api.object_management | 4 | `alloc_callback_fail.*`: allocation callbacks not invoked for a few object types. |
| multiview.multisample | 1 | `no_queries.max_multi_view_view_count` (renderpass and renderpass2): two cases, not investigated. |
| multiview.renderpass2 | 1 | see multiview.multisample. |

The full list with statuses is in `build/claude-fix/final-tally.json` (`remaining`).

