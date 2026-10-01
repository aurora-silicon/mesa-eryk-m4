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

Both options default off after hardware qualification found no reliable gain.
The synthetic resource
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
zero. Both options remain disabled by default.

The first canonical completion diagnostic module was installed for a normal
graphical boot (module SHA256
`8f854cbe38946323a69bb451c07f135bc1d43dba3ea6f1266f2e0f77204f4b39`).
Nine alternating same-binary Zink pairs show a -0.0300% median paired change for
25 versus 100 us (4/9 positive); all saved images match exactly. Keep 100 us.
Two wrapper preflight errors happened before GPU execution; their guard receipts
remain recorded, the cadence was restored, and there was no driver fault/reboot.
Full evidence: `build/m3-three-20261001/` and the local benchmark runs.

## Final paired timings

Each row uses nine alternating control/candidate pairs of the original
Minecraft gameplay capture, measuring 30 completed replay frames. Each policy
first passes its approved image comparison and receives a warmup. These are
replay FPS, not live Java FPS. FPS columns are separate descriptive medians;
the paired change is the effect estimate. Different rows have independent
controls, so differences between their absolute medians are not gains.

| Change | Renderer | Control FPS | Candidate FPS | Median paired change | Positive pairs |
| --- | --- | ---: | ---: | ---: | ---: |
| Completion fallback 100 to 25 us | Zink | 75.77 | 76.21 | -0.030% | 4/9 |
| Completion fallback 100 to 25 us | Vulkan | 105.28 | 105.03 | +0.140% | 5/9 |
| Complete loader reuse | Zink | 81.49 | 81.32 | -0.108% | 3/9 |
| Complete loader reuse | Vulkan | 105.25 | 105.27 | -0.175% | 4/9 |
| Cross-CB tiling | Zink | 80.55 | 81.45 | -0.257% | 4/9 |
| Cross-CB tiling | Vulkan | 104.01 | 105.25 | +0.261% | 7/9 |
| CPU preparation overlap | Zink | 80.25 | 80.92 | +0.838% | 6/9 |
| CPU preparation overlap | Vulkan | 105.14 | 105.19 | +0.001% | 5/9 |

All eight descriptive bootstrap 95% intervals for the paired medians contain
zero. No reliable Minecraft gain was established, so none of the policies is
promoted. This does not establish equivalence or prove no effect in other games.
The kernel CPU-preparation experiment releases the runtime mutex only during
waits; the single worker still owns GPU packet execution. No asynchronous
independent kernel packets were implemented. Its preparation counter stayed
zero in these Minecraft comparisons and CTS tests, so these runs do not qualify
concurrent new VM/timestamp preparation or runtime unbind.

A complete-loader profile of the whole Minecraft Zink replay observes 548
vertex hits / 689363 misses and 241 fragment hits / 110014 misses. The existing
indexed-multidraw fast path returns before this instrumentation, and misses
include ineligible shaders. This is the whole setup plus replay, not only the
30 timed frames. The synthetic hit counts therefore do not justify predicting
application gains.

## Final correctness, identities and restored policy

With loader reuse, cross-CB tiling and CPU preparation overlap enabled together,
the frozen targeted CTS catalog completed all 590 cases: 526 Pass,
64 NotSupported, no failures. Retained image checks pass for Minecraft Zink,
Minecraft Vulkan, Satisfactory Vulkan gameplay, Factorio Linux Wayland,
0ad Vulkan and SuperTuxKart Zink. Original references and tolerances were kept.
The compiler uniform encoder test passes 28406 checks. Final resource oracles
pass 144 scenarios per policy (288 in total); the seven cross-CB scenarios pass
with both policies and on host lavapipe.

The final CPU-overlap kernel module SHA256 is
`a50883a71d71355fe460cd810844e1d19d5ed198160952114149ef0caaf659be`,
built from the canonical M3 source through `576939f34386`; two intentional
guarded module deployments separate it from the first completion module.
Mesa implementation is committed through `68672c56828`. Loader and cross-CB
comparisons use the isolated Vulkan binary identified above; completion and
CPU-overlap comparisons use normal Vulkan SHA256
`82098edb14e583ae7c562e14e004a4335ace70ff4b23bcf6cb535ae2ed4153c7`.
Each row's exact capture, replayer, kernel, Mesa identities and boot ID are in
the qualification JSON.

All 230 completed guarded-run receipts were checked for the actual 60-second
watchdog timeout, completed phase, watchdog disarm and all exposed temperature
logs. This includes the retained rejected loader prototype. The highest exposed
temperature was 97.0 C; these are PMP/CPU-derived channels, not a qualified
dedicated GPU sensor. Startup 95 C, regulation 110 C and emergency 120 C
policies remain unchanged. The two wrapper preflight errors described above
preceded GPU execution; no unexpected reboot or driver fault occurred.

Normal boot uses 100 us waits, `submit_overlap=0`, the original graphical
autologin session and `/opt/asahi-m3m4-f568f1256f8`. Experimental Mesa prefixes
are isolated from that selection. Main benchmark report selectors are unchanged.
The diagnostic report is
`/home/council/dev/gpu-replay-bench/diagnostics/m3-three-20261001/results.html`,
with `qualification.json`, the frozen indexed catalog and final policy receipt
beside it. The benchmark repository is committed locally only; bulk captures,
images, modules and raw logs remain outside Git. No M4 hardware experiment or
hardware qualification was performed for these M3-only options.
