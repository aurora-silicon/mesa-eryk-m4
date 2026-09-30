# M3 Zink: recognize equivalent robust uniform-buffer bounds

Minecraft's GL replay enables Vulkan robustness through Zink. Its vertex and
fragment UBO loads reach the input-loader matcher with the predicate
`offset < usub_sat(size, 3)`, sometimes folded to `usub_sat(size, 3) != 0` at
zero offset. The matcher previously accepted only `offset + 3 < size`.
These are equivalent for the admitted nonwrapping 32-bit scalar offsets, but
missing the saturating forms left the full GPU alias/residency path in place.

Recognize the equivalent bounds without changing the descriptor admission
policy, zero-page fallback, scalar range checks, binding mutability rules or
GPU execution-time reads. The bound must still be component 2 of exactly the
same descriptor that supplies the address. Incorrect subtraction constants,
ordinary wrapping subtraction, mismatched offsets and wrong size components
are rejected by the host tests. Indexing, volatile loads and denied layouts
retain their original paths. Uniform-file exhaustion remains a fallback.

The shared change enables the new forms only in fragment shaders. The existing
vertex matcher is retained: the full prototype reduces Minecraft's terrain
vertex program from 2284 instructions / 152 registers to 143 instructions /
59 registers, but changes 71–72 pixels by more than one RGB level in each saved
frame. The vertex difference is unresolved and its performance is not published.
The full prototype is retained on `experiment/m3-zink-saturating-ubo-20260930`
at `ad8ce80a997`; the shared branch does not enable that vertex extension.

## Diagnosis

The installed build already selects ordinary fragment variants, with terrain
programs at 1061–1081 instructions / 112 registers / no scratch. The historical
6406–6424 instruction / 192 register / 56-byte scratch programs are no longer
the current fragment bottleneck. Explicit Zink lazy descriptor selection did
not expose a useful gain and is not installed as a workaround.

A diagnostic native-counter window between first/last saved snapshots gives
about 459 ms tiling, 368 ms fragment and 118 ms compute spans within 1412 ms
wall time. Counters are global, may overlap, include other clients and screenshot
work, and are not precise per-client utilization or the clean benchmark interval.
They directed investigation toward vertex work; they do not explain every part
of the M1/M3 gap. CPU sampling also retained useful data, but perf's missing
`/proc/schedstat` warning makes that image-check receipt unsuccessful. It is
unscored diagnostic evidence, not a GPU fault.

## Evidence and scope

Workspace evidence: `build/m3-zink-bottleneck/`; benchmark evidence:
`gpu-replay-bench/diagnostics/m3-zink-bottleneck-20260930/`.
The isolated fragment-only Vulkan candidate is
`/opt/asahi-m3-zink-fragment-saturating-5da3da4d27cc`, SHA256
`5da3da4d27cc7bba311b79a244b323e955d65815d2c45c71bf3ab775f1cbdab7`.
Other libraries remain the installed `01d77ea1a25` package. The desktop still
uses that complete installed package; no driver reload or reboot is needed for
these isolated guarded replays. M4 is untouched.

Host descriptor tests, bounded sampled-loop tests, Ioto program tests and the
ARM64 Vulkan build pass. The fragment-only candidate's three saved Minecraft
GL frames match the installed build exactly. Paired timing and CTS results are
recorded below. No quality or capability is removed.

## Paired Minecraft OpenGL result

Three alternating warmed samples on the same boot give 26.1927 FPS median for
`01d77ea1a25` versus 31.0171 FPS for the fragment-only candidate: **+18.42%**.
Samples: control 25.9587 / 27.0606 / 26.1927; candidate 29.8152 / 31.2984 / 31.0171.
All first/middle/last images match exactly. Source, API capabilities, 3024x1837
resolution, 30 completed frames and the benchmark replayer are unchanged; only
the isolated Vulkan library differs. This is not live Minecraft FPS or a claim
to have closed the entire M1/M3 gap. The paired helper records each library hash
and every sample in `fs-paired/{runs,images,summary}.json`.

## Regression validation

The frozen 1429-case descriptor/robustness/shader selection completed on M3:
**965 Pass, 464 NotSupported, zero failures**, matching the prior selection's
counts. Every bounded batch retained watchdog and thermal protection and the
runner reports healthy. Catalog SHA256:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
Selection: workspace `build/minecraft-vertex-cse/production-cases.txt`;
results: `build/m3-zink-bottleneck/cts-fragment-saturating/`.
This is a targeted regression selection, not the complete Vulkan CTS.

The original native Vulkan Minecraft capture also completed successfully;
all three saved frames are pixel-identical to its retained reference.
Run: `20260930T123733695811Z-m3-minecraft-demo-vulkan-check`.
Evidence: benchmark `diagnostics/m3-zink-bottleneck-20260930/native-vulkan-images.json`.
The image-check timing includes readback and is not a new performance score.
