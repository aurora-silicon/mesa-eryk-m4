# AGX3 slot allocator CPU cost, September 22

Source fix `6a6f211bc51`, replay output helper `14ff3904b9e`.
Reuse of a retired scoreboard slot previously scanned all 33,282 dependency
tokens, including 32,768 static TPS halves. Track the written TPS prefix;
continue clearing the GPR and staging/special ranges. Without static stack
accesses, each reuse now visits 514 entries. No slot-policy or wait changes.

## Measurement

32 retained Factorio shaders, host x86_64 compiler replay, 20 paired rounds
with alternating before/after order, no concurrent build in `compare-idle`:
median corpus wall time 0.490122 s before, 0.470811 s after; median of paired
percentage reductions 3.5553%. Process startup is included. This is a host
compilation result, not M4 CPU timing, shader execution time or game FPS.
Every shader binary is byte-for-byte identical (including the empty shader).
Original bytes and logs are in `identical-shader-binaries.tar.gz`; input and
executable hashes and all timing samples are in the comparison JSON.

`compare` is the exploratory run (2.8551% median paired reduction); a release
build overlapped some of it. `compare-idle` removes that source of contention.
The initial 178-sample host profile assigned about 4.5% to agx3_slot_assign;
this small sample identified a lead, not a precise cost estimate.

## Validation and remaining limits

- All 120 slot checks pass, including under ASan/UBSan. Nine new boundary
  cases ensure retired aliases cannot consume a later generation's wait.
- 262,144 assignments across 2,048 deterministic dependency streams and
  1–8 slots match the original function exactly. Original/candidate C and
  reusable differential harness are retained.
- Mesa slot, static-spill and workgroup tests pass. The broader graphics
  compiler test fails identically with the original function: see
  `graphics-baseline-comparison.json`. No claim that this test suite passes.
- Native release Gallium and Vulkan builds pass. New binaries are built on
  the host only, **not installed**; preserve the pinned descriptor-fixed CTS
  campaign's driver. Hardware CTS results in the adjacent directory exercise
  the prior installed build, not this CPU optimization.
- The replayer propagates binary-output I/O errors; the saved negative check
  returns 1. Saving binaries does not request any NIR transform.

Tool usage is in `README-compiler-comparison.md`. Copy the tools back into
workspace `tools/` before use. Rebuild the two host replayers from the desired
sources; `inputs.json` names the original retained capture corpus. All host
work is GPU-free. Raw profiling data and executables remain under workspace
`build/agx3-slot-cost-20260922/`. No upstream push was requested or performed.
