# Firefox performance measurements

Use the workspace `tools/firefox-probe.py` under `gpu-experiment.py` with
`--cpu-max-khz 4104000`. The guard does not raise previously imposed CPU caps;
record/check scaling_max_freq before comparisons. Keep normal thermal control.

`--cpu-profile` records all-system CPU-clock samples at 99 Hz only during the
bounded observation interval. Sampling includes idle polling: exclude idle
when interpreting costs. `--no-screenshots` skips expensive image captures for
performance iterations while retaining browser/process/guard checks.
`--env KEY=VALUE` supplies explicit diagnostic browser environment overrides.

Pass `start.js` as `--start-script` on a YouTube video, and `report.js` as
`--page-script`. The start script plays muted video, records animation and
video frame callback timing, and publishes measurements in a DOM attribute
because Marionette execution sandboxes are replaced between calls. The report
retains video quality counters, callback distributions and decoded dimensions.
Processing duration is browser-dependent and may be unavailable. These are
browser measurements, not a physical scanout or GPU timestamp claim. Retain
monitor mode and window dimensions when comparing results.
