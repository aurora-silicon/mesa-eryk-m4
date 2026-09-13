# Vulkan CTS random sample, seed 20260920

Frozen selection: 1,000 unique cases drawn uniformly without replacement from
226,262 Linux-applicable cases in the pinned Vulkan CTS 1.0.2.6 must-pass list.
Only Windows-handle/WSI and Android WSI cases are excluded before sampling;
optional capabilities and prior results do not affect selection.

This sample overlaps the prior seed 20260919 in four cases (996 new cases).
The overlap is retained rather than redrawing after selection. `selection.json`
records the seed, population, exclusions and source-list hash;
`previous-overlap.json` records those four case names.

Run configuration: 20 concurrent CTS processes inside bounded watchdog/thermal
guards, using the same installed native M4 Mesa and the separately identified
`deqp-vk-input-aspects-tess-chroma` historical CTS variant. Results and timing
are retained here separately from all earlier samples. This random sample
is not a full Vulkan conformance result.

## First-attempt results

854 Pass (85.4%), 15 Fail (1.5%), one Crash (0.1%), two ResourceError
(0.2%), 128 NotSupported (12.8%). All 1,000 selected cases ran exactly once;
no retries, replacements or driver changes were made during this measurement.

Twenty concurrent processes were allowed in each bounded batch. Total wall
span was 146.011 seconds, including a 68.426-second inspection gap after the
first segment. Campaign segments themselves took 77.586 seconds, including
SSH, guard and evidence overhead; batch execution took 56.310 seconds.
Normal shader caching was retained, as in the preceding seed's runs.

Index 36 trapped with SIGTRAP in Mesa's SPIR-V parser, reporting nested
Block/BufferBlock decorations. This is a userspace process crash; attribution
to a CTS defect or driver bug needs further investigation. The first segment
retained its 50 completed cases, including the crash and already-running
children. After inspecting the successful guard and unchanged boot, the next
segment continued at index 50. See `crash-inspection.json`.

Fourteen failures report VK_ERROR_FEATURE_NOT_PRESENT and one reports
VK_ERROR_FORMAT_NOT_SUPPORTED. The two ResourceError outcomes are Wayland WSI
setup failures ("Failed to bind shell"), retained as errors rather than passes
or unsupported results. Exact cases, messages and logs are in `failures.tsv`
and `failure-logs/`; full local evidence paths are recorded in `summary.json`.

All six experiment guards completed with return code zero. Five report-level
verdicts are `api_error`, reflecting the recorded Vulkan errors above. No GPU
reset or reboot was observed. All batches verified the 60-second watchdog;
the six-channel 0.5-second thermal monitoring recorded a peak of 62.75 C and no
thermal failures. Final read-only inspection found the watchdog idle and
armed at 60 seconds, SDDM active and the same boot ID.

The native M4 driver and CTS binary hashes are recorded in `summary.json`.
The CTS executable includes the previously documented historical input-aspect,
tessellation render-area and chroma-reference corrections; see the adjacent
`cts-random-1000/fixes/` history. This is neither pristine CTS nor a conformance
certification. Unsupported cases are not passes.
