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

## Direct parameter follow-up

The first complete-loader prototype used a fresh parameter upload for each
vertex draw. In the direct-draw oracle it had no vertex hits and its Minecraft
Zink nine-pair median change was -4.0574% (3/9 positive), so it was not promoted.
The follow-up reuses only byte-identical CPU direct-draw parameters and DrawID
uploads; indirect argument contents/addresses are untouched. Cache enablement
and relaxed CB-entry dependency metadata are restricted to the opt-in M3 path.

Follow-up isolated Vulkan SHA256:
`375285446a65101f2225db2b0bebee74dc91ecfc2c47e3d5cad3855efad7d6b3`.

Both settings pass 48 direct CPU-update, 48 direct GPU-buffer-copy update and
48 indexed multi-draw GPU-update pixel oracles. With complete reuse enabled,
each direct-draw probe records 6158 vertex and 6158 fragment hits, with 50 misses
per stage. These are oracle hit counts, not application FPS gains.
All seven overlap oracle scenarios pass on M3 and host lavapipe. Architecture
logs show one early-tiling permission in the independent-attachment scenario,
and zero in each of the six ordering scenarios, with the option disabled always
zero. Both options still default off until application timings are evaluated.

The canonical completion diagnostic module is installed for a normal graphical
boot (module SHA256 `8f854cbe38946323a69bb451c07f135bc1d43dba3ea6f1266f2e0f77204f4b39`).
Nine alternating same-binary Zink pairs show a -0.0300% median paired change for
25 versus 100 us (4/9 positive); all saved images match exactly. Keep 100 us.
Two wrapper preflight errors happened before GPU execution; their guard receipts
remain recorded, the cadence was restored, and there was no driver fault/reboot.
Full evidence: `build/m3-three-20261001/` and the local benchmark runs.
