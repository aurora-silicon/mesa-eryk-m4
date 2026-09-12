# Vulkan capture/replay workflow

The default graphics workflow is now: reuse a reference capture, isolate the
failing frames, replay under the mandatory guard, compare saved outputs, and
validate the fix in the real application. Do not repeatedly navigate game menus
or view unchanged desktop screenshots. This policy is also in root `AGENTS.md`.

For production OpenGL through Zink, capture the same private Zink build with
lavapipe as its Vulkan device. Keep application settings and a native failing
capture when device-dependent Zink choices matter. A restricted reference or an
offscreen replay does not prove unmodified application/desktop support.

## Installed tools and build

`reference/gfxreconstruct` is LunarG GFXReconstruct at
`11891af36f7f0d75213613be96f23d16c36f88d8`. Vulkan-Headers, SPIRV-Headers and
SPIRV-Reflect are initialized at the repository's pinned submodule revisions.
The ARM64 capture layer, replay, info and convert tools are installed privately
under `/opt/gfxreconstruct` on `omarchy-m4`. Their hashes and source/submodule
revisions are in `/opt/gfxreconstruct/build-manifest.json` and the host staging
directory `build/gfxreconstruct-stage`.

Build with the existing cross sysroot:

```sh
cmake -S reference/gfxreconstruct -B build/gfxreconstruct-aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/home/council/omarchy/graphics-build/aarch64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DGFXRECON_ENABLE_OPENXR=OFF \
  -DGFXRECON_TOCPP_SUPPORT=OFF -DGFXRECON_INCLUDE_TEST_APPS=OFF \
  -DBUILD_WERROR=OFF -DBUILD_WSI_WAYLAND_SUPPORT=ON \
  -DBUILD_WSI_XCB_SUPPORT=OFF -DBUILD_WSI_DISPLAY_SUPPORT=OFF \
  -Dnlohmann_json_DIR="$PWD/reference/gfxreconstruct/external/nlohmann-json/share/cmake/nlohmann_json"
cmake --build build/gfxreconstruct-aarch64 -j8 --target \
  VkLayer_gfxreconstruct VkLayer_gfxreconstruct_json \
  gfxrecon-replay gfxrecon-info gfxrecon-convert
```

The layer manifest's `library_path` must resolve to the private installed
`/opt/gfxreconstruct/lib/libVkLayer_gfxreconstruct.so`. Its JSON resides in
`/opt/gfxreconstruct/share/vulkan/explicit_layer.d/`. Do not enable this layer
globally for the desktop. `vulkan-swrast` supplies lavapipe; `vulkan-profiles`
supplies `VK_LAYER_KHRONOS_profiles` when a shared-capability capture is needed.

## Capture and replay

Deploy `vulkan-trace.py` beside `/root/firmware-snapshot.py`; it reuses that
tool's check for a live ancestor owning the 60-second watchdog and fresh six
channel thermal readings. It never opens or renews the watchdog itself. Its
child is bounded and terminated on timeout, thermal failure or interruption.
Do not run multiple guards concurrently on the target.

```sh
python3 tools/gpu-experiment.py --name reference-capture --seconds 40 \
  --hypothesis 'Capture a known-good software Vulkan workload' \
  --expect '^PASS: Vulkan capture process completed' -- \
  python3 /root/vulkan-trace.py capture --guard-output '{output}' \
  --renderer lavapipe --seconds 25 -- PROGRAM ARGUMENTS

python3 tools/gpu-experiment.py --name hardware-replay --seconds 40 \
  --hypothesis 'Replay the retained reference on M4' \
  --expect '^PASS: Vulkan replay process completed' -- \
  python3 /root/vulkan-trace.py replay --guard-output '{output}' \
  --renderer asahi --seconds 25 --trace /PATH/TO/capture.gfxr \
  --memory-translation rebind --omit-pipeline-cache --frames 1,4
```

`--frames` means a capture frame range in capture mode, and screenshot frame
numbers in replay mode. Replay still executes the input trace from its start;
use a trimmed trace to reduce its work. Replay uses an offscreen swapchain.
`--memory-translation rebind` adapts resource allocation; omitting pipeline
cache data avoids consuming a different driver's cache. Both are explicit and
recorded. Unsupported features are never silently stripped. Process success
alone is not pixel correctness; inspect warnings and compare outputs.

Each run retains command, renderer environment, ICD and library hashes, trace
hash on replay, selected adaptations and timestamps. `--retain CONFIG` copies
an application config into evidence. The outer runner retains source patches,
kernel logs and thermal/watchdog results. Use existing `image-compare.py` for
saved screenshot comparisons; view its montage only when useful.

For GUI capture, supply the actual session's `XDG_RUNTIME_DIR` and
`WAYLAND_DISPLAY`, and launch as the appropriate user. The helper does not
discover or modify sessions. Large interactive reference captures need the
existing qualified long guard with fresh GPU retirement and thermal readings;
a software workload alone is not a watchdog progress signal. Do not replace
that requirement with an unconditional heartbeat.

