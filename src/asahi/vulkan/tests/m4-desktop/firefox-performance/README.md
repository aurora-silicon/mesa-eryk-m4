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

`--window WIDTH HEIGHT X Y` floats and positions only the profile-owned probe
window using Hyprland's Lua dispatchers. The helper records monitor modes and
page viewport/DPR; compare the same output and size across kernel reboots.
The profiler is stopped with SIGINT; exit 130 or -SIGINT is normal when a
profile file was retained, while forced-kill/other failures remain errors.

The helper also retains the owned Hyprland client's actual rectangle and output
in window.json. Its socket timeout exceeds the 12-second navigation timeout;
a previous 8-second transport timeout could fail before Firefox returned the
navigation result. The outer watchdog/thermal guard remains the run's bound.

`--window` now requires its rectangle to fit a connected output and requires
page visibility at completion. A disconnected TV otherwise let a probe run
hidden (zero callbacks and zero dropped-frame counter) while appearing healthy;
that is explicitly not valid playback evidence. The retained run
20260921T163556478563Z-firefox-fixed-tv-cache-lru is such an invalid measurement.
Use firefox-perf-summary.py RUN [...] to summarize callback timing, submitted
geometry/preparation costs and cache counters from retained evidence. It reports
visibility; do not interpret hidden-page counters as display performance.

start-1080.js requests YouTube hd1080 through the page's wrapped player object
and samples playback quality every 250 ms. report-steady.js retains both the
cold-start totals and a separate interval beginning eight seconds after start.
Verify the reported dimensions; requesting a quality alone is not proof of it.
The final 20260921T165256936203Z-firefox-panel-1080-visible run observed only
1920x1080 during the steady interval: 720 total frames, seven drops, 12.002 media
seconds (59.41 non-dropped frames/s). The browser and compositor captures look
correct. The kernel changes and installed eight-command configuration are
recorded under the kernel's reference/firefox/. This is a short playback result,
not a claim of perfect startup or full-browser/CTS coverage.

The probe retains HK/AGX/ASAHI/Zink and GTK scaling environment overrides as well
as the generic graphics variables, so performance variants remain auditable.

After fetching evidence, firefox-probe-cleanup.py --apply ID [...] on the target
removes only closed profiles named by retained probe results, checks that no
process uses them, and records configuration hashes and removed bytes. Without
--apply it previews the cleanup. Keep any profile still needed for reproduction;
the original user profile is never a cleanup target.

firefox-youtube-demo.py GUARD_OUTPUT opens the retained final-test profile for
an attended demonstration. Run its setup under gpu-experiment.py. It requests
1080p, resumes muted playback after asynchronous quality switching, verifies
visible/advancing video, then hands the systemd user browser unit to the user.
Failed setup stops that unit. It does not change the normal user's Firefox
profile. This user-requested playback is not an unattended benchmark loop.
Validated in 20260921T192359316985Z-firefox-youtube-user-playback; the preceding
attempt paused during the quality change and was cleaned up, not called a pass.
