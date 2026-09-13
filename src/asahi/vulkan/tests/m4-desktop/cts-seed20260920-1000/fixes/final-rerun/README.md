# Seed 20260920 after fixes

All 1,000 frozen cases ran once in this final campaign: **871 Pass, 129
NotSupported, zero Fail, Crash, ResourceError, Timeout or HarnessError**.
All 854 original passes remain passes. All 15 original failures and the one
userspace crash pass; Wayland basic rendering passes, and the other former
Wayland setup error reaches the optional VK_GOOGLE_display_timing check and
returns NotSupported. The original 128 unsupported cases remain unsupported.
See comparison.json and ../resolutions.tsv for the exact transition list.

20 concurrent workers; 30-second per-case bound; 50-second outer experiment
bounds; 60-second watchdog; six thermal channels sampled every 0.5 seconds.
Elapsed 80.947 s including guards/transport, batch execution 68.882 s, peak 62 C.
Wayland renders its original 600 frames in 23.345 s in this parallel run.
Normal shader caching is retained. No case replacement or retry occurs in this
campaign; earlier unsuccessful campaigns are recorded separately.

The separately identified CTS executable is deqp-vk-seed20-pump, SHA256
3b8763f1f60ab1faa581ee9c61985e382cc6ca2e4011ba38b8fdf07ff2f52771.
It includes the documented historical CTS corrections, xdg-shell backport and
application event pump. Tests are not represented as pristine CTS conformance.
Original logs and pixel/functional checks are preserved except for the
explicitly documented invalid historical test setup corrections.

Mesa fixes 14 original failing cases;
one Fail and the parser Crash required historical CTS input/setup corrections.
No kernel changes were needed for those cases. Native Mesa and kernel binary
hashes, boot identity and every guard result are retained in summary.json.

The existing hourly DCP D114 callback failed the display during the work and
required a recovery reboot before this campaign. That callback remains open;
recovery is not a driver fix. Native and lavapipe controls and failed attempts
are retained in ../dcp-recovery and ../rerun-20s / ../rerun-30s-attempt. CTS now
refuses WSI batches in a latched failed-display state.

The normal Omarchy desktop and Foot were restored and visually verified using
native scanout. Final read-only checks show SDDM active, DCP failed=0, no CTS
processes, and watchdog idle/armed at 60 seconds on boot
3e6a333b-ddc3-4749-999d-606b6d42262f. This is a sampled qualification, not a
full Vulkan conformance or long-duration stability claim.
