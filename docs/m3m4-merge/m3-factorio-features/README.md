# M3 Factorio feature qualification — 2026-09-23

The user authorized implementing the M3 features required by the retained M4 Factorio recordings. This extends the original merge qualification scope.

## Pipeline statistics

Enable the shared G15S/G16G counter implementation and fragment statistics uniform/native prolog. The counter address must be uploaded through the native IOTO uniform prefix; changing only the public feature bit does not establish the shader ABI. The geometry/tessellation/vertex-storage gates remain independent.

Candidate installed separately at `/opt/asahi-m3m4-factorio-stats`, leaving the normal desktop unchanged. Six smoke cases and 268 reproducibly selected cases from the pinned Vulkan 1.4.6.2 catalog yielded **233 Pass, 41 NotSupported, zero failures/crashes/device losses**. The skips retain disabled geometry/tessellation dependencies; they are not passes. Coverage includes compute, input assembly, vertex, clipping and fragment counters, secondary buffers, resets and result copies.

Raw QPA, identity, kernel logs and 0.5-second thermal telemetry: workspace `build/m3-factorio-features/{stats-smoke,stats-focused}`; compact exact results are retained here. Every segment used the existing M3 guard with a verified 60-second watchdog and 120 C emergency policy. This is focused feature validation, not full GL 4.1 or Factorio qualification.
