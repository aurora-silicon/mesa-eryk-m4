# M3 Factorio feature qualification — 2026-09-23

The user authorized implementing the M3 features required by the retained M4 Factorio recordings. This extends the original merge qualification scope.

## Pipeline statistics

Enable the shared G15S/G16G counter implementation and fragment statistics uniform/native prolog. The counter address must be uploaded through the native IOTO uniform prefix; changing only the public feature bit does not establish the shader ABI. The geometry/tessellation/vertex-storage gates remain independent.

Candidate installed separately at `/opt/asahi-m3m4-factorio-stats`, leaving the normal desktop unchanged. Six smoke cases and 268 reproducibly selected cases from the pinned Vulkan 1.4.6.2 catalog yielded **233 Pass, 41 NotSupported, zero failures/crashes/device losses**. The skips retain disabled geometry/tessellation dependencies; they are not passes. Coverage includes compute, input assembly, vertex, clipping and fragment counters, secondary buffers, resets and result copies.

Raw QPA, identity, kernel logs and 0.5-second thermal telemetry: workspace `build/m3-factorio-features/{stats-smoke,stats-focused}`; compact exact results are retained here. Every segment used the existing M3 guard with a verified 60-second watchdog and 120 C emergency policy. This is focused feature validation, not full GL 4.1 or Factorio qualification.

## Geometry, tessellation and graphics storage

The shared native Poly implementation now admits G15S: geometry/tessellation execute through compute with the existing native root uniform and generated-draw paths. Point-size and vertex storage features follow this independently tested gate.

All 1,302 cases in the pinned `geometry.*` and `tessellation.*` groups were attempted: **1,054 Pass, 248 NotSupported** by final case outcome (native float64 and geometry invocation-count limits). One original 10-second harness timeout is retained; its continued GPU retirements and successful 10.23-second isolated rerun establish a duration limit, not a GPU hang. The six related variants passed within 35-second case / 45-second watchdog-guard segments. Eighteen initial smoke cases also passed.

All three original M4 Factorio GL recordings now replay with their original GL 4.1 context request: Linux X11, Linux Wayland, Proton OpenGL. Each produced three 3840×2160 sampled images with **exact RGB equality to the reviewed M4 reference**, without context adaptation or skipped calls. Private benchmark evidence is local in `gpu-replay-bench/results/factorio-*/m3/20260923T1707*` and `20260923T170809*`. No assets belong in this public Mesa repository. Zink now derives GL 4.6/GLES 3.2 from the features; this focused qualification is not a full conformance claim.

The broader host Asahi unit suite yielded 109 Pass and 10 failures. Its failing compiled targets do not depend on the runtime files changed here (dependency audit retained locally); these results are not presented as a clean unit-suite pass.
