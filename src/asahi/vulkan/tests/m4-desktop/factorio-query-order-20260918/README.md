# Factorio query-copy ordering, 2026-09-18

Factorio 2.0.77 from Steam reproducibly timed out in a native query-copy shader.
Its retained Vulkan capture includes EndRendering, a timestamp result copy,
EndQuery for fragment invocation statistics, then a WAIT query-result copy.
EndQuery defers availability to post_gfx, but the existing ordinary compute
stream precedes that write. Appending the waiting copy to that earlier stream
creates a circular dependency. Empty graphics statistics queries exhibit the
same failure without Factorio, FEX, X11, or a render pass.

`vulkan-query-copy-probe.c 2` reduces this to reset, begin/end an empty fragment
statistics query, and copy with WAIT. The expected result is zero and availability
one. Build with `cc -O2 -Wall -Wextra FILE -lvulkan -o PROBE` and run on hardware
only through `tools/gpu-experiment.py`. Modes 0/1 exercise unavailable timestamp
copies without WAIT; the GL probe covers Zink timestamp/occlusion readback.

Evidence under workspace `_provenance/experiments/`:

- `20260918T010646103158Z-query-copy-postgfx-order-baseline`: GPU timeout,
  seven thermal samples, peak 62 C, sealed fault collected before recovery.
- `20260918T010824450508Z-query-copy-postgfx-order-fixed`: PASS, exact zero/one
  result, peak 70.5 C. Fixed native library SHA-256:
  `0988bc27de128f328d4fadddd131fbfe0997ae1409c7d104b673fb4da93badc5`.
- The same reduced test passes on host lavapipe.

The fix finalizes graphics/post-graphics and ordinary compute streams before
creating a native query-copy consumer. It retains WAIT semantics and actual
query results; it does not disable statistics or hide a timeout.

Factorio native failing capture: `20260918T010318782011Z-factorio-inner-vulkan-capture`,
retained as `build/steam-crash-20260918/factorio-startup.gfxr` (13 frames).
Steam pressure-vessel rewrites Vulkan layer paths: the private capture wrapper
must set VK_LAYER_PATH inside the container, immediately before the application.
The simple passing probe qualified that setup in
`20260918T010220127887Z-query-copy-steam-container-capture-home`.
A software replay of the application capture is blocked by device extension and
feature differences; no unsupported capabilities were silently removed.
Fixed native replay passed in
`20260918T011010516942Z-factorio-query-order-native-replay`: all 12 complete frames,
1.15 seconds, peak 67 C. Replay explicitly used offscreen swapchains, memory
rebinding and omitted pipeline caches. Its mapped-image layout warning means
this is execution/dependency evidence, not a pixel-correctness claim.
Capture SHA-256: `04f8335b1a4ae67dc2182de5437ed049c986f3d5afd19da381e7564fda79afe7`.

Steam multilib image r9 contains the fixed x86_64 and i686 Vulkan libraries,
retaining the existing Zink/GLX/EGL stack. Packing verified both architecture
manifests in `20260918T010912626913Z-steam-query-order-pack-r9`; image SHA-256:
`eb54d99f6c672a86e831e93d87daf4802efa71ebe775da99791a6e8738f65f48`.
The normal launch options were restored before original application validation.

The first normal application load passed query initialization but hit host OOM
while allocating full-quality Space Age texture atlases:
`20260918T011401182334Z-factorio-steam-fixed-first-load`. Kernel evidence reports
6,090,032 KiB unevictable memory, VM anon RSS 7,052,144 KiB, zero swap, and an
OOM kill of the complete VM. This is a separate memory-capacity failure, not a
recurrence of the query deadlock. A 4 GiB swapfile was subsequently activated
in its own Btrfs subvolume, with a persistent nofail fstab entry;
`20260918T012028160450Z-factorio-host-swap-reserve` passes activation checks.
The helper retains the prior fstab and does not change graphics settings.

Kernel tools commit `c6dd2e3a1e33` also prevents full-clock guards from rewriting
the current thermal QoS ceiling as a permanent user cap. Nine temporary-sysfs
tests pass, including thermal stop, sensor loss, disabled policy, stale samples,
lower-cap enforcement, and recovery from a temporary thermal ceiling. The
95 C cutoff and mandatory CPU thermal policy remain enforced. Original user
maxima were restored at idle (cooling states zero) before the retry.

## Original Steam application qualification completed

Factorio 2.0.77 Space Age launched through logged-in Steam, initialized at full
existing graphics settings, and entered a new default Freeplay world (seed
1650223530). Mouse navigation reached Single player, New game, map setup and
Play; keyboard Tab dismissed the introduction and Escape opened the pause menu.
The application remains paused for the user. No existing save was selected or
manually overwritten. This qualifies the native Linux x86_64 OpenGL application
through FEX/muvm, Zink and the M4 Vulkan driver; it is not a DXVK/DirectX game test.

The initial load completed in 404.055 seconds, including sprite atlas generation
and mipmaps. Reported final video memory use was 2699.04 MB. No graphics quality
setting was lowered. The four-GiB host swap reserve avoids the previous host OOM;
it is a capacity fix, not a claim that memory use or load time is optimized.

Application evidence under workspace `_provenance/experiments/`:

- `20260918T012258279728Z-factorio-steam-with-swap`: first bounded segment was
  intentionally handed off while the ordinary game was still loading; its
  missing-success verdict is not counted as a pass.
- `20260918T012806972925Z-factorio-steam-sprite-load-continuation`: attached to
  that same game; initialization PASS, peak 66.5 C, 256 thermal samples.
- `20260918T013809429902Z-factorio-single-player-menu`: mouse navigation.
- `20260918T014311707995Z-factorio-start-freeplay`: generated world and intro,
  peak 70.75 C.
- `20260918T014427595695Z-factorio-freeplay-control`: keyboard Tab enters the
  playable world, peak 67.25 C.
- `20260918T014858932876Z-factorio-final-pause`: Escape opens the pause menu,
  peak 68 C, nine thermal samples. Final scanout is retained locally as
  `build/steam-crash-20260918/factorio-final-paused.png`; its hash is in
  `final-validation.json` along with the selected experiment reports.

Both Wayland outputs remain active: eDP-1 2560x1664 at 60 Hz and the Philips
PHL 237E7 on DP-1 at 1920x1080 at 60 Hz. Persistent autologin is unchanged and
the watchdog broker returned armed to idle with a verified 60-second timeout.
Final root filesystem use is 54 GiB used / 42 GiB available, after game/runtime
installation and swap allocation. Historical driver builds and captures were
archived on the development workstation, not discarded; see the preceding
Steam/X11 qualification directory for restore locations and checksum evidence.

Known follow-ups: first loading remains slow and memory intensive; thin dashed
diagonal seams and flat-looking areas remain in UI panels/buttons. Terrain,
character and wreck render, but this run is not a pixel-perfect graphics claim,
a performance benchmark, or long-duration stability qualification. The source
GUI atlas inspected separately does not contain the diagonal artifacts.

`factorio-ui-step.py` retains the input recipe for this Lua-configured Hyprland.
Run it as target root inside `gpu-experiment.py`; it uses the workspace helpers
`/root/steam-desktop-command.py` and `/home/e/.cache/x11-input.py`. Click coordinates
are physical panel pixels, while keyboard events use Hyprland send_shortcut.
Do not use the obsolete focuswindow dispatcher on this Hyprland build. A success
marker only proves command completion; confirm the expected screen transition.
