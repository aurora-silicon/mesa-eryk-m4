# M3 immutable PPP state reuse (2026-10-02)

The native draw path uploads and emits eight PPP state payloads even when their
contents match the preceding draw. The opt-in G15S path retains one immutable
command-pool allocation per control stream and compares every bind, viewport
and varying byte. A mismatch uploads fresh storage; existing storage is never
modified. Command-buffer reset destroys the cache along with its stream.
Graphics invalidation and standalone viewport updates invalidate the match.

`HK_M3_BIND_STATE_REUSE=true` enables allocation reuse only.
`HK_M3_BIND_STATE_ELIDE=true` additionally omits identical PPP updates when reuse
matched. Both default off; G16/M4 and other chips keep their existing behavior.
The final header for the two-word per-draw scissor/depth selector is retained.
Normal, indirect, robust-index and empty-geometry paths still close that header.
Depth/scissor tables, draw commands, shaders, barriers and submission completion
are unchanged. `HK_ARCHITECTURE_PROFILE` reports per-stream uploads, reuses and
elisions; these diagnostics are excluded from timed measurements.

## Qualification

Host draw-state tests cover every payload byte, differing viewport counts,
truncated storage, f32/f16 varying counts, null initial state and immutability.
ARM64 Vulkan builds successfully. M3 runs use a 60-second watchdog, bounded
45-second guards and temperature telemetry. Kernel module SHA256:
`f542a8e7716c0f692b0b0ca9853bdfbc481b0200b775175b65e3efce8a2d5df0`.
Candidate Vulkan SHA256:
`fc2c3c95dd1c78572bbfe84d13fec07d36edf9c2c3da788b4cc32db0697c89e6`.

The unchanged 30-frame Minecraft Vulkan capture at 3024x1837 retains all three
reference images exactly. Five same-boot alternating pairs with mixed batching,
CPU overlap and the qualified experimental depth/color compression give:

| Policy | Median completed replay FPS |
| --- | ---: |
| Allocation reuse, complete PPP commands | 158.278 |
| Allocation reuse and identical-command elision | 162.175 |

This is +2.462%, not live Java FPS. The earlier allocation-only control was
158.871 versus 159.049 FPS (+0.112%, no reliable gain). The separate counter
run proves each 1,370-draw world pass makes 11 uploads and elides 1,359 duplicate
bind tails, removing 86,976 command bytes per pass while retaining the selectors.
The earlier elision candidate independently measured +2.620%.

506 pinned CTS cases from catalog SHA256
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`
produce 324 Pass, 182 NotSupported and no failures. Coverage includes indirect
multi-draw, dynamic state, secondary buffers, viewport/scissor, robust indexing,
occlusion and all 23 geometry-emission cases. The latter include empty draws.
Two indirect-draw cases actually hit the elision path; other selected cases
exercise state changes and fallbacks. This is focused coverage, not full CTS.

A bounded real Vulkan Minecraft demo run displays maximized 3024x1837 gameplay
with F3, loads the candidate DSO and records 575,456 elisions. Guard and policy
restoration pass. Accelerated Plasma autologin remains enabled and active on
its prior Mesa; the optimization is not made a desktop default.

Detailed images, per-case catalog indices, policies, temperatures, library
hashes, paired samples and commands are local benchmark evidence under
`diagnostics/m3-macos-driver-gap-20261002/bind-state-elide-v2`. No captures or
vendor binaries are included in the Mesa commit. M4 was not hardware-tested.
