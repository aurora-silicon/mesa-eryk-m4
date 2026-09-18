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
