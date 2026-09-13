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

## Host STK reference

`stk-reference-host.py --output NEW_DIR --config ORIGINAL_CONFIG_0_10_DIR
--profile SHARED_PROFILE --seconds 180` runs a private x86 Zink/lavapipe build
under an isolated Xvfb display. It retains the original XML files, binary hashes,
capability profile, input requests, images and Vulkan trace. The ARM hardware
path still uses the mandatory guarded tools. Write ordered JSON requests to
`NEW_DIR/requests/NNN.json` with action `snapshot`, `key` (xdotool key name),
`click` (x/y), or `finish`. Reuse input requests instead of navigating again.

Build the reference with the same Mesa source (Zink plus lavapipe), host
GFXReconstruct, and privately extracted STK/Profiles packages under the paths
in the helper. Do not change game graphics settings to obtain a pass.
`vulkan-profile-intersect.py --formats` now includes the intersection of format
feature flags as well as features/extensions. Enable `SIMULATE_FORMATS_BIT` in
the Profiles layer and verify the reported capabilities before capture. The
initial STK reference without format simulation used unsupported D24 depth;
it is retained as evidence but is not an M4-compatible replay. The corrected
profile reports D32S8 and excludes D24. Software limits and X11/offscreen WSI
remain explicit replay adaptations; final native application checks are required.

## Core push-descriptor decoder fix

`gfxreconstruct-core-push-descriptors.patch` fixes a replay/JSON decoder omission
in revision 11891af: the Vulkan 1.4 core commands
`vkCmdPushDescriptorSetWithTemplate` and `...WithTemplate2` were captured but
silently skipped by the decoder. They share the KHR payload layouts; the patch
keeps the original core/KHR identity and dispatches the matching entrypoint.
Apply alongside the process-ID capture patch, rebuild replay and convert for
host/ARM64, and record their hashes. C++ export is not qualified by this change.

Before: STK software replay reported missing descriptor set0 and crashed in
lavapipe's generated shader. After: the unchanged capture completed3470 frames
through the race. M4 frame50 matched software with maximum RGB channel error3
and no pixels above threshold8, under the guarded thermal runner. The reference
still contained unwanted ShaderLayer capabilities from Profiles retaining
unlisted extensions; that is a separate capture-profile issue, not a pass of
unsupported M4 features.

Profiles extension simulation retains unlisted device extensions by default.
Use `VK_KHRONOS_PROFILES_EXCLUDE_DEVICE_EXTENSIONS` from the intersection
report's `removed_reference_extensions`. The STK host helper does this and
retains that report. Query the simulated device and check formats/features as
well as extension aliases; the layer can inject additional promoted aliases.

`vulkan-trim-host.py TRACE --output NEW_DIR --profile PROFILE --first N --last M` recaptures a
software replay into a short trace with GFXReconstruct's retained initial
state. This is x86-only and bounded. It uses captured X11 presentation under
private Xvfb because lavapipe lacks the frame-boundary extension required by
offscreen recapture. Retain the original, validate the trim on software, then
use the target's guarded runner. Trim creation alone proves no GPU result.

## Submission-state tracking and explicit replay adaptations

Apply gfxreconstruct-submit-state.patch to GFXReconstruct11891af. The tracker
previously skipped every command buffer in a submit array whenever its first
entry was wait-only. Zink uses this pattern, so image layouts stayed UNDEFINED
and the trim omitted live textures. Inspect every entry for both Submit variants.
Rebuild gfxrecon-replay as well as the capture layer: --capture uses a statically
linked encoder. Rebuilding the layer alone does not update recapture.

The corrected11-frame SuperTuxKart trim (full frames5095-5105) matches original
frame5104 exactly on software. Before:198402 pixels differed by more than8;
after:0 differing pixels. Evidence: _provenance/stk-reference-20260912/trim-linked-diff.
Original, broken trims and validation logs remain retained.

Recapture automatically enables optional maintenance10 and creates unused
ShaderLayer modules, even with the application profile. The hardware replay
rejects maintenance10. vulkan-trace.py --remove-unsupported is an explicit
diagnostic adaptation recorded in its manifest and GFXReconstruct log; it must
never be counted as unmodified application support. Native application validation
is still required. HK_DEBUG_LOG=1 and MESA_LOG_LEVEL=debug retain Mesa source
locations for command-recording errors, avoiding blind retries.

Firefox reference profiles now also apply format simulation and the explicit
removed_reference_extensions list from their adjacent .report.json file.

## Draw-by-draw attachment comparisons

