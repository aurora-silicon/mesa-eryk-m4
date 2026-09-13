# GPU application qualification — 2026-09-13

These are bounded startup/rendering tests on J713/M4, not full conformance,
long-term stability, performance targets or every application feature. Evidence
IDs refer to workspace _provenance/experiments/. Earlier renderer restrictions
and replay adaptations remain recorded in five-apps-20260912/STATUS.md.

| Application | Qualified path and evidence |
| --- | --- |
| SuperTuxKart | Zink/M4 race, correct kart/terrain/HUD,15.050 seconds on race clock;20260912T203436842865Z. Original5115-frame capture also replayed. |
| Firefox | Normal profile, hardware WebRender and WebGL1/2 through Zink/M4. MDN textured cube and page snapshot correct;20260913T020103735416Z. Exact static-color readback passes020017. |
| Chromium | ANGLE GL through Zink/M4, textured WebGL cube, context not lost and GL error0;20260912T203330576693Z. |
| Plasma6 | Standalone KWin/Plasmashell desktop, wallpaper/panel/icons, Zink/M4;20260912T120807742380Z. |
| vkQuake | Native Vulkan demo world/HUD;20260912T120250. Original83-frame replay also completed. |
| GNOME | Native desktop/top bar with normal global Zink configuration;20260912T212138983849Z. |
| Blender5.2.1 | Vulkan Workbench fullscreen UI, cube/grid/axes and timed rotation;20260913T015130930679Z. Lite build excludes Cycles; Eevee unqualified. |

Blender's17-frame replay now passes native synchronization validation with no
VUIDs or GPU timeout (20260913T015038083051Z,57.25 C). GFXReconstruct previously
ignored recorded timeline-counter observations and destroyed pending resources.
The replayer now waits for captured progress before dependent host calls.
Its final native image has the grid/axes; lavapipe omits these overlays, so the
comparison is not pixel-identical. New replayer SHA256:
fe93683a857ae08af25631dd652c6986243c17055744101b1e19d3dc15e44d98.

Remaining Firefox diagnostic: enabling Vulkan capture swaps the WebGL snapshot's
red/blue channels, while normal rendering/snapshot and adapted Vulkan replay
are correct. Reproduced with read=0&animate=1 in webgl-readback.html;307200 bad
pixels in015254. The same fixture without capture passes020017 and lavapipe
passes015229. The shader-cache-disabled result was confounded by capture being
off; it is not justification for a cache workaround. Investigate capture's
injected external-image initialization and shared-image state; no root cause
or production workaround claimed. Ordinary Firefox acceleration stays enabled.

SDDM graphical boot and normal keyboard login/Foot passed0152-login-timeline-fixed.
Hyprland remains running, bounded test processes exited, SSH watchdog is idle
and renewing its60-second device every10 seconds. Latest tests peaked58.75 C;
all Linux tests retained the six-channel thermal guard and watchdog lease.
The existing hourly DCP callbackD114 failure remains separately tracked.
