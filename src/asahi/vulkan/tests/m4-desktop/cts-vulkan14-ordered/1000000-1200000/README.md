# Catalog range [1000000, 1200000): first run and fixes (2026-09-15)

Frozen Linux CTS 1.4.6.2 catalog (`../catalog.json`), zero-based indices
1,000,000 to 1,199,999, twenty consecutive 10,000-case segments, 20 workers,
50 cases per group, first-attempt execution, hard 60 s case bound, the
watchdog and 0.5 s thermal guards on every segment. The corrected CTS binary
(`/root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit`, see `../rest70k/`) was used
throughout. The range covers `dEQP-VK.pipeline.fast_linked_library.blend`
through `dEQP-VK.pipeline.monolithic.image.suballocation`.

`segments.json` lists every segment attempt with counts, runner seconds, guard
verdict and evidence directory (`_provenance/experiments/<id>`); `failures.json`
and `failures.txt` list every Fail/Crash/Timeout outcome of every attempt;
`selection.json` is the frozen range selection. Retained incomplete attempts are
counted separately from completed segments.

## Outcome

| Segments | Result |
| --- | --- |
| 1-2, 5-12, 14-20 (17 segments) | complete, no Fail/Crash/Timeout |
| 3 | first attempt stopped on 1 Crash (compiler abort); rerun complete after the fix, no failures |
| 4 | complete with 10 Fail; all 10 pass after the fixes below (targeted rerun) |
| 13 | first attempt stopped on 11 Timeout (`pipeline.monolithic.blend.dual_source.multi_attachments.*`); the 1,126 Unrun/Interrupted cases were rerun as a targeted list without the dual-source cases: 864 Pass, 262 NotSupported |

Completed segment totals: 19 segments x 10,000 plus the recoveries; no
segment produced a Fail, Crash or Timeout that is still open except the
dual-source family in segment 13 (below). Peak temperature 68.25 C.

## Fixes (Mesa `m4-asahi-port`, in order)

1. `6edeb6ffcd0` agx: make the coefficient validity check scalar in
   interpolate_at_offset. A vec2 condition made bcsel produce a vec2; a
   single-component interpolateAtSample result stored it as-is and failed NIR
   validation (SIGABRT):
   `pipeline.fast_linked_library.misc.interpolate_at_sample_no_sample_shading`.
2. `76de9d99f59` agx: materialize half uniform export sources ahead of the
   export run on AGX3. The stencil reference (a 16-bit constant promoted to a
   half uniform) had no witnessed uniform-to-half copy at pack time
   ("no grounded AGX3 encoding for bitop"), so
   `pipeline.fast_linked_library.multisample.z_export.{stencil,depth_stencil,write_all}_static_atc*`
   (6 cases) failed pipeline creation with VK_ERROR_FEATURE_NOT_PRESENT.
3. `afed273b790` agx: skip the coefficient validity fallback on the native
   HAL200 interpolation path. On G16G the fallback selected the tile-corner
   constant for coefficients whose float LSB is set (A = 2/137 on a 137-pixel
   wide target), turning interpolateAtSample into 32-pixel steps:
   `pipeline.fast_linked_library.multisample_interpolation.{sample_interpolate_at,sample_qualifier}_distinct_values.137_191_1.samples_4`.
   Standalone probes (`build/claude-fix/probe/`, experiments
   `20260915T07*-claude-probe*`) show the same integer chain evaluating
   correctly, so the packing fault behind the original check remains open.
   Superseded by `c7381aaae2c`: the check is the rasterizer's constant-plane
   marker (A = B = integer 1, value in C, no 1/W factor), now tested
   directly on the native path; the six 137_191_1 cases still pass and the
   0-1M `with_sample_decoration.1_sample` cases are fixed.
4. `6dde9655d4f` hk: skip alpha-to-coverage when the fragment shader writes
   gl_SampleMask. With alpha-to-coverage in the epilog the main shader's
   coverage update ran without tests and the epilog's test-mode update wrote
   neither depth nor stencil on M4:
   `pipeline.fast_linked_library.multisample.z_export.sample_mask_static_atc*`
   (2 cases). The CTS accepts skipping alpha-to-coverage in this case.

Each fix was validated on the 1,001-case regression list
(`build/claude-perf/reg1000/cases.txt`, catalog [4000,4500) + [30000,30500) +
trim: 815 Pass / 186 NotSupported unchanged) and fix 4 also on a 676-case
monolithic multisample subset (1 Fail to 0). The ten segment-4 failures pass
in a targeted rerun (`20260915T071902104012Z-claude-seg4-fails-rerun`). The
`*_dynamic_atc*` variants are NotSupported because
extendedDynamicState3AlphaToCoverageEnable is not advertised; nothing was
implemented for them.

Kernel commits landed during this campaign are documented in
`linux/linux-asahi/drivers/gpu/drm/asahi/M4_PORT.md` ("Compute pipelining,
notification-driven completion and firmware dispatch cadence").

## Open: dual-source multi-attachment blend timeouts (segment 13)

`dEQP-VK.pipeline.monolithic.blend.dual_source.multi_attachments.<format>`
runs 2,500 iterations, each creating a pipeline with a unique blend state
over four attachments, drawing, submitting and reading back. Isolated it
passes in 22 s (Radeon 780M on the workstation: 0.7 s); under 20 workers it
exceeds the 60 s bound, and the runner's first-attempt policy stops the
segment at the first timeout. Per iteration (measured with temporary
timers, evidence `20260915T06*-claude-dualsrc-timing*`): fence wait 4.9 ms
(one 5-command packet per iteration, each command about 1 ms of firmware
turnaround), epilog compile 0.8 ms per draw (2,500 unique blend states),
command buffer begin 0.46 ms, 20 image-to-buffer copies 0.75 ms. The
Mesa disk shader cache does not help (unique keys).

This is throughput, not a rendering defect. A kernel experiment that chains
compute commands behind in-flight renders with type-4 barriers
(`build/claude-perf/drafts/compute-behind-render-barrier.patch`) passed the
regression list but did not shorten this case; the render-behind-compute
direction timed out on the first desktop frames and was reverted. The
per-command firmware turnaround is the remaining cost (see the port notes).
The dual-source cases were excluded from the segment 13 recovery list and
are not counted as passing here. With two workers and one case per group
all 42 of them complete inside the bound (40 Pass, 2 NotSupported,
235.6 s runner time, `dual-source-jobs2.json`, evidence
`20260915T071937885884Z-claude-dualsrc-all-jobs2`).
