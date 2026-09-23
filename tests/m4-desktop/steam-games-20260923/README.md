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
