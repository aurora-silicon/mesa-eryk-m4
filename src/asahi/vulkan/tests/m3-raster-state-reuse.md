## Immutable raster-table reuse

`HK_M3_RASTER_STATE_REUSE=true` opts G15S into reusing the final complete
scissor/depth-bias records when all encoded bytes match. Scissors are compared
as the complete current viewport array; depth bias uses all 12 native bytes.
Previously recorded entries are never overwritten or moved. Existing per-draw
selectors still close every normal, robust and geometry-empty draw. They select
the retained table tail with the original viewport-count adjustment. The arrays
belong to one control stream and reset with it, so no cross-CB/device cache or
new lifetime/invalidation rule is introduced. M4 and desktop defaults are unchanged.

The host test resolves real selector indices across one/two-viewport tail reuse,
rejects undersized and misaligned tables, changes every depth-bias byte and
checks earlier records remain intact. Both unchanged Minecraft image checks are
pixel-exact. All 506 frozen-index focused Vulkan cases finish: 324 Pass,
182 NotSupported, zero failures/crashes/device losses. The selection retains
its original catalog hash and indices, including all 23 geometry emit cases.

A guarded 23.10-second real native Vulkan Minecraft run shows maximized
3024x1837 unpaused F3 forest gameplay on the private candidate. Diagnostics
confirm 663,287 scissor and 663,018 depth-bias reuses during that application run.
Those are reuse events, not frames or saved GPU time. The run used a copied demo world and stopped its owned game process without
modifying the user's world/options.

Fifteen same-boot alternating 30-frame replay timings give 162.04 / 162.37 FPS
by separate medians, paired mean +0.34% and paired median +0.12%. This is not a
reliable throughput gain. The change is retained as an opt-in reduction in
CPU-recorded and GPU-uploaded table data, with exactness qualification; it is
not presented as narrowing the macOS FPS gap.

Evidence: `/home/council/dev/gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261002/raster-state-reuse/`.
Vulkan SHA256: `51e170c383d7cb8000d7b44312fec42da0b799f68fe14ace380055a278f34bec`.
Kernel SHA256: `f542a8e7716c0f692b0b0ca9853bdfbc481b0200b775175b65e3efce8a2d5df0`.
Every experiment retained the mandatory 60-second watchdog, 0.5-second thermal
telemetry and completion-policy restoration receipts. This is focused testing.

A final unscored counter diagnostic, selected by the actual completed frame
interval, finds 1,369 scissor and 1,369 depth-bias reuses in each of the 30
1370-draw main passes. Their tables shrink from 38,360 bytes to 28 bytes per
pass, saving 38,332 bytes/frame. Across other passes the counters establish
at least 40,776.53 bytes/frame saved (viewport arrays can save more). This is
metadata storage/upload reduction, not a claimed frame-time saving. The reusable
`tools/minecraft/raster-table-cost.py` rejects an incomplete interval or guard
and excludes the initial warmup pass; evidence is in `raster-state-reuse/table-cost`.

Normal accelerated Plasma restoration passes the guarded probe with a fresh
Konsole window, mapped normal Vulkan/Gallium libraries and a completed desktop
screenshot. The 60-second watchdog is disarmed cleanly; `plasmalogin.service`
is enabled and active. Evidence is in `raster-state-reuse/desktop`.
