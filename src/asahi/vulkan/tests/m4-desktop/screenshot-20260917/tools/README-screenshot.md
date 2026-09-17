# Omarchy screenshot performance on J713

The launcher entry `trigger.capture.screenshot` runs
`omarchy-capture-screenshot`. Its smart/region picker is
`omarchy-capture-region` → `hyprpicker -r -z` (frozen desktop) + `slurp`
(selection). Both are Cairo/wl_shm clients; Hyprland uploads their images
through OpenGL → Zink → the M4 Vulkan driver. `grim` runs after selection.

Maintained client/compositor changes are in `reference/hyprpicker-0.4.7`,
`reference/slurp-1.5.0`, `reference/hyprland-0.56.1`, and `reference/aquamarine-m4`. Mesa changes
are in `mesa/m4-asahi`.

## Reproduce and measure

- `build-mesa-desktop.sh` builds Vulkan and Zink in a separate release directory;
  the assertion-enabled `mesa-m4-asahi-arm64` CTS/debug build remains available.
- `build-hyprpicker.sh` cross-builds the frozen overlay with the Omarchy sysroot.
- Slurp: `meson setup build/slurp-m4-arm64 reference/slurp-1.5.0
  --cross-file build/mesa-m4-aarch64.ini --buildtype release
  -Dman-pages=disabled`, then `ninja -C build/slurp-m4-arm64`.
- `build-hyprland.sh` incrementally builds the compositor.
- `build-aquamarine.sh` builds the DRM backend; installer `--backend` installs
  it, and `--compositor` installs Hyprland. Restart the session under a guard.
- `install-screenshot-tools.py` hash-checks and atomically installs clients in
  `/usr/local/bin`, preserving `/usr/bin` package files and any prior overrides.
  It writes manifests/backups under `/opt/m4-screenshot-backups`.
- `wayland-pointer-probe.c` sends real relative pointer motion and button events
  through wlr-virtual-pointer. IPC cursor warps alone do not exercise slurp's
  drag path. Build on target with the generated protocol from
  `reference/hyprland-0.56.1/protocols/wlr-virtual-pointer-unstable-v1.xml`.
- `screenshot-performance-probe.py OUTPUT --phase combined --duration 10`
  measures physical panel completions and external KMS frame counts while
  dragging. Use `--x 30 --y 30 --width 1000 --height 650` for a large panel
  selection. `--phase wrapper` runs the installed Omarchy smart picker and saves
  the resulting PNG in the evidence directory. Other phases isolate baseline,
  cursor policy, freeze, and selection. Optional perf/Wayland/strace diagnostics
  perturb timing; do not use their FPS as final acceptance measurements.
- `screenshot-pixel-probe.py OUTPUT` captures native scanout after growing and
  shrinking selections, first with the optimized client and then packaged
  slurp over the same frozen desktop. Compare pixels after the 32-byte scanout
  header. The Cairo test in `reference/slurp-1.5.0/tests/render-damage.c` also
  compares 10,800 complete reference frames at scales 1–3 and border widths
  1–6, with one/two seats,
  overlapping rectangles, text, clipping and alternating buffers.

Run target probes ONLY inside the mandatory watchdog/thermal wrapper:

```
python3 tools/gpu-experiment.py --name screenshot-check \
  --hypothesis 'Installed picker sustains the panel refresh rate' \
  --expect 'PASS: screenshot component' --seconds 40 --cpu-max-khz 4104000 -- \
  python3 /root/screenshot-performance-probe.py '{output}' \
  --phase wrapper --duration 10 --x 30 --y 30 --width 1000 --height 650
```

A `pass` from this runner means the bounded command and guards succeeded.
It does **not** by itself mean 60 FPS: inspect `screenshot-performance.json`
and physical completion intervals in `*.present-history.json`.

## Findings

1. Hyprpicker redrew every full-screen SHM buffer on every frame callback even
   when frozen. Dirty-driven rendering removes those redundant uploads. It
   must attach the buffer actually rendered, preserve dirty state while busy,
   and wake its idle event loop on SIGTERM.
2. Slurp repainted and damaged the full output for every pointer update.
   Per-buffer accumulated damage preserves old contents while repainting only
   changed selection fills, edges and labels. Rectangular Cairo clips preserve
   exact antialias coverage; a combined complex clip changed stroke pixels.
3. Hyprland invalidated the complete layer on every content commit. Full
   geometry damage is now reserved for geometry/layer-state changes.
4. The OpenGL texture path replaced damage with the surface clip. It must
   intersect them. A full-output layer clip otherwise defeats precise damage.
5. Damage-ring coalescing turned sparse edges/cursor trails into a huge bounding
   rectangle. Dense damage still coalesces; sparse damage remains precise with
   a fixed complexity cap.

6. Linear filtering reads neighboring texels. Expand each surface damage
   rectangle by one source pixel before transforming/scaling. A forced full
   repaint proved the earlier one-level edge discrepancy was stale compositor
   content: full redraws matched exactly. The corrected sparse repaint also
   matches exactly, so small raw uploads are now retained.