## Matching capabilities

Unrestricted lavapipe enabled features that M4 did not expose. The first replay
failed at Vulkan call 22 (`vkCreateDevice`), before shader execution. Retain this
as a capability mismatch, not a GPU crash.

`vulkan-profile-intersect.py TARGET.json REFERENCE.json --output NEW.json`
intersects `vulkaninfo --json` feature/extension snapshots. It uses Vulkan's XML
registry to filter extension structures and retains software-device properties,
including host-image-copy layout arrays. It saves input hashes, explicit
exclusions and differing features beside the profile. Limits and formats are
not made equivalent to M4. Replays do not enable the Profiles layer.

Capture with `vulkan-trace.py capture --renderer lavapipe --profile NEW.json`.
Use the originals plus the generated report to audit restrictions. Recreate the
profile when the driver's exposed capabilities change.

Lavapipe has application-specific capabilities: for engine `mesa zink`, it
exposes `VK_EXT_external_memory_dma_buf` only if both import and export work;
ordinary `vulkaninfo` needs only one. The J713 software-reference profile must
therefore explicitly pass `--exclude-extension VK_EXT_external_memory_dma_buf`.
This is a reference capture restriction, not a production-driver change.

The installed Profiles layer produced misleading feature warnings while
applying default-false values, and initially replaced host-image-copy property
arrays with null pointers. Keeping the software profile's properties in the
intersection avoids that observed crash. Keep the original logs and validate
the resulting capture on the software device before using it as an oracle.

## Resource-level diagnostics

`gfxrecon-convert --format jsonl --output capture.jsonl capture.gfxr` exposes
exact Vulkan call indices without launching a renderer. `gfxrecon-info` reports
capture device, frame count and pipeline counts. Keep large JSONL offline;
summarize relevant calls instead of dumping the entire file into the chat.

`vulkan-trace-readbacks.py capture.jsonl --output NEW.json` selects submitted
image-to-buffer transfers for GFXReconstruct's `--dump-resources` mode. The
helper handles direct primary command buffers and refuses secondary/repeated
submissions rather than guessing their execution context. Pass the result to
`vulkan-trace.py replay --dump-config NEW.json` to request raw buffers.

**Not yet qualified:** per-transfer dumping of the 15-readback HDR trace stalled
on lavapipe and was stopped by the 25-second workload bound. Do not use that mode
as an automated correctness gate until diagnosed. Ordinary replay of the shared
feature HDR trace completed on M4, but its image-layout warnings still need an
output comparison. The original captured probe passed all 15 CPU pixel checks.

## Initial evidence, 2026-09-12

Evidence directories below are under workspace `_provenance/experiments/`:

| Run | Result and boundary |
| --- | --- |
| `20260912T023902956084Z-gfxrecon-hdr-lavapipe-capture-v2` | Unrestricted Zink/lavapipe HDR capture: all 15 pixel checks passed; 1.4-second workload. Buffer-address capture warnings retained. |
| `20260912T023949356523Z-gfxrecon-hdr-asahi-replay` | Unrestricted capture rejected at call 22, `VK_ERROR_EXTENSION_NOT_PRESENT`; no GPU crash. |
| `20260912T024103148505Z-gfxrecon-device-profiles` | Original M4/lavapipe capability JSON snapshots. |
| `20260912T024551021543Z-gfxrecon-hdr-common-profile-v2` | Shared-feature Zink capture: all 15 pixel checks passed; 112 KiB trace. |
| `20260912T024611937910Z-gfxrecon-hdr-common-asahi-replay` | Shared-feature trace executed on M4; process pass only, image-format/layout warnings retained. |
| `20260912T024835338383Z-gfxrecon-hdr-zink-profile-capture` | Reference additionally respects lavapipe's Zink-specific DMA-BUF restriction; all 15 pixel checks passed. |
| `20260912T024859039393Z-gfxrecon-hdr-zink-reference-readbacks` | Per-transfer dump replay stopped at 25 seconds on lavapipe; unqualified. |
| `20260912T025016580635Z-gfxrecon-window-reference` | Four-frame `vkcube` capture against the greeter's Wayland socket did not finish within 20 seconds; no window-frame comparison claim. Retry on an unlocked application session. |

All these experiments used the existing watchdog/thermal guard. No screenshot
was viewed while constructing this workflow. A complete SuperTuxKart reference
capture, trimmed failing frames and the corresponding pixel comparisons are
still pending; do not claim the game is fixed based on these tooling checks.

Primary tool documentation:
[GFXReconstruct Vulkan usage](https://github.com/LunarG/gfxreconstruct/blob/dev/USAGE_desktop_Vulkan.md),
[resource dumps](https://github.com/LunarG/gfxreconstruct/blob/dev/vulkan_dump_resources.md),
[Khronos Profiles](https://github.com/KhronosGroup/Vulkan-Profiles/blob/main/layer/TUTORIAL.md).
