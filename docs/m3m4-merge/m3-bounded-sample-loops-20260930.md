# M3 vertex uniform reuse and bounded texture-loop optimization

Native operand lowering creates repeated reads of immutable uniforms after
ordinary CSE. Enable the existing late CSE pass for G15S application vertex
shaders through an explicit compiler key. Constant programs and internal
helpers retain their policy. Other GPUs retain their default behavior.

Early sampled-loop preservation prevents generic sampler expansion from
multiplying compiler work. After ordinary descriptor specialization, reconsider
only sampled loops with an exact bound of at most four iterations, a loop-analysis
cost of at most 128 instructions, and no containing or contained loop. Large,
unknown-bound and nested loops retain the preservation hint. NIR's normal
unrolling heuristic still decides whether an eligible loop should expand.
No sampled result, graphics setting, API capability or floating-point rule is
changed. Overrides `AGX_NATIVE_VERTEX_UNIFORM_CSE=false` and
`HK_NATIVE_SHORT_SAMPLE_LOOPS=false` participate in shader/pipeline cache keys.

## Measurements and qualification

The first unchanged Minecraft Vulkan paired replay gives 97.8697 FPS control
versus 100.7650 FPS with vertex CSE (+2.96%). A subsequent same-binary pair gives
100.4577 FPS with CSE versus 103.4889 FPS with CSE plus bounded loop release
(+3.02%). These separate comparisons must not be combined into a claimed final
same-session percentage. All first/middle/last reference frames match exactly.
These are GPU replay scores, not live Minecraft/Java simulation FPS.

Retained benchmark diagnostics: `m3-native-schedule-paired`,
`m3-short-loops-paired`, `m3-native-motion-paired`, `m3-short-loop-tiles-paired`.
Additional fragment preambles, general code-motion overrides, graphics load
scheduling and smaller tile budgets did not establish a useful additional gain;
none is enabled by this change. Experimental commits remain on
`experiment/m3-native-scheduling-20260930`.

The default candidate passed 1429 targeted CTS cases: 965 Pass / 464 NotSupported,
no failures or guard errors. See `tests/bounded-sample-loops-20260930`.
Host loop tests cover successful short-loop unrolling and retention of large,
dynamic and nested loops. Descriptor, Ioto program and backend optimizer tests
pass. The broad `agx3_graphics_compile` fixture has pre-existing binary/rejection
expectation failures: rebuilding unchanged HEAD produced byte-identical error
output. This is not a claim that every host test passes. Logs are retained under
`build/minecraft-vertex-cse`.

## Final package and paired results

Committed package `01d77ea1a25`, installed at `/opt/asahi-m3m4-01d77ea1a25`,
has Vulkan SHA256
`ad776c1048240037dd5cde21ebdff52c38b66dcd5cff7eb37fae26616eaf63c8`.
The complete ARM64 package builds and all seven deployed library hashes match.
Six alternating warmed measurements per build in
`gpu-replay-bench/diagnostics/m3-bounded-loops-final-paired` give:

| Build | Median replay FPS |
|---|---:|
| Previous `3966e1fcc80` | 96.8542 |
| New `01d77ea1a25` | 104.7968 |

This is an 8.20% matched gain with the original 3024x1837 capture, 30 completed
frames, identical kernel/replayer and exact first/middle/last images. The older
M1 diagnostic is 93.5593 FPS: the new M3 result is 12.01% ahead, not 20%.
M1 was not rerun; its retained thermal-qualification limitation still applies.
The separate OpenGL/Zink capture measures 26.2459 median FPS across three warm
samples and matches exactly; there is no matched GL control for this change.

Six image regressions (Minecraft Vulkan and OpenGL, Satisfactory Vulkan,
Factorio Wayland, 0 A.D. Vulkan and SuperTuxKart Zink) match the previous M3
references exactly. The initial automatic Satisfactory comparison selected M4
and showed the existing one-level cross-device differences; its failed receipt
is retained. Final comparison uses the previously pinned M3 reference, without
changing tolerance. Targeted CTS used the precommit, M3-equivalent candidate;
final package timings and Minecraft image checks use the committed package.
This is not a fresh full-suite qualification or an M4 qualification.

## Deployment and recovery

Persistent and runtime graphics environments select the new prefix. The guarded
Plasma handover completed and KWin loaded the new Vulkan library. An optional
subsequent desktop-contention diagnostic aborted because it checked for KWin
exit immediately after asynchronous `loginctl terminate-user`. Its guard
retained the watchdog; logs were saved and a reboot was explicitly requested.
There were no firmware fault snapshots in that failed experiment. No result
from these contention probes is scored or establishes a desktop bottleneck.
Evidence is under `build/minecraft-vertex-cse/deployment`, including
`contention-failed` and `contention-failed-guard`.

Recovery boot `f1458389-c5d1-4679-ac31-1881506b69a5` has active plasmalogin,
KWin and plasmashell as eryk, and KWin maps the new package. GPU progress is
healthy and advancing; no replay workload remains. Boot logs do contain AOP
firmware/probe errors, two UAT firmware-exclusion timeouts and CMA allocation
warnings before successful desktop startup; this is not a claim of an error-free
boot. M4 was not touched. The public-facing local results page selects the new
Minecraft Vulkan and GL sessions; benchmark changes remain local-only.
