# Allocation-flag buckets

The J713 0 A.D. CPU profile `20260911T114803313970Z` attributed 1.23% of
all-core CPU-clock samples to the driver thread's `agx_bo_cache_fetch`.
Sampled instruction offsets locate almost all of that cost in list traversal.

Each size class now has sixteen flag-hash buckets. Exact flag, size and
alignment checks remain mandatory. Hash collisions only affect search cost;
the global LRU, cache budget and eviction policy are unchanged.

Both ARM64 libraries build. The workspace `tools/bo-cache-test.py` exercises
the actual internal cache with synthetic BOs and no DRM device. It passes
mixed flags (including collisions and future flag bits), size/alignment
rejection, LRU trimming, shared-object rejection and complete eviction.

Guarded actual-game run `20260911T122040990231Z` passed 180 seconds and
34 health checks, peaking at 52 C. The final scene retains buildings/shadows.
Buffer-cache lookup falls below the profile's displayed 0.05% threshold;
driver-thread all-core samples fall from 3.04% to 2.12%. Game profiling records
2.07 FPS over the last120 complete frames: 452.5 ms/frame in swap buffers out
of 482.8 ms/frame overall. This reduces CPU overhead without establishing a
substantial gameplay improvement. Main-thread wall times include waits.

Full, unmodified trace replay `20260911T122543031078Z` passes without API or
shader errors, peaking at 52 C. Frames172530,179895,187257 are pixel-identical
to the previous M4 baseline. This does not remove the existing small llvmpipe
differences or cold GPU-readback defect. No other GPU platform is qualified
by these J713 results.

## Screenshot frame pacing (2026-09-17)

Whole-second cache ages made expired objects leave together, under the cache
and map locks on the submit thread. The screenshot profile
`20260917T145941058282Z-screenshot-frame-spikes` shows missed panel refreshes
at integer monotonic seconds and BO-free/VMA-validation stacks.

Use nanosecond insertion ages and reclaim at most one stale object per cache
insertion. The 256 MiB cap, explicit byte-pressure trims, WDDM trim generations
and full teardown eviction retain their behavior. An idle process may retain
stale cached objects until its next insertion or explicit trim, as before.
`tests/test-bo-cache.c` exercises the real cache with mocked BO freeing; the
`tests/run-bo-cache-test.py HOST_MESA_BUILD --output NEW_DIR` (also exposed as
workspace `tools/bo-cache-test.py`)
compiles it with assertions and tests precise age boundaries, bounded cleanup,
pressure and byte-budget behavior without a DRM device.

## M4 executable permission normalization (2026-09-19)

Both native and virtio M4 allocators add READONLY to EXEC BOs. Cache lookup
previously searched with the caller's original EXEC|LOW_VA flags, so it never
found those cached BOs. Normalize the effective flag identity before selecting
the cache bucket. Exact equality, read/write separation and other chips remain
unchanged. The real cache test now covers this round trip and rejects using
read-only data as writable data.

`AGX_VIRTIO_MAP_PROFILE=true` optionally reports per-thread, per-label mapping
counts/bytes/wall time at one-second intervals (16 bounded buckets). It measures
mapping, not allocation/VM binding or GPU time. In the menu baseline
`20260919T013035656631Z-factorio-native-virtio-map-profile`, guest time 40.44--75.69 s
contains 66,467 new tile/compute program mappings and 4.383 s mapping wall time.
With normalization, `20260919T013344481872Z-factorio-executable-cache-menu` has
only three tile-program mappings after guest time 40 s. Both are different
animated scenes, not an isolated FPS speedup ratio. The latter completes at
35.85 FPS (25.43--37.65), peak 90.75 C under the 95 C guard; 60 FPS is unmet.
`20260919T013318065400Z-factorio-executable-cache-frame` is pixel-identical to
the original retained frame. Full CTS remains deferred.

## Command-buffer tile and compute launch allocations

Small M4 clear/reload/store and compute Ioto programs now use the existing
executable USC command-buffer upload pool. They retain the command-buffer
lifetime and alignment of ordinary draw programs, removing one BO ownership
and mapping operation per pass. Other chips and oversized programs keep the
standalone allocation path. Pooled uploads use the same coherent publication
path as existing M4 draw launch programs.

`20260919T013559882015Z-factorio-pooled-tile-compute-frame` matches the original
reference exactly. `20260919T014154571700Z-factorio-pooled-tile-compute-queries`
passes timestamp copies and ordering. The 60-second untouched menu run
`20260919T013627454028Z-factorio-pooled-tile-compute-menu` ends at 35.59 FPS
(35.16--36.08), peak 90 C under the 95 C guard. Driver-thread CPU is 2.6%
of one core over guest seconds 40--75. GPU/firmware execution now dominates;
this small pooling change is not claimed as a measured FPS improvement.
A separate vblank_mode=0 diagnostic ends at 35.86 FPS, with no improvement.
Normal game settings remain unchanged. 60 FPS and broad CTS remain pending.
