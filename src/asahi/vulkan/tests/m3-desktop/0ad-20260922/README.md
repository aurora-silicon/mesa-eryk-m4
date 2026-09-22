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
