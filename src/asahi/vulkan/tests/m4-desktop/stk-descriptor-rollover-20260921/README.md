# SuperTuxKart descriptor rollover regression, 2026-09-21

Native ARM64 SuperTuxKart 1.5 displayed a grey background with HUD but no
race scene on J713 M4. Geometry was present in the G-buffer. Deferred lighting
read incorrect textures and the final fullscreen copy wrote transparent black.

The application legally writes three separate combined-image-sampler bindings
with one VkWriteDescriptorSet (binding 0, array element 0, count 3). Gen9
sampled descriptors are 76 bytes; binding alignment is 8 bytes. The old writer
used offsets 0, 76, 152 instead of 0, 80, 160. In the final copy, textureSize
reported invalid source dimensions and texelFetch read zero. Disabling shader
caches, compression, prefetch optimizations and descriptor heap reuse did not
repair the scene. A trim with reconstructed descriptor state did not preserve
the bug, so use the startup capture, not the frames-300-302 trim.

The fix resolves array rollover using each binding's array size, skips empty
bindings, and uses its actual byte offset, immutable sampler and dynamic-buffer
index. The shared writers cover ordinary, push and template updates. Inline
uniform writes are split at binding boundaries.

## Reproduction and validation

Workspace trace:
`_provenance/experiments/20260921T050340561262Z-stk-vulkan-startup-capture/remote/race_frames_1_through_220.gfxr`
SHA256: `243140451289e5832b3d1cc639396208b901172287744fc0ce3b0854fc56fdb7`.

Replay startup through frame 30 with the normal shaders, production HK flags,
an offscreen swapchain, memory rebinding and omitted pipeline-cache data.
Use tools/vulkan-trace.py under tools/gpu-experiment.py with --renderer asahi,
--frames 30 --end-frame 30 --asahi-debug ''. The pre-fix production library
SHA256 is 0cf7bc13faae54d4fbb72c5ef6a5fab386b158693723dba3c2c10178b73c6c17.
It displays grey. With the fix, the track, kart, lighting and sky return.
Against host lavapipe frame 30, mean absolute channel error is 0.548/255;
16248 of 2073600 pixels differ by more than 8 in any channel. This allows
normal rasterization/precision differences; it is not a pixel-exact claim.
Lavapipe additionally uses --remove-unsupported and one queue; the original
application test below is required in addition to this adapted replay.

The live Vulkan benchmark completed 891 race frames in 46.925 seconds,
18.9877 FPS, with correct race screenshots at 30 and 65 seconds after launch.
Minimum available RAM was 5727.6 MiB, peak temperature 87.5 C. The wrapper
reports missing_success_evidence because its expected success string was
mistyped; the child returned zero, emitted PASS: race benchmark complete,
and saved the profiler CSV and summary. This is a wrapper label error, not a
failed or interrupted game run. Raw logs and screenshots remain in the named
workspace evidence directory.

The fullscreen output is 1920x1080 DP-1 with render scale 2, so the scene is
rendered at 3840x2160 and downsampled. Neither attached display has a 4K mode.
Menus/loading are excluded from the game's race profiler. Geometry 2,
anisotropic 4, dynamic lighting on, shadows/SSAO/MLAA off, VSync off.
Kernel db8763db51c8 bounds retired command-owner caches; it is also required
to avoid the prior excessive retained RAM during loading.

Release ARM64 Vulkan library builds successfully. The existing host native
texture descriptor geometry/policy test passes. All hardware runs use the
60-second watchdog, bounded guard, 0.5-second temperature logging and 95 C
stop. Normal SDDM autologin and both displays are restored after live runs.

## Zink validation and installed build

`20260921T053610009230Z-stk-zink-rollover-fixed-race` completed 766 race frames
in 69.375 s = **11.0414 FPS**. The 65-second screenshot shows the full race
(track, kart, sky, effects); the 30-second image is still loading. Minimum
available RAM 4774.4 MiB, peak temperature 87.25 C. No GPU fault or OOM was
reported by either completed live run. These two samples measure different
application rendering backends and are not a controlled API-overhead comparison.

The exact tested Vulkan library was atomically installed in the normal
`/opt/asahi-m4-port/lib/libvulkan_asahi.so` location. SHA256:
`2a960c5ea294e4709de6502abe3c5ada7b73071211e6f45d23e17c133cd36ad1`.
The previous library is retained beside it with `.before-<sha256>` suffix.
The binary was built from the code in commit 012214ce783 before that commit
was created; its embedded version string therefore predates the commit.

Production desktop restart 20260921T053954175065Z-stk-fixed-production-desktop passed: normal autologin Hyprland maps the installed fix, eDP-1 and DP-1 are active with DPMS on, and no benchmark is left running.
