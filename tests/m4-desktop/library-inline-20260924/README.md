# Serialized library-call inlining cost

A retained private application fragment shader expanded to 237,000 NIR
instructions before generation-9 texture lowering. Re-indexing the complete
caller for every serialized texture helper made this stage quadratic.

Allocate fresh caller SSA indices only for surviving cloned definitions, before
splicing the callee. Preserve existing definitions and invalidate live-def
metadata. The builder no longer needs to re-index the entire caller per call.

Host validation on 2026-09-24:

- All 6,039 NIR tests passed (nine pre-existing disabled tests), including the
  new repeated-inline test with a parameter, branch, phi and existing caller
  definitions. Constant folding verifies the expected result.
- Generation-9 texture lowering on the retained input took 6.68 s before and
  0.364 s after. These are single-run compiler-stage timings, not game FPS.
- Normalizing SSA indices after lowering produced identical 27,502,957-byte
  textual NIR: SHA-256
  `3759c57573a36f512578eba33b64899e2e88938c2c3b32517dd32f2e4de27fcc`.
- The six graphics compiler subsets indexed-samplers, array-gather,
  cube-sample-lowering, 2d-robustness, sampled-storage-lowering and
  storage-image-lowering passed. The full multi-generation graphics test has
  existing unsupported-encoding failures: baseline and patched output are
  byte-for-byte identical, both exit 1. It is not a full-suite pass.
- ARM64 Vulkan library cross-build succeeded. Hardware rendering qualification
  is separate and pending at this checkpoint.

The proprietary shader input remains in the private local benchmark evidence.
`agx_shader_replay INPUT.nirbin --lower-texture-gen9` reproduces the stage
without opening a GPU. Set `AGX_REPLAY_NIR_OUTPUT` to retain normalized output.

Hardware follow-up: experiment
`20260924T011658309288Z-frostpunk-inline-menu-render` replayed all 600 native
application frames with the new build, original resources and no feature
removal. Four GFXReconstruct pipeline-creation workers were used after bounded
cold-cache segments. The 60-second watchdog was renewed by observed GPU
retirement; all 723 thermal samples were valid, peak 107.5 C, no new GPU fault.
The existing missing-menu-text defect remains reproduced; this validates the
compiler change's execution, not full application rendering correctness.
