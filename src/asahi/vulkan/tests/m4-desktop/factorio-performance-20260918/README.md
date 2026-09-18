# Standalone Factorio performance, 2026-09-18

Public official Linux x64 demo 2.0.77 (full standalone requires account access),
run directly through the installed FEX/muvm graphics wrapper, without Steam.
Archive SHA256: f2fc889de1924b551cf52ca2ce2c73251f9c20ee2ae38cc8e973415baffef2a1.
Installed under `/home/e/Games/factorio-demo-2.0.77`; isolated configuration and
saves under `/home/e/.local/share/factorio-standalone/demo-2.0.77`.
Launcher: `/home/e/.local/bin/factorio-standalone`, also in the desktop menu.
Guest graphics remain the R10 x86 Mesa overlay, not the native host libraries.

## Initial measurements, before optimization

* First initialization: 207.263 s, including an initial ~35 s sprite crop step.
  Sprite loading occupied 44.841–203.734 s; GPU compression was enabled and its
  self-test passed. VRAM 1117.04 MB. This run exposed a configuration-version
  prompt, so it is profiling evidence rather than a clean usable-menu result.
* Corrected valid configuration (same graphics preset, independent paths):
  169.474 s game initialization, 177.018 s observed launcher time including
  startup and final capture. Existing crop metadata avoids the first-run crop
  work; this is NOT a driver speedup. Menu visible, service remains active.
* Startup guest sampling: 74.28% idle, 16.17% PreloadWorker translated-code
  samples. Host sampling: about 78.8% idle, 3.6% systemd-journald. This phase
  differs from Steam's earlier compiler-heavy CPU profile.
* During the first sprite phase all-client kernel retirement logs recorded
  105,344 commands: sum preparation 8.434 s, active 119.686 s, recorded GPU
  span 37.704 s. Active includes host observation/firmware turnaround. Includes
  compositor work and does not establish exact utilization or causation.
  Investigate many-small-command overhead before blaming translation.
* User reports roughly 5 FPS in the menu. Instrumented frame-time/FPS
  qualification and optimization are still pending.

Both launches used the 60 s watchdog, six 0.5 s thermal channels, 95 C cutoff
and normal requested CPU maxima with CPU thermal throttling enabled. Peaks
65.25 and 66.5 C respectively. No quality reduction was made.

## Tools and evidence

Run the mirrored `factorio-standalone-observe.py` on the target under
`tools/gpu-experiment.py --seconds 520 --cpu-max-khz 4104000` with
`--output {output} --state <state> --launcher <launcher> --seconds 480`.
It refuses an existing VM, requires the armed watchdog, starts the ordinary
application in `m4-factorio-standalone.service`, retains logs and a screenshot,
and leaves it open only after initialization and liveness checks.
`factorio-profile.py OUTPUT [--toggle-fps]` samples an already active game
for 12 seconds under the same guard; it does not launch another game.

Full private provenance remains in `_provenance/experiments/`:

- `20260918T094424744162Z-factorio-standalone-demo-first-launch`
- `20260918T095204147117Z-factorio-standalone-demo-menu`

The first observer's direct SSH child disappeared after collection; the second
uses a service lifetime. The first minimal config lacked Factorio's version
marker; the second retains a valid version-13 config with only isolated paths
and windowed mode selected. Neither change is a rendering optimization.

## Fragment counter contention fixed

The FS statistics prolog issued one global atomic per covered pixel to the same
address. It now sums covered samples within the non-helper lanes of a subgroup
and elects the lowest active lane for one atomic. This preserves counts and
helper lifetimes; statistics remain enabled. The generic `elect` intrinsic was
not lowered in the secondary-prolog path (one retained compile assertion), so
selection explicitly uses the already supported subgroup minimum operation.

The CPU-checked GL microbenchmark verifies full 2560x1664 coverage, odd
2557x1661 edges, 17x13 and 1x1, five iterations each, statistics on/off.
Median warm fullscreen GPU query time: stats off 3.045 ms, stats on 7.569 ms;
with reduction off 2.936 ms, on 3.013 ms. Counts match exactly in all cases.
These timer results include the timestamp stream implementation overhead.
Original two-frame native Factorio replay has **zero differing pixels** in the
2560x1664 frame-2 image. Both use identical offscreen/rebinding adaptations.
The derivative/helper regression retains 121680 invocations and 0/116400 bad
atlas pixels. Targeted CTS: 35 Pass, 13 NotSupported, no failures, 1.822 s.