`vulkan-draw-dump.py TRACE.jsonl --first CALL --last CALL --output NEW.json`
selects submitted primary draws and their command-buffer/render-pass boundaries.
Pass this configuration to guarded `vulkan-trace.py replay --dump-config`.
`--inputs` also requests descriptors and vertex/index buffers; raw attachment
comparison is the smaller starting point. Secondary command buffers, repeated
selected submissions and multi-draw arrays larger than one element currently
require explicit additional implementation and are rejected by this helper.
The underlying native multi-draw command is retained; it is not decomposed.

`vulkan-dump-compare.py SOFTWARE_DIR HARDWARE_DIR --output NEW.json` correlates
raw attachments by draw, image and subresource, reports hashes and byte errors,
and distinguishes bytes changed by the reference draw. It does not qualify
floating-point tolerances or undefined attachment regions as rendering failures.

The replay build also needs `patches/gfxreconstruct-draw-dump.patch`: GFXR's
original diagnostic path missed multi-draw checkpoints, dropped semaphore-only
submissions, and implicitly reset an auxiliary buffer in application pools that
forbid individual resets. The core-push-descriptor patch now dispatches the
matching core/KHR function in cloned command buffers too. Firefox's first-three
checkpoint software test completes and preserves its final frame byte for byte.
Captured `sampleShadingEnable` validation warnings remain in this reference;
the dump fixes do not silently rewrite that application's pipeline state.

Reference capture profiles may use `vulkan-profile-intersect.py --formats
--sample-counts` to intersect image formats and sample-count masks. This prevents
lavapipe-only 4x MSAA from entering the reference when the actual M4 path uses
single samples. It remains an explicitly restricted reference, not a native
application-support result. Trim profiles now use private layer manifests with
absolute library paths, so a missing system Profiles layer cannot silently
leave the trim unrestricted. Replay creates its compiler-capture directory
before setting `AGX_COMPILER_CAPTURE_DIR`, retaining failure bundles reliably.


## Inspecting shaders that compile but render incorrectly

Set `AGX_COMPILER_CAPTURE_SUCCESS=vertex|fragment|compute|all` alongside
`AGX_COMPILER_CAPTURE_DIR` to retain accepted compiler inputs too. This is
opt-in and shares the existing 32-bundle quota and 8 MiB per-input limits.
The manifest records `success`; accepted backend IR can be printed offline
with the matching `agx_shader_replay` build and `AGX_MESA_DEBUG=shaders`.
No application launch is needed to inspect the saved NIR again.

`vulkan-trace.py replay --replace-shaders DIRECTORY` retains and hashes explicit
SPIR-V substitutions named `sh<captured shader handle>`. This is diagnostic
adaptation, never evidence that the original application works.

`vulkan-shader-probe.py --name NAME --trace LOCAL.gfxr --remote-trace REMOTE.gfxr
--shader PROBE.spv --handle ID --dump-config DRAWS.json --output NEW_DIRECTORY`
runs the same replacement on host lavapipe and guarded M4, then compares raw
attachments. It checks capture hashes, saves binaries/configuration and records
both renderer results. Default end frame is 2; use `--end-frame` when needed.
Use this to isolate inputs/operations, then qualify a fix with the original
shader and capture. Firefox's blur diagnosis used it to distinguish correct
fragment texture queries from incorrectly relocated vertex descriptor handles.

Firefox probes retain a page screenshot and a compositor screenshot in addition
to native scanout when available. DPMS can make `m4_scanout` return ENODATA;
that condition is recorded explicitly rather than discarding the browser run.


`app-observe.py --guard-output DIR --seconds 20 --retain CONFIG -- /absolute/program ARGS`
observes a real program in the normal logged-in desktop under gpu-experiment.py.
It saves two native screenshots, renderer mappings, exact binary/config hashes
and compiler diagnostics, then stops its process group. `--capture` retains
native Vulkan calls. Survival is not a visual correctness verdict.

`chromium-probe.py --guard-output DIR --renderer lavapipe --capture
--capture-profile SHARED_PROFILE.json` records Chromium through ANGLE/GL/Zink.
It retains local CDP GPU diagnostics, page/native screenshots and process maps.
Use the same command with `--renderer asahi` and no profile for actual hardware
validation. A profile is a recorded reference adaptation; native runs retain
all application requests. CDP is isolated to this temporary browser profile.

`vkquake-reference-host.py --output NEW_DIR --profile SHARED_PROFILE.json
--seconds 12` creates an isolated x86 lavapipe/Xvfb shareware-map reference.
Shareware vkQuake ignores +commands (cmdline is set only in its registered-data
branch), so the helper uses an ordinary autoexec.cfg to select e1m1. The run is
time-bounded and retains a screenshot, mappings, trace and configuration.

`mesa-loader-install.py` fixes private EGL/DRI/GBM library discovery for clients
such as KWin with file capabilities, retaining exact backups and SHA256s. It
also registers the private Asahi ICD in the standard system ICD directory;
secure loaders ignore VK_DRIVER_FILES. Reapply loader RUNPATHs after replacing
those DSOs. Existing clients keep their previous mappings.