7. Whole-second BO-cache expiry freed cohorts under submission locks. Use
   nanosecond ages and at most one stale free per insertion, retaining the
   256 MiB budget and explicit pressure/teardown trimming.
8. Narrow RGBA8/BGRA8 SHM uploads used a render pass each. The M4 Vulkan path
   now uses its existing raw compute copy for small uncompressed 2D transfers.
   Bulk transfers retain the graphics path. Twelve raw-copy cases, including
   sRGB, and full-resolution GLES partial-upload/compositing comparisons pass.
9. Ioto draw preparation repeatedly searched the complete command grammar.
   Kind/opcode indexing preserves exact validation and reduces CPU work.
   The independent grammar checks every first word and truncation length;
   18,226,780 checks pass with sanitizers, plus both program-core suites.
10. Aquamarine treated separate KMS controllers sharing the same render GPU as
    different GPUs. It copied the complete external image each frame. Compare
    render-device identity and allocate each controller's scanout directly from
    the shared GPU. Framebuffer IDs and failed-import caches are scoped to their
    importing DRM backend. Distinct or unknown GPUs retain the existing path.
11. Fully hidden layer content commits still woke an idle output. A conservative
    Hyprland fast path skips content damage only behind a stable, fully opaque
    higher layer covering the output. Translucency, animation, visible-region
    clipping, blur, partial coverage and geometry/state changes retain damage.
    The committed buffer remains current and is shown when the cover closes.
12. The desktop now uses a separate release Mesa build. The assertion-enabled
    CTS/debug build remains available and unchanged in configuration.

Temporary GameMode, performance-governor and realtime-priority experiments were
reverted after removing the redundant external copy. The qualified session uses
SCHED_OTHER and schedutil. No special priority permissions or clock override are
required for the picker.

The kernel polling-window experiment did not improve FPS either before or
following the compositor fixes. Both trials were reverted. The original
canonical kernel/module remains installed; no screenshot kernel patch is kept.

## Qualification

The installed Omarchy wrapper is exercised through real Wayland pointer/button
input and must save a valid PNG. Default motion is circular at approximately
120 input events per second with a 64-pixel radius: every rounded input delta
is nonzero. A 0.5-second moving warmup precedes the 20-second steady-state
measurement. Earlier triangle reversals and a smaller circle contained zero
deltas, allowing legitimate VFR idle frames. `--motion triangle` preserves the earlier motion for comparison.
A normal 60 Hz output may report slightly above/below 60 FPS over a finite
counter window; acceptance is based on completion intervals as well as counts.

Final qualification results are recorded in `SCREENSHOT_HANDOFF.md` and the
tracked Mesa bundle. They exercise the installed Omarchy wrapper on each output
with both outputs active, normal governors and normal scheduling. Pixel checks
compare growing/shrinking selections with packaged slurp and forced complete
repaints. Raw-copy and GLES regressions cover the installed Mesa build.

Final installed-session reports and build/source hashes are retained with the
Mesa qualification bundle and `SCREENSHOT_HANDOFF.md`. Root experiment archives
also retain rejected candidates and diagnostic captures; do not count every
successful guarded command as a 60 FPS result.

`--repaint-check` on the pixel probe captures a second scanout after forcing a
full repaint without changing the scene. `screenshot-pixel-compare.py` checks
both comparisons automatically. `screenshot-external-cadence.py` samples the
external driver's completion counter every millisecond around the performance
probe. Its timestamps have sampling uncertainty; the internal panel's native
history contains precise kernel completion timestamps.

The GLES regression source is `src/asahi/vulkan/tests/m4_gl_partial_upload.c`.
Link with EGL/GLESv2 and run with the standard M4 Zink environment, under the
mandatory guard: `m4-gl-partial-upload 2560 1664 12`. It checks source bytes and
scissored alpha blending over an interpolated gradient against complete uploads
and redraws. The debug/CTS build stays separate from the release desktop build.

Use `screenshot-performance-summary.py REMOTE --output panel` (or `external`)
to check completion intervals, contiguous counters, coverage and the saved PNG.
The wrapper phase preserves the session's automatic cursor policy; component
isolation phases can temporarily force hardware cursors and restore afterward.

`screenshot-occlusion-probe.py` uses `wayland-layer-probe.c` to change a small
layer from red to blue behind the freeze, check zero hidden panel completions,
then check the latest blue pixels are exposed and visible updates still repaint.
Generate the layer-shell client header/private code plus xdg-shell private code,
link libwayland-client, and install the helper as
`/usr/local/bin/m4-layer-occlusion-probe`. Run the Python probe under the guard.

For a cadence regression with concurrent hidden updates, add
`--hidden-layer-updates` to `screenshot-external-cadence.py`. It changes the
controlled panel layer once per second while dragging the external selection;
all helper processes are bounded and terminated afterward. Compare panel
completion counts as well as external cadence to verify hidden work is skipped.