Evidence:
- `20260918T100550535469Z-factorio-statistics-baseline`
- `20260918T100632913525Z-factorio-statistics-reduce` (compile assertion)
- `20260918T100742115678Z-factorio-statistics-reduce-explicit`
- `20260918T100836716656Z-factorio-menu-stats-baseline-replay`
- `20260918T100908600687Z-factorio-menu-stats-reduce-replay`
- `20260918T101006395956Z-factorio-stats-reduce-helper-regression`
- `20260918T101033809360Z-factorio-stats-reduce-cts`

The running-menu baseline sampled 90.70% guest CPU idle. Roughly 12 render
passes plus 42 computes occurred per menu frame; a heavy render pass reached
36 ms. End-to-end 60 FPS and improved application startup remain pending.

## Real application and atlas cache

The x86_64 candidate runs via a private ICD under
`/home/e/.local/share/factorio-driver`; desktop/Steam libraries remain untouched.
First cache-producing launch with subgroup counters: 162.379 s. Then enabling
Factorio's supported `cache-sprite-atlas=true` allows the next initialization in
**12.372 s**, versus original 169.474 s (13.7x); same preset, compression,
resolution and 1117.04 MB reported VRAM. Cache file 1070486358 bytes.
The observer's 20.100 s elapsed includes VM startup, its final 3 s liveness
wait and screenshot/process collection. Do not present it as pure game time.
Most gain is the application cache, not a claim of faster cold GPU compression.
Original configuration is `config.before-performance.ini` in isolated state.

- `20260918T101130506324Z-factorio-candidate-create-atlas-cache`
- `20260918T101514148510Z-factorio-candidate-cached-start`

Live-game FPS qualification remains ongoing.

## Adjacent timestamp reset batching

EndCommandBuffer may merge consecutive reset-only compute streams, while
retaining the completion boundary before a timestamp producer or consumer.
Each reset formerly forced a separate firmware command even when followed by
another independent reset. The memory-dependency and attachment checks remain.

`vulkan-query-copy-probe.c` mode 3 writes 64 queries, individually resets each,
rewrites even queries, and copies all results and availability. Even queries
wait for availability and must be nonzero and monotonic; odd queries must be
unavailable (their values are undefined). Passes on lavapipe, baseline and
candidate hardware. Frozen timestamp CTS: 908 Pass, 26 NotSupported, no failures,
28.187 seconds runner time. Original native frame 2: zero differing pixels.
This qualifies correctness; live FPS gains are not yet measured.

Evidence: `20260918T103448735616Z-factorio-reset-merge-candidate`,
`20260918T103511642334Z-factorio-reset-timestamp-cts`,
`20260918T103600276298Z-factorio-reset-merge-frame`.

A separate kernel polling experiment (eight 10us busy polls instead of four
sleeping polls) did not improve checked tiny-command turnaround: median
0.651644 ms baseline, 0.663076 ms candidate. Source, module and initramfs were
restored and the original kernel rebooted. `gpu-turnaround-probe.py` retains
this bounded, CPU-readback-checked experiment. Do not deploy the rejected build.

`factorio-ui-step.py` checks that the actual game window exists before input or
capture. Live guest perf sampling coincided with game exits; causation remains
unresolved. Avoid it for subsequent FPS qualification.

## Dynamically uniform sampling avoids reduction setup

Implicit texture footprints can remain inside the reduction branch when both
sampler and texture descriptors are dynamically uniform. Nonuniform descriptors
retain unconditional derivatives. This preserves all advertised sampling modes.
The native retained menu frame remains pixel-identical. All 2187 selected
texture-mipmap and GLSL derivative cases completed: 1929 Pass, 258 NotSupported,
no failures, 40.444 s runner time. This run does not replace specific min/max
reduction oracle coverage.

Full-resolution replay passes (same 36 renders, includes restored state) summed
111.43 ms recorded GPU spans before, 103.87 ms after; this is not live FPS.
Prior live menu observation after reset batching: 97 downsample passes in
12.173 s (~7.97 frames/s), workload still far from the 60 FPS target.

Evidence: `20260918T104819717843Z-factorio-reset-menu-timings`,
`20260918T105006719316Z-factorio-uniform-sampler-frame`,
`20260918T105817575885Z-factorio-uniform-sampler-cts`.

The user requested no more UI interaction: measure/optimize the main menu only.
`factorio-replay.py` stops our benchmark service and runs the retained trace
with optional compiler captures, without mouse/keyboard input.
`gpu-command-timings.py` summarizes serialized retirement logs by ASID/geometry;
GPU render spans include the interval between tiling and fragment work.

