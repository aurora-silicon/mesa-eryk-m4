# Rejected internal uniform CSE trial, 2026-10-03

An opt-in late AGX3B CSE pass for internal clear/reload programs reduced the
Minecraft depth-upload shader from 35 to 31 compiler half registers (linked
program allocation 36 to 32), with the same 80 instructions and 12-instruction
preamble. The entire program was checked for physical-register destinations and
uniform/texture/sampler state stores before permitting this intra-block pass.
The main world shaders did not change. Five host tests exercised immutable
uniform reuse, state mutations, physical input clobbers, ISA/preamble rejection,
and block boundaries; all 317 compiler tests passed.

Both full first/middle/last image checks were pixel-identical. Instrumented
profiles joined all 492 submitted commands to their kernel GPU stage lifetimes.
The full-size single-draw group improved by approximately 18 microseconds per
frame; the world pass was unchanged. Fifteen instrumented alternating pairs
showed a paired mean +0.561%, median +0.751%, eleven wins. These are diagnostic
observations, not normal-boot benchmark scores.

After rebooting with slot timing disabled, nine alternating pairs showed a
paired mean +0.019%, median -0.112%, four wins. Separate medians were 166.919
versus 164.948 replay FPS, but the paired results do not support that apparent
regression either. Both same-boot image checks remained exact. No consistent
completed-frame benefit was established, so the compiler change, cache option,
and experimental tests were removed. No default or desktop library changed.

Evidence and the rejected patch are retained outside Git under
`gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/internal-uniform-cse/`.
`build-v2.json` pins the final binary; `ordinary-v2/` contains the ordinary-boot
image checks, timing evidence and receipts. The normal boot was
`c9ebec67-0416-4d0d-a110-bdbdc8173055`. Hardware tests used the mandatory 60-second
watchdog, bounded workload and all-channel thermal monitor. These observations
do not close the macOS performance gap or establish M4 performance.
