# Qualified G15S Zink render-pass lookahead

The M3 Minecraft OpenGL replay improves from 76.4146 to 86.6446 median FPS
over nine alternating guarded pairs. All pairs improve; the median paired
change is +8.9136% (bootstrap 95% interval +1.2115 to +15.2701%). Three sampled
frames are pixel-identical. The separate native Vulkan recording runs at about
105 FPS; those are different captures, so the gap is not translation overhead.

Full TC lookahead previously corrupted Godot TPS. Disabling merged depth/stencil
resolves restores the exact reference while preserving color and attachment
usage lookahead. `threaded_context_options.disable_renderpass_depth_resolve`
keeps those operations as ordinary `pipe->blit` calls. Zink selects that
fallback for Honeykrisp. No Vulkan capability, draw, format, sample count or
application setting is removed. The underlying inline depth-resolve corruption
is isolated but not claimed fully explained or repaired.

Zink enables lookahead for the qualified G15S device ID 0x6030. The G16G default
remains unchanged; no M4 hardware experiment was run. `ZINK_DEBUG=rp` and
`ZINK_DEBUG=norp` retain bounded diagnostic use.

A generic TC attachment-use bug is fixed independently: a pending depth resolve
must retain its source attachment even if no draw uses depth. The retained
`src/asahi/vulkan/tests/m4-desktop/resolve-lookahead-probe.c` tests that behavior
and consecutive color destinations. Under old full lookahead, all 2048 depth
pixels incorrectly stay at the destination clear value when depth testing is
disabled. The fixed path returns the expected 0.25; depth-enabled and all color
checks also pass. It exits normally for pixel failures so the host can retain
an expected broken-driver observation after the GPU guard has safely drained.

Qualification: 174 frozen GLES framebuffer/depth/blit/invalidation cases,
162 Pass and 12 NotSupported, no failures; exact Godot and Minecraft checks;
retained Factorio, SuperTuxKart, 0 A.D. Zink, SuperTux GL2/GL3 checks; real
Minecraft demo world rendering with F3 at 3024×1837 and candidate library maps.
The demo information overlay remains; this is not a live-FPS benchmark.
Firefox Seascape is exact to the current normal build, but both differ from
the older pinned bank. That historical check remains failed and unscored.

The completed profile observes graphics retirements 140→110, commands 374→308,
fragment spans 136.51→123.05 ms, and process CPU time 72.86→73.71 ms. Global
retirement counters include desktop work and overlapping engines; do not add
them into utilization or substitute them for completed replay timings.

Full evidence: `/home/council/dev/dirtyroom_m4/build/m3-zink-overhead-20261001/`.
Compact receipts/report:
`/home/council/dev/gpu-replay-bench/diagnostics/m3-zink-lookahead-20261001/`.
82 guarded experiments include two failed live-launch attempts, retained as
failures: an internal startup bound and a screenshot-helper error. The latter
used the long progress guard and triggered watchdog recovery. Final normal and
candidate launch checks both complete under ordinary 45-second guards. Peak
exposed package channel 84.75°C; all guards arm 60 seconds and monitor all
channels every 0.5 seconds. No GPU fault was reported. Never execute the C probe
or any real app unguarded for driver qualification.

Cross-built Gallium candidate:
`/opt/asahi-m3-zink-pass-lookahead-c0059a9b2f0a`;
SHA256 `c0059a9b2f0a2ffb1cb5b709c9ae9030ae75f31f491c9c7a9a895dc3b85cf44f`.
The existing Vulkan library remains
`82098edb14e583ae7c562e14e004a4335ace70ff4b23bcf6cb535ae2ed4153c7`.
The running desktop prefix remains `/opt/asahi-m3m4-f568f1256f8`.

Host helpers:
`tools/m3-replay-paired.py`, `tools/m3-multidraw-resource-probe.py`
(`--diagnostic-env ZINK_DEBUG=rp` and an explicit PASS marker), and
`tools/m3-minecraft-live-smoke.py` in the workspace root. The last stages a
copied save and verifies mappings without changing the desktop's Mesa selection.
Benchmark `tools/minecraft/gl-interval-profile-summary.py` keeps profiling
output explicitly diagnostic; score only approved, non-profiled 30-frame runs.