## Rejected broader shader specialization; metadata-query fix

A broader whole-body ordinary path for up to eight scalar 2D/3D samplers
was pixel-correct but regressed GPU time (138.74 ms, compared to uniform
baseline 103.87 ms across 36 full-size replay passes). Disabling GCM reduced
it to 96.03 ms, still no improvement over the simpler shader with GCM disabled
(93.19 ms). The broader specialization was removed, not deployed.

It exposed a generic NIR bug: sampler metadata queries were treated as
LOD-selecting instructions and received an invalid bias source. Only actual
sampling and LOD-query operations should receive sampler LOD bias.
The retained failure is `20260918T112433607365Z-factorio-multi-sampler-frame`;
corrected replay `20260918T113035994267Z-factorio-multi-sampler-bias-frame`
is pixel-identical. Six implicit sampler oracles (ordinary, custom border,
min/max, minimum LOD, combined border/minimum LOD) pass in
`20260918T113613570583Z-factorio-implicit-sampler-oracles`.
The generator now has `--implicit`; explicit-LOD remains its default.

`gpu-turnaround-probe.py` brackets the checked command sequence with
`counter-clock.c` readings. Interpolating the architected-counter/monotonic
offset avoids NTP drift across samples. Evidence:
`20260918T111317290086Z-factorio-turnaround-clock-correlation`.
Cross-domain clock offset uncertainty means this is diagnostic correlation,
not yet proof of exact firmware-versus-host latency attribution.

## Constant-program feasibility checkpoint (not enabled)

Appending a compiler-generated fragment preamble to the native Ioto program
is not qualified. Ordinary implicit sampling passed, but custom-border sampling
returned incorrect pixels. Exporting only 32-bit values avoids the first encoder
rejection; larger application shaders still require uniform writes outside the
compact encoding's range. The opt-in prototype was removed from the driver.
The diagnostic patch remains in the workspace build directory (it also contains
pre-existing native-Buoy context and must not be blindly applied).

Evidence: `20260918T115739889858Z-factorio-native-preamble32-oracles`,
`20260918T120456449395Z-factorio-native-preamble32-frame`,
`20260918T121529486376Z-factorio-preamble-border-ir`.

`factorio-standalone-observe.py` now avoids screenshots and secondary guest
process launches by default. `--observe-seconds N` observes the untouched menu
for a bounded interval and stops the benchmark service afterwards;
`--screenshot` explicitly requests a visual reference. This implements the
user's request to stop using UI interaction while optimizing the driver.

## Reuse the preceding render's timestamp

A timestamp immediately after EndRendering can use that completed VDM's
firmware timestamp, without launching an otherwise empty CDM. Only the actual
tail render with no existing timestamp is eligible, and no active compute
stream may intervene. Post-render availability writes and transfers retain
their order. The timestamp still measures hardware completion, not a CPU clock.

All 934 frozen timestamp cases: 908 Pass, 26 NotSupported, no failures,
27.168 s runner time. Retained Factorio frame: zero differing pixels. Bounded
untouched-menu observation improved from roughly 7.7 FPS to 8.4 FPS; this is
still far below the user's 60 FPS goal. The comparison uses the same quiet
kernel, graphics settings and private x86 Vulkan build apart from this change.
Evidence: `20260918T125509398864Z-factorio-tail-timestamp-cts`,
`20260918T125553028973Z-factorio-tail-timestamp-frame`,
`20260918T125401742537Z-factorio-quiet-menu-fps`,
`20260918T125705839090Z-factorio-tail-menu-fps`.

Actual FPS is available without input, overlays or screenshots using Zink's
`GALLIUM_HUD=fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_DUMP_DIR=<directory>`.
Read the resulting `fps` file after the bounded observer stops the process.
This is application swap cadence; earlier geometry counts were inferred FPS.
The Vulkan overlay-layer attempt aborted during initialization, so it is not
used. The same driver without that layer started normally.

Follow-up constant-program diagnostic with explicit GPR copies of uniform
sources still failed custom-border pixels (`20260918T124424938263Z-factorio-preamble-gpr-oracles`).
That prototype was again removed. Register-demand, no-schedule and no-constant-
promotion variants showed no material replay improvement. Disabling the FS
counter prolog for diagnosis also showed no material improvement after the
subgroup reduction fix. Descriptor/UBO prefetch produced incorrect pixels and
remains disabled. None of these rejected variants is installed as a default.
