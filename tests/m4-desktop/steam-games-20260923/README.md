# Steam graphics selection follow-up (2026-09-23)

Steam's FEX bridge still bound /opt/asahi-m4-port while the desktop and matched
benchmark replays used versioned merged Mesa. Lethal Company initially created a
D3D11 device then displayed a black window. GE-Proton11-7-aarch64 with merged
Mesa a27cbcbe7bf reached its brightness setup screen; renderer/input validation
is ongoing. No claim of completed menu/gameplay qualification yet.

Target bridge mount drop-in:

    /etc/systemd/system/home-e-.local-share-m4\x2dfex-mesa.mount.d/50-shared-mesa.conf
    [Mount]
    What=/opt/asahi-m3m4-a27cbcbe7bf

Selecting the bind source requires stopping processes using the previous mount;
live tests temporarily have an overmount. Normal boot uses the configured source.
The bridge's stable ICD/library paths remain visible within pressure-vessel.
`steam-game-bridge-state.py` records their hashes without account information.

`steam-desktop-command.py` now selects Hyprland's socket by the live process's
lock file, excluding stale sockets left by previous compositor crashes. It was
validated on a desktop with stale PID 776 and live PID 7946 sockets.

`steam-game-input.py` injects input through Hyprland's supported dispatcher API,
requires an active 60-second experiment watchdog lease, and avoids guest XTEST
requests which may stall at the microVM's X11 proxy. Input qualification pending.

The normal Steam Proton Experimental path with the merged bridge reached the
first-run brightness screen and LAN/online selector. The apparent interstitial
wait included a roughly 30-second install-script evaluation. This remains
startup validation, not menu/gameplay capture qualification.

Input fix: omit the explicit window target, which Hyprland uses to reset pointer
focus coordinates to (1,1), and emit separate down/up events. Guest XTEST is
avoided; host compositor input advanced past first-run setup.


Actual X11 pointer qualification: Hyprland cursor warps moved the compositor
cursor but XQueryPointer still reported coordinates outside the game window.
`bench-pointer.c` sends wlr-virtual-pointer absolute motion and button events;
this updated XQueryPointer coordinates and selected LAN/main-menu controls.
Build against the wlr-virtual-pointer protocol generated with wayland-scanner
from Hyprland's protocols/wlr-virtual-pointer-unstable-v1.xml. Coordinates are
compositor logical pixels with the combined desktop extent, not X11 root pixels.

A clean reboot verified the merged Mesa mount without an overmount. Lethal
Company v81 now reaches the actual LAN main menu. Its 31-frame 3840x2160 DX11
capture replayed on M4 with offscreen swapchain and memory-translation none;
readbacks at frames 1,16,31 were produced, and frame16 shows the correct menu.
Experiment: 20260923T220705178599Z-lethal-menu-replay-qualification; peak78C.
This was readback qualification, not scored timing or cross-device validation.
Proprietary trace/assets remain in the local-only gpu-replay-bench repository.

Live gameplay trimming with Steam running exhausted host memory and killed the
muVM. The retained kernel OOM record identifies PID3185, not a GPU fault. A full
stream capture also approached exhaustion, so it was stopped. Standalone LAN
launch without Steam CEF is being evaluated with a 384MiB/10s memory stop gate.
Gameplay is not yet qualified. No new game/capture tests have run on M3.


X11 present-wait regression: the full Lethal recording returned DEVICE_LOST for
many vkWaitForPresentKHR calls while rendering continued. x11_wait_for_present
compared u_cnd_monotonic_timedwait's C11 result against POSIX ETIMEDOUT. The
helper actually returns thrd_timedout, causing the generic error branch to run.
Use thrd_timedout and thrd_success, matching the display/Wayland paths.

Host regression probe extracts the actual function from the selected source
revision and links Mesa's real monotonic condition-variable implementation;
only swapchain storage and the GPU semaphore prerequisite are substituted.
`test-x11-present-timeout.py SOURCE HOST_BUILD` passes completed presentation,
condition timeout, semaphore timeout, semaphore error and presentation error.
`--baseline-ref a7d898f39ba` reproduces DEVICE_LOST instead of TIMEOUT for the
condition timeout, while the other four cases pass. Incremental ARM64 Vulkan
build passes. Hardware integration validation remains pending. This fix is not
yet claimed to repair the black gameplay scene.
