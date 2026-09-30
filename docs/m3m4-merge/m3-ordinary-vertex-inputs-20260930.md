# Ordinary M3 vertex inputs

Implementation commit `3966e1fcc80` extends the existing ordinary descriptor
variant to hardware vertex shaders on G15S. Immutable descriptor metadata must
prove ordinary sampler/format behavior; scaled formats, special LOD handling,
sparse/shadow UBOs and mutable descriptor bindings retain the general variant.
No advertised capability, shader operation or application setting is removed.
UBO contents continue to be read on the GPU at execution.

Command recording checks each UBO descriptor once rather than once per word.
The GPU loader omits eligibility words whose reads were folded out of the
ordinary executable. Bounds checks for actual scalar data remain unchanged.
`HK_M3_VERTEX_PLAIN_SAMPLERS=false` disables the new vertex specialization.
The compiler/pipeline cache policy contains that override.

The frequent Minecraft world vertex program decreases from 489 instructions /
80 allocated register slots to 326 instructions / 48 slots. This is not itself
a frame-rate claim. Separate instrumented stage observations decrease the world
geometry span from 2.94775 ms to 2.819583 ms; the fragment span remains 2.66 ms.
The profiling boot was restored to normal before timing.

## Completed replay measurements

At 3024x1837, identical 30 captured gameplay frames with F3 visible, the full
package was compared against f7b0d4dce0a on one normal boot. Six timing samples
per build alternate order; both configurations first passed exact comparisons
of frames 1, 16 and 31 against the original reference.

| Build | Median replay FPS |
|---|---:|
| f7b0d4dce0a control | 95.9669 |
| 3966e1fcc80 default | 97.2337 |

The measured gain is 1.32%. The new score is 3.93% above the **previously
recorded** M1 diagnostic 93.5593 FPS, with that run's existing thermal-sensor
qualification limitation. M1 and M4 were not rerun. These are completed-frame
GPU replay timings, not live Minecraft/Java simulation FPS. The earlier M3
97.2551 result was on a different boot; use the matched control for the new gain.
All samples, including slower samples, are retained in benchmark
`diagnostics/m3-ordinary-vertex-final-paired/{configuration,runs,summary}.json`.

Full package: `/opt/asahi-m3m4-3966e1fcc80`.
Vulkan SHA256: `f48d0001a54ff1d27d50115e1878670755e71f1023f7fed71003be8657457f92`.
Code-equivalent precommit candidate:
`04e4a0be1ec3af176c6927a7dda070e0f770509163b3fa0cc5c8c4130dd9b2a0`.
The package's new Git build ID is included in its shader-cache identity.

## Correctness

The precommit default candidate passed 1196 pinned-index CTS cases:
789 Pass / 407 NotSupported, no failures or guard errors. Host descriptor tests
exercise both vertex and fragment stages. Ioto composition tests and the full
ARM64 build pass. See `tests/vertex-samplers-20260930` for the retained selection.
This is targeted regression coverage, not a full conformance claim.

Saved game image checks pass for Minecraft Vulkan and OpenGL/Zink, Satisfactory
Vulkan gameplay, Factorio Wayland, 0 A.D. Vulkan and SuperTuxKart Zink. Minecraft
images match exactly; the other games retain their existing one-RGB-level policy.
No failed tolerance was relaxed. Evidence: benchmark
`diagnostics/m3-ordinary-vertex-games.log` and workspace
`build/minecraft-inputs-next/game-summary.json`.

Vertex preambles and rearranging uniform inputs were also investigated. Their
checkpoints are on `experiment/m3-vertex-preamble-20260930` and
`experiment/m3-input-coalescing-20260930`; neither is part of the default change.
An early-tiling replay completed with matching images but did not establish an
additional gain; its default remains unchanged. All hardware experiments used
the existing watchdog, thermal and completion-progress guards.

## Complete package and desktop validation

The complete package also passes exact Minecraft OpenGL/Zink image checks.
Three warm replay samples are 26.1697, 24.6645, 25.3645 FPS
(median 25.3645). This separate capture has no matched GL control
for a causal speedup claim. Evidence: benchmark
`diagnostics/m3-ordinary-vertex-gl.log`.

The persistent and live M3 graphics environment now select the complete package.
The 45-second watchdog/thermal-guarded session handover took 4.07 seconds:
KWin maps its Vulkan library and Plasma is running. No recent kernel errors
were reported after handover. Receipt:
`build/minecraft-inputs-next/deployment/receipt/success.json`.
Autologin is preserved; no profiling marker or benchmark workload remains.
