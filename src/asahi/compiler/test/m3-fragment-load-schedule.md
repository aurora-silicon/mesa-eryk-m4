## Rejected fragment load scheduling

The existing AGX3b post-register-allocation load scheduler normally runs on
compute shaders. A private G15S-only compiler-key control tested it on ordinary
fragment shaders too, retaining internal/preamble exclusions, physical register
hazards, control flow, coverage and memory order. Its Vulkan pipeline/disk-cache
identity included the control. Both variants match all three reference images;
309 compiler tests pass, including implicit-LOD and coverage-order tests.
Focused frozen-index CTS gives 219 Pass / 171 NotSupported / zero failures.

The initial five alternating pairs suggest +2.63% by separate medians. A further
15 pairs expose bimodal run variation: separate medians show 158.58 / 161.85 FPS,
but paired mean change is only +0.22%, paired median +0.039%, and the candidate
wins only eight pairs. This does not establish a reliable gain. Diagnostic
shader hashes confirm the main 260/282-instruction fragment binaries changed;
their 88-register, zero-scratch footprint did not. The experiment is rejected,
its driver/compiler control removed, and its immutable patch, library hash,
checks, all timings and CTS receipts retained under `fragment-load-schedule`.
The two additional scheduler regression tests remain useful independently.

Evidence: `/home/council/dev/gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261002/fragment-load-schedule/`.
Candidate Vulkan SHA256: `23fbda7ff9d7547957e46975810ace6315438057d27a8e705a0ab05a439b48f8`.
Kernel SHA256: `f542a8e7716c0f692b0b0ca9853bdfbc481b0200b775175b65e3efce8a2d5df0`.
Frozen CTS catalog SHA256: `72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
Every hardware run used a 60-second watchdog and the 0.5-second thermal guard.
This is focused testing and negative performance evidence, not a full CTS or
macOS parity claim. Desktop Mesa selection remains unchanged.