Plasma tests use `plasma-nested-probe.sh` and `plasma-nested-shell.sh` with
`app-observe.py`. A byte-identical KWin copy without file capabilities, retaining
the basename `kwin_wayland`, is used only for capture instrumentation. The
original capability-bearing executable suppresses loader environment settings.
Nested success establishes compositor/desktop rendering, not standalone login.

## Exact software reference frames

`vulkan-replay-software.py TRACE --output NEW_DIRECTORY --frames 60` replays
an existing trace on the x86 host lavapipe build with four CPU threads and a
60-second default bound. It records the capture, replay and ICD/library hashes,
logs, exact rebind/offscreen/no-pipeline-cache adaptations and PNG frames.
Compare those frames with the guarded hardware replay using image-compare.py.
This helper never selects the M4 or any host hardware GPU.

Optional `--dump-config FILE` retains and applies a GFXReconstruct resource-dump
selection for numerical comparison of intermediate resources.

`vkquake-indirect-probe.py --dump capture_dr.json --dispatch INDEX
--push-constants VALUES.bin --globals reference/vkQuake/Shaders/globals.inc
--output NEW_DIRECTORY [--mode generate]` creates a one-buffer numerical probe
from the captured indirect-generator inputs. The default records visibility and
backface decisions; `generate` retains early returns, atomics and index generation
and adds per-surface stage markers. These are explicit diagnostic adaptations,
not proof of the original application's correctness. Index allocation order from
contended atomics may differ legitimately; compare draw counts and triangle sets.
The manifest records captured input hashes, constants, invocation count and
probe-output offset. Compile `probe.comp` with glslangValidator and execute using
`vk-buffer-oracle.c`, with gpu-experiment.py for hardware. `M4_ORACLE_SOFTWARE=1`
selects only lavapipe's CPU device for host reference generation. Comparing a
reference-generation run against its unchanged input intentionally reports a
mismatch; retain its output and validate expected semantics before reusing it.

`atomic-oracle.py --output NEW_DIRECTORY` generates a64-lane exact integer
regression for eight SSBO atomic operations with divergent inactive lanes.
Use the same guarded vk-buffer-oracle workflow; host lavapipe independently
checks the CPU-calculated reference.

## Standalone desktop qualification

`sddm-session-probe.py --guard-output '{output}' --session plasma --seconds 10`
runs inside `gpu-experiment.py` (65-second outer bound). It temporarily supplies
SDDM autologin through a runtime systemd mount namespace, captures native
scanout and renderer maps, and restores the normal greeter and remembered
session. It never changes the default boot target. A detected GPU fault retains
evidence and leaves login stopped for recovery. `--session gnome` uses the same
runner once GNOME is installed. A process/map pass still requires reviewing the
final desktop image; it is not evidence for every desktop application.

Plasma 6 standalone run `20260912T120807742380Z-plasma6-standalone-sddm` passed
with KWin and plasmashell using the private M4 Asahi library, a correct native
desktop image, and peak monitored temperature 56.75 C. This complements the
earlier nested Plasma/Konsole validation.

## Offscreen presentation fences

Apply `gfxreconstruct-offscreen-present-fences.patch` and rebuild replay on host
and target. The offscreen swapchain consumed present wait semaphores but ignored
maintenance1 presentation fences, so Blender's first successful fence query
waited forever on both lavapipe and M4. Signal the first presentation fence on
the wait submission and additional swapchain fences with ordered empty submits;
consume the binary wait semaphores and emit the frame boundary only once.
The unchanged nine-frame Blender reference now completes on host lavapipe in
2.88 seconds; its first frame remains byte-identical. The software helper now
retains a structured timeout result too. This repairs replay synchronization,
not the driver, and does not remove waits or claim native application support.

`vulkan-draw-dump.py --draw INDEX --draw INDEX` selects sparse checkpoints
instead of a contiguous range. This avoids hundreds of attachment dumps while
bisecting Blender's composition graph. Its first UI draw differs by at most
one byte; later comparison traces incorrect panel inputs back into the overlay
passes. Five earlier apps and GNOME retain their prior qualification boundaries;
Blender is not yet visually correct. The unchanged nine-frame capture completes
on M4 in5.29s after the replay presentation-fence fix (234038, peak56.25 C).

Live application probes now check the retained native DCP failed-flip counter
before launching, and every experiment records pre-existing display status.
The hourly unhandled D114 callback (4164/4100 bytes) failed the display during
offscreen compiler tests while GPU retirement continued; this is a separate
known display issue, not a GPU test success or an inferred idle/DPMS state.
Evidence is retained in `_provenance/dcp-20260913`; recovery reboot requested
at00:12 UTC. The display-manager boot policy is unchanged.
