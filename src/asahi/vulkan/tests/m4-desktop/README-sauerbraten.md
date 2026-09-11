# Guarded Sauerbraten map test

`M4_TEST_SAUERBRATEN=1` selects the private ARM64 client at
`/opt/m4-sauerbraten/sauer_client`, data in `game/`, dependencies in `lib/`.
Build the unmodified official 2020 release; the workspace keeps its source and
build provenance under `build/sauerbraten`. Launch through `desktop-guard.py`
only (workspace `tools/desktop-experiment.py --game sauerbraten`).

The test requests a 1024x768 Wayland window and metl4. Graphics settings remain
those selected by the engine; SDL audio uses its dummy device. Config and logs
are isolated in `/home/e/.config/m4-asahi-sauerbraten`. The launcher validates
native Hyprland, real Vulkan progress and imported DCP scanout, then requires a
surviving owned game window, map-ready marker and M4 Zink renderer. Screenshots
require separate visual inspection; survival is not a rendering oracle.

The existing 60-second watchdog and six-channel thermal guard remain mandatory.
Thermal/failure shutdown now also stops the Sauerbraten process. Evidence is
saved before recovery/disarm. Kernel/Mesa identities and binary SHA are recorded.

First hardware run `20260911T140909402052Z-sauerbraten-metl4-first-map` failed
cleanly at map upload: VK_FORMAT_A1R5G5B5_UNORM_PACK16 was rejected by the native
buffer-to-image admission path. Renderer was M4 Zink, 53 temperature samples,
peak51.5C, no kernel GPU fault; guard initiated reboot and evidence was fetched.
This is not a completed map qualification. No game feature was disabled to
avoid the missing format.

`M4_SAUERBRATEN_TRACE=1` records original EGL calls in a unique trace under
`.local/state/m4-asahi-traces` for later reference replay. The ordinary game
script reports its built-in FPS every five seconds after map loading. These
reports never renew the watchdog; only the existing GPU/thermal checks do.
On failure the guard prints the selected game's log tail before recovery, so
missing features can be identified while the reboot is still finishing.
