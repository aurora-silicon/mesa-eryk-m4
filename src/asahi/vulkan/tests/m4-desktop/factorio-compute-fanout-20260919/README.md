# Compute end timestamp fanout

Opt-in `HK_M4_COMPUTE_TIMESTAMP_COPIES=true` requires kernel feature bit5.
Up to eight additional end destinations share a closed compute tail or a
sequence of adjacent timestamp-only writes. Real dispatch timing boundaries,
resets, memory dependencies and completion flushes are preserved. Copies travel
with merged streams; streams with fanout cannot be discarded by start folding.

The new overflow probe also exposes an existing start-folding visibility bug:
a query copy reading a timestamp written at its own CDM start can still see
unavailable. Keep that producer separate from all query-copy consumers. On M4,
064825867342Z recorded CPU query9=56c7092e01 while the GPU destination retained
feed713. No-WAIT flags made this a bounded result failure rather than a spin.

Guarded J713 evidence, 20260919 UTC, watchdog60s, six sensors0.5s, cutoff95C:
- 064659571885Z: twelve timestamps passed; profiles show eight extra copies,
  then two in the overflow stream.
- 064731302696Z: initial matrix stopped at10 queries, flags0; preceding1/9
  counts passed all eight flags. Original evidence retained.
- 064940021913Z: after consumer-boundary fix, all32 combinations passed:
  counts1/9/10/64 and flags0..7 (32/64-bit, availability, WAIT). Exact groups
  of nine, monotonicity, CPU/GPU agreement, destination padding, and the initial
  fill all checked. Peak61.25C. This is not full Vulkan CTS coverage.

Kernel module13f5e54c33250d7db3a67d8be3baeb9b5efccebbc2749a39bd00a42bab3f5695.
Mesa libraryec007694a6ae49655161b03ffbd81182d07ccdf9873210b2e5f566c93b3439d2.

Original complete Factorio frame064957928442Z passes with zero differing
pixels from retained reference88594c10407e243eb0617860b8f2abc1f06c0844ec67ddba3900875d7e36796e.
Replay uses the recorded offscreen/rebind adaptation; this is not a live FPS claim.

Final bounded normal menu065049971912Z completed:117 samples over60s,
mean41.041FPS,median40.7,min21.52,max47.23,zero samples>=59. Tail12
mean37.526FPS. Peak85.75C. Scenes differ from preceding runs, so no isolated
speedup/regression is established. User requested pause after this run; all
workloads exited and graphical login remains active.60FPS remains unmet.
