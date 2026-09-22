# M3 0 A.D. shader experiments

Two independent, disabled-by-default options target dynamic texture metadata:

- `AGX_NATIVE_MEMORY_PREAMBLE=true` admits uniform, explicitly speculatable
  constant-memory loads to the native arithmetic preamble. Mutable storage,
  texture operations and descriptor-state stores remain excluded. Existing
  NIR preamble analysis must prove uniformity. Exported values retain the
  native 32-bit uniform contract.
- `HK_NATIVE_BRANCH_SCALED=true` puts scaled-format conversion behind its
  descriptor predicate. The texture operation, including derivatives and
  residency, stays outside the branch. A cloned texture instruction permits
  replacing SSA uses across the newly created control-flow blocks.

Compiler cache bits 40 and 41 separate both options for HAL200 devices.
Neither option changes advertised capabilities, application quality, clocks,
thermal policy or the installed desktop's default options.

The original branch prototype incorrectly used a same-block SSA rewrite after
creating control flow and crashed in the compiler. The final implementation
replaces the cloned definition and invalidates control-flow metadata.

On J514S, the native 3024x1964 0 A.D. replay completes with each option. The
50-iteration frame-2 replay images are byte-identical to the matching looped
reference. Looping itself changes the third frame relative to a non-looped
replay, and GFXReconstruct's reported FPS does not count these repeated frames;
do not use it as game FPS. The original three-frame memory-preamble replay is
also pixel-exact. A live memory-preamble sample shows no material gain, so it
is not enabled in the launcher. The branch experiment measures 73.48 FPS with
conventional Vulkan descriptors versus 70.95 without the option; short samples
alone do not establish the size of a gain.

Sixteen focused scaled/normalized-format CTS cases all Pass using the retained
M4-matched `deqp-vk-seed30-scaled-blit` binary, both options enabled, under the
M3 watchdog and five-channel 95 C thermal guard. See `evidence.json` for exact
case indices, catalog, binaries and retained experiment paths. No general
Vulkan compliance or OpenGL performance claim follows from this sample.

## Zink presentation serialization

Honeykrisp was missing from Zink's Mesa-driver synchronization list. The
fallback issued an empty submission, then waited for its fence while holding
the queue mutex before every presentation. Add Honeykrisp to the existing
Mesa path: `vkQueuePresentKHR` receives the original wait semaphore and the
common Vulkan WSI implementation provides display synchronization. No wait
semaphore, rendering fence or kernel retirement check is discarded.

Matched EGL/GBM/Gallium builds differing by this switch measure 48.85 FPS before
and 78.61 FPS after on the unchanged 3024x1964 Acropolis Bay scene. Actual mapped
library hashes are retained in `zink-presentation.json`. Both bounded probes
complete without GPU faults; the optimized screenshot shows terrain, buildings,
foliage and shadows. This is a short starting-scene comparison, not a 120 FPS
result or broad presentation-compliance claim. The normal desktop installation
remains separate from these candidate libraries during the investigation.

## Identical colour export coalescing

`HK_NATIVE_COALESCE_BROADCAST=true` recovers the broadcast ABI when all eight
colour locations store the same full vector, once each in the same block,
without an intervening side effect. Partial, differing, dual-source and
control-flow-split outputs remain untouched. Honeykrisp remaps each active
attachment to the shared export; unused attachments remain disabled. Cache
bit 42 separates the option, which remains off by default.

The host `asahi-fragcolor` test covers acceptance and six rejection cases.
The J514S surfaceless Zink oracle passes nine full-image comparisons: equal
and distinct outputs plus discard, with identity, reversed and sparse output
mappings. Probe source and the bounded wrapper are retained beside this file.
All guards retain the M3 watchdog and five-channel thermal policy. Raw evidence
is `/root/m3-apps/0ad-120-broadcast-mrt`; `broadcast-oracle.json` identifies
the binaries and retains the output.

Selected simple game shaders fall from at least 73 to 24–40 half-registers.
The more complex material shaders still use up to 104. Live uncapped samples
measure 80.59 FPS with coalescing and 85.57 FPS with additional lossless
framebuffer compression. The latter 3024x1964 screenshot was inspected: terrain,
structures, foliage and shadows remain present. These short samples are not
120 FPS or a broad compatibility claim. The normal desktop libraries are still
separate from the experimental prefix.
