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
