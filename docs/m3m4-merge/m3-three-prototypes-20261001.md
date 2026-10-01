# Three M3 latency/recording/overlap experiments

## Complete loader reuse (opt-in)

`HK_CACHE_NATIVE_PROGRAMS=true` caches the last complete native loader per
vertex/fragment stage in each recording command buffer, restricted to M3,
ordinary graphics, no preamble/statistics/memory-writing shader/GS/tessellation.
Every draw still resolves descriptor metadata, addresses, bounds and live GPU
uniform loads. Matching requires all tail fields and every actual load range.
Equivalent immutable uploaded roots may reuse the old root; vertex-visible
fields are excluded only for the fragment ABI. Executable and constant BOs
remain retained, pooled bytes immutable, and the cache is freed before reset.
No shader-object pointer identity, CPU uniform snapshots or submit-time mutation.
`HK_NATIVE_PROGRAM_PROFILE=true` reports per-stage hits/misses on reset.

## Cross-command-buffer tiling (opt-in)

`HK_M3_CROSS_CB_TILING=true` keeps predecessor metadata across command buffers
inside one submit. Tiling may overlap only when the current entry has no vertex
execution dependency, the preceding pass writes no arbitrary shader memory and
there is no trailing barrier or start timestamp. Full fragment dependencies,
16 disjoint native pass slots, fences and packet/engine/VM boundaries remain.
This does not implement independent asynchronous kernel packet execution.

Both options default off pending hardware qualification. The synthetic resource
probe now has an optional direct-draw mode as well as indexed multi-draw. The
cross-CB probe exercises independent attachments, initial and trailing vertex
barriers, a barrier-only CB, start timestamps, explicit fences and CB reset.

Completion cadence has its own kernel diagnostics and default remains 100 us.
All hardware experiments require the existing 60-second watchdog and PMP guard.
