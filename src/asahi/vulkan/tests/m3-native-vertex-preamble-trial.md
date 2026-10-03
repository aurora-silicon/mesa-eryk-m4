# Rejected inline M3 vertex preamble trial, 2026-10-03

The same 30-frame, 3024 x 1837 Minecraft Vulkan capture still runs materially
slower on Linux than the fresh macOS reference (median 271.307 replay FPS).
The best previously qualified Linux configuration remains approximately
168 FPS. These are completion-inclusive replay timings, not live Java FPS.

Actual Apple pipeline reflection on M3 Pro / macOS build 26A428 reports
80 instructions in the shared world vertex program, with a constant
calculation phase. Static counts do not identify GPU cost or establish
comparable register-count units. The private pipeline-statistics collector
reproduces all three uninjected reference images exactly, but its factory
instrumentation makes its timing diagnostic only.

An isolated, default-off trial appends a scratch-free vertex preamble to the
bounded G15S Ioto input loader and explicitly admits that inline form in the
typed VDM metadata. It preserves terminal instructions, overlap-safe copying,
capacity checks, executable ownership, the USC window and unsupported-stage
rejections. Its initial metadata rejection was a clean command failure,
not a GPU fault; that receipt is retained.

All three gameplay images match exactly. Nine alternating same-boot pairs
measure 167.481 versus 162.694 median FPS, with paired mean -2.439%, paired
median -2.559%, and zero wins. The actual selected linked world vertex
programs shrink from 228 to 148 instructions and 74 to 48 half registers.
This instruction reduction is not a performance improvement.

A second trial performs late intra-block CSE of preamble input uniforms only
after proving no uniform/state store can modify the reserved input prefix.
Mutable output reads, boundary-crossing reads and unproven writes remain
excluded. Four added compiler controls and all 316 host compiler tests pass.
The representative offline constant program shrinks from 204 to 133
instructions; this offline variant is not the selected linked executable.
With inline vertex preambles retained, nine pairs improve 162.423 to 163.986
median FPS (paired mean +1.188%, seven wins). With vertex preambles disabled,
the same pass gives 168.015 versus 168.203, paired mean -0.352%, paired median
+0.010%, five wins: no reliable improvement.

Current slot diagnostics join all thirty world-command address lifetimes.
World tiling measures about 1.355 ms without vertex preambles and 1.492 ms
with input CSE plus vertex preambles; fragment spans remain about 2.12 ms.
The selected world vertex constant programs have 126 instructions and
42 half registers after CSE. Profiles are instrumented, and global runtime
decomposition requires independent exact counter/stage agreement. The first
ordinary profile misses a sampled retirement boundary and is retained as
unqualified for that decomposition; a second ordinary profile qualifies.

Neither trial is retained in production sources or enabled by default. No
Vulkan CTS or general vertex-constant-program qualification is claimed.
`HK_M4_PROGRAM_PROFILE` retains the useful bounded diagnostic additions
`preamble_instrs` and `preamble_gprs` for the actually selected shader.

Private libraries, source patches, exact images, all paired samples, compiler
inputs, loaded kernel options, boot identities, temperatures and verified
60-second watchdog receipts remain outside Git under
`gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/`:
`pipeline-statistics`, `vertex-preamble`, `vertex-preamble-v2`,
`preamble-uniform-cse`, `preamble-uniform-cse-fragment`, and `vertex-stage-cost`.
The diagnostic boot uses the unchanged kernel module
`e9802ae3a291b5886a98f42092f9e1764f32ad29b8dc577731e86de337416872`.
The temporary next-boot timing marker is removed; normal desktop Mesa and
cooling/watchdog policies are unchanged. M4 was not hardware-tested here.
