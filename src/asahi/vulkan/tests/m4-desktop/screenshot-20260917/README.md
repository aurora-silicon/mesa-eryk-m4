# Omarchy screenshot picker — J713 / 2026-09-17

The Alt+Space Screenshot entry (`trigger.capture.screenshot`) runs
`omarchy-capture-screenshot`, then `omarchy-capture-region`. The interactive UI
is **hyprpicker -r -z plus slurp**. Grim saves the selected area afterward.

The fixes are installed in the normal desktop path. They remove redundant
full-screen uploads, preserve precise repaint regions through the compositor,
reduce Mesa upload/allocation/command-decoding costs, and eliminate an extra
full-screen copy between two KMS controllers sharing the same M4 GPU. A final
compositor fix avoids waking an output for layer content completely hidden by a
stable opaque overlay, while preserving the latest buffer for later exposure.

## Maintained source

| Project | Commits | Change |
| --- | --- | --- |
| hyprpicker | `4e83bb065fa4` | Dirty-driven frozen rendering, correct buffer attachment, idle termination |
| slurp | `6dcee0691baa` | Precise per-buffer Cairo repaint and Wayland damage |
| Hyprland | `4f15d93368ee`, `7d5bb4577ddb`, `449c7c2` | Layer damage, clip intersection, sparse damage ring, filtering footprint, hidden-layer damage |
| Aquamarine | `ec1c95baefb5` | Shared-render-device direct allocation and per-KMS import cache ownership |
| Mesa | `6a1627633dbd`, `0b2fb5498e1b`, `6b89bb9a1cb4` | BO-cache reclamation, small raw color uploads, indexed Ioto decoding |
| Kernel | `b476abf105bf` | Existing unified kernel; no screenshot kernel changes retained |

Other-project sources remain under `reference/`. The tracked portable patch,
tool, build-hash and evidence bundle is
`mesa/m4-asahi/src/asahi/vulkan/tests/m4-desktop/screenshot-20260917/`.
Its `source-commits.json` contains full revisions; `patches/bases.json` specifies
the bases for other-project patches. Mesa fixes are ordinary commits immediately
preceding the qualification bundle. Local commits have not been pushed in this task.

## Installed configuration

- Clients: `/usr/local/bin/hyprpicker`, `/usr/local/bin/slurp`. Packaged `/usr/bin`
  copies are preserved. There is no GameMode wrapper.
- Compositor: `/opt/m4-hyprland/bin/Hyprland`.
- Backend: `/opt/asahi-m4-port/lib/libaquamarine.so.13`.
- Mesa: release Vulkan and Gallium libraries under `/opt/asahi-m4-port/lib`.
  The separate assertion-enabled CTS/debug build remains available.
- Atomic installers retain backups and manifests in `/opt/m4-screenshot-backups`
  and alongside Mesa libraries. `builds.json` records exact SHA-256 hashes;
  embedded Git version strings can name the parent commit from build time.
- The normal session uses SCHED_OTHER, schedutil and its automatic cursor policy.
  Temporary GameMode, performance-governor and realtime-permission trials were
  reverted. Historical candidates in `reference/omarchy-m4-screenshot/` are not
  installation instructions.
- Both outputs remain enabled: eDP-1 2560x1664@60, scale 2; DP-1 Philips PHL 237E7
  1920x1080@60, scale 1. The unified kernel and existing DisplayPort lifecycle
  service remain in use. No temporary screenshot autologin or trace marker is left.

## Validation

Final cadence results are recorded in `validation/` in the bundle:

| Output | Measured FPS | Intervals | Missed refreshes | Maximum interval |
| --- | ---: | ---: | ---: | ---: |
| eDP-1 | 60.022 | 1,199 | 0 | 16.699 ms |
| DP-1, concurrent panel updates | 60.021 | 1,200 | 0 | 17.839 ms (sampled) |

Both are 20-second runs through the installed Omarchy wrapper. External evidence
is `20260917T164425783916Z-screenshot-qualified-hidden-external`; panel evidence
is `20260917T164537876237Z-screenshot-final-qualified-panel`. The external test
sent 21 controlled layer updates over the helper's lifetime; 20 panel completions
still occurred during that normal multi-layer picker run, with zero external
misses. The separate minimal occlusion regression verifies the conservative
suppression path; it is not a claim that all hidden desktop work is eliminated.
Final pixel evidence is `20260917T164640980953Z-screenshot-final-qualified-pixels`,
with zero differing bytes in all six comparisons. Exposure evidence is
`20260917T164610719108Z-screenshot-final-layer-occlusion`.
Measurements use real Wayland pointer/button input through the installed Omarchy
wrapper, save a PNG, and check physical completion cadence. The measured window
is 20 seconds after a 0.5-second moving warmup. Circle radius 64 ensures every
integer input delta is nonzero; earlier triangle/small-circle probes sometimes
legitimately stopped producing damage. This qualifies steady-state dragging,
not zero startup latency or every possible future workload.

Panel intervals use kernel completion timestamps. External intervals use the
native completion counter sampled every millisecond, so timestamps carry sampling
uncertainty. Counter continuity, interval coverage and PNG validity are checked
by `tools/screenshot-performance-summary.py`; a successful experiment guard alone
is not an FPS pass. Both outputs stay enabled during qualification.

Regression coverage:

- Slurp: 10,800 full reference-frame comparisons across scales 1–3, borders,
  multiple seats, overlapping rectangles, text and alternating buffers.
- Hyprland damage ring: 30 tests. Native growing/shrinking selections match
  packaged slurp and forced full repaints byte for byte; default color picking
  returns a color and exits.
- Hidden layer probe: committed red/blue changes stay hidden without panel
  redraws; removing the overlay exposes the latest blue buffer; visible red
  changes repaint. The fast path excludes partial coverage, animations,
  transparency, blur, visible-region clipping and above-lock cases.
- Mesa BO cache: real cache code verifies flags, collision handling, sizes,
  alignment, LRU, age cohorts and budget/pressure eviction.
- Ioto: 18,226,780 independent grammar/truncation checks with ASan/UBSan;
  all three raw/program-core Meson suites pass.
- Installed Vulkan: 12 raw-format upload cases, all `bad=0`.
- Installed Zink/GLES: 12 frames at 2560x1664; source bytes and partial alpha
  compositing match full uploads/redraws (`copy_bad=0 blend_bad=0`).

All target workloads were bounded and guarded by the 60-second watchdog and
0.5-second thermal monitoring with the 95 C cutoff. Detailed experiments,
including failed candidates, remain in `_provenance/experiments/`. Diagnostic
tracing perturbs timing and is not used as final FPS evidence. Desktop images
and graphics traces remain local; the Git bundle retains text/numeric reports
and artifact hashes rather than screenshots of the user's desktop.

The initial component experiment (`20260917T132903127317Z-screenshot-real-input-2`)
measured about 21 FPS for the combined stock picker. A later candidate missed one
external refresh when the other panel repainted beneath the freeze; that report
is retained rather than relabeled as a pass. `tools/README-screenshot.md` explains
build, installation and probe commands.

To restore reusable tooling from the tracked bundle, copy its `tools/` files to
the workspace-root `tools/` directory before running them; build/install helpers
resolve the source and build directories relative to that workspace. Apply each
other-project patch to the exact base recorded in `patches/bases.json`.
