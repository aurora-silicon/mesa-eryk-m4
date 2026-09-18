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
