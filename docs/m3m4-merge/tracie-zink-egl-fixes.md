# Zink attachment tracking and EGL resize correctness (2026-09-24)

Qualified shared Mesa: `e6a74082f1dea33498e52c5e735d713bc8df9b9f`.
Both targets use the identical, hash-verified full library tree at
`/opt/asahi-m3m4-e6a74082f1d`. Kernels and traces were not modified.

## Fixes and diagnosis

- `39a09037574`: Honeykrisp defaults to ordinary attachment tracking instead
  of Zink's optional threaded render-pass lookahead. Original Godot TPS GLES3
  high (catalog 76) renders heavily overbright/striped with the optimization;
  the same corruption reproduces on Zink/lavapipe, while direct llvmpipe is
  substantially correct. `ZINK_DEBUG=norp` isolates the successful path. The
  production fix is a driver-scoped correctness fallback, not a claim that the
  internals of the lookahead optimization are repaired. `ZINK_DEBUG=rp` can
  explicitly re-enable it for diagnostics. No draws or attachment formats
  were removed and no per-application override is needed.
- `e6a74082f1d`: EGL/X11 must invalidate a changed-size drawable even on its
  software-loader path. Kopper/Zink can use that loader with a hardware GPU.
  Previously EGL reported the new window dimensions but rendering remained
  clipped to the original 32x32 replay window. Unmodified Pixel Dungeon,
  Glyphy and Pathfinder canvas_text replays now render their full windows.

Mean absolute RGB error against the supplied upstream PNG, in 8-bit channel
units with no cropping, rescaling or acceptance threshold:

| Trace | Before | After, M3 and M4 |
| --- | ---: | ---: |
| Godot TPS GLES3 high | 80.6751 | 0.7525445 |
| Shattered Pixel Dungeon | 37.8563 | 0.0325523 |

The corrected Godot and Pixel Dungeon outputs are pixel-identical between
M3/M4, but neither exactly matches its upstream PNG. Glyphy and Pathfinder
have no supplied independent PNG; full-window text restoration was visually
checked. This is not an independent conformance certification.

## Small resize regression

`probes/egl-x11-resize.c` creates a 32x32 EGL window and checks red pixels at
eight growing/shrinking dimensions, using the normal native Zink/Honeykrisp
renderer under Xvfb. The old package `39a09037574` reports 128x96 but produces
only 1,024 of 12,288 red pixels. The new package passes every pixel check at
all eight sizes on both GPUs, with GL error zero.

The first version of the probe held one acquired swapchain image across
multiple resizes without swapping; it failed on a later grow even after the
EGL fix. The retained final probe presents that acquired image before later
checks, while preserving the first unswapped resize that reproduces the trace
bug. EGL permits resize completion at swap (EGL 1.5 section 3.10.1). The
no-swap observation is retained as a diagnostic, not an additional qualified
fix or a passing test. Direct llvmpipe also behaved differently and is not
claimed fixed by this native validation.

Build with the platform EGL, GL and X11 development libraries. Execute only
inside the standard watchdog/thermal wrapper; the local validation repository
contains `tools/egl-x11-resize-check.py`, both before/after logs and hashes.

## Full OpenGL corpus regression

Separate traces-db revision `baf65903abcdf0afc7c6737bd3c1c20e31cc9148`:
84 apitrace OpenGL files and 36 RenderDoc files, 120 cases per GPU. Session
`correctness-regression`, no diagnostic Zink overrides. All original calls and
resolutions retained; the previously recorded Pioneer backbuffer-readback
adaptation remains explicit. These counts describe execution, not passes:

| Outcome | M3 Pro | M4 |
| --- | ---: | ---: |
| Image, no matched error diagnostics | 76 | 76 |
| Image with retained diagnostics | 43 | 42 |
| 32-second replay deadline | 1 | 2 |
| Total | 120 | 120 |
| Peak sampled temperature | 82.25 C | 105.5 C |

No execution-status regressions or GPU faults occurred in these bounded
campaigns. CS:GO exceeds the deadline on both; Blender cube diorama also
exceeds it on M4. RenderDoc reflection diagnostics and captured API errors
remain recorded. Four large image changes are the corrected Godot, Pixel
Dungeon, Glyphy and Pathfinder scenes. Other changed images are 0 A.D.
(mean change 0.07296), Blender (~0.0000113) and M4 Pioneer (~0.00303); no
new large image discrepancy was found. Cross-device equality alone cannot
prove correctness. D3D and Vulkan formats were not rerun for these EGL/Zink
changes; the report retains their earlier evidence and provenance.

Software-control triage reproduces Godot Thrive's transform feedback without
varyings and Blender's dispatch without an active compute shader. Furmark's
trace declares a float Layer uniform but calls glUniform1i; GiMark requests
a core profile then calls legacy matrix functions. These are retained API
diagnostics, not silently counted as driver passes or GPU faults. Rigs of
Rods invalid-draw diagnostics and 8x MSAA support remain unresolved.

A diagnostic `AGX_MESA_DEBUG=noopt` run rejected native meta shader encodings
and crashed in userspace; it is not a GPU fault, production setting or passing
experiment. Its logs remain in the separate local validation repository.

## Desktop deployment

Both normal desktop configurations now select the qualified versioned package,
with original files backed up under `/root/tracie-validation/desktop-backup-e6a74082f1d`.
Guarded session restarts passed: M3's active Plasma Wayland session and fresh
screenshot; M4's normal Hyprland session with its prior displays and no safe
mode. Both helpers verified exact loaded Vulkan/Gallium/EGL/GBM library paths
and hashes. Existing autologin preferences were retained. No kernel rebuild
or reboot was needed. The separate M3 game-launcher configuration was not
changed by desktop selection.

Raw captures, references, complete logs and deployment evidence remain in the
separate local-only `tracie-m3m4-validation` repository. No game assets are
included in this Mesa commit.
