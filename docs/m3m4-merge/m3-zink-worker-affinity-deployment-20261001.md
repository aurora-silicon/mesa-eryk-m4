# M3 worker policy: complete package and desktop deployment

The complete ARM64 Mesa package at revision `f568f1256f8` is installed at
`/opt/asahi-m3m4-f568f1256f8` and selected for M3's normal Plasma session.
The host generators were rebuilt before the cross build. All seven package
libraries were verified on the target before selection. The prior desktop
configuration is retained in `/root/mesa-worker-affinity-selection-20261001`;
the game-specific `/opt/asahi-m3-0ad` environment remains separate.

The full package passes six existing game image checks: Minecraft OpenGL/Zink
and native Vulkan, Satisfactory Vulkan gameplay, Factorio Wayland, 0ad Vulkan,
and SuperTuxKart Zink. Reference images and their existing tolerances are
unchanged. The 48-case multi-draw resource oracle also passes with the worker
option absent, checking both worker masks and the unchanged caller mask.
A real Minecraft demo enters its copied world on native Wayland in 7.61 seconds
and loads the full-package Gallium library. This remains a bounded rendering
witness behind the demo information dialog, not a live FPS measurement.

Plasma was restarted through its normal display manager and autologin. The
probe verifies the active Wayland session/VT, Zink on Apple M3, and the exact
Vulkan/Gallium/EGL/GBM/backend libraries mapped into KWin. Its first screenshot
was taken during the Plasma splash; a second guarded probe confirms the usable
desktop, taskbar and an ordinary Konsole window after startup completed. Both
screenshots and post-capture KWin queries completed, with no new GPU fault or
reboot. All GPU work used the existing watchdog and thermal guard.

The companion JSON retains the package and selection records, six image-check
receipts, oracle output, real-application and desktop guard receipts. This is
M3 hardware validation. It does not qualify deployment on M1 or M4 or provide
fresh benchmark scores; the paired performance evidence remains in
`m3-zink-worker-affinity-20261001.json`.
