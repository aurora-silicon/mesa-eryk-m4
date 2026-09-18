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
Original application validation remains outstanding at this checkpoint.
