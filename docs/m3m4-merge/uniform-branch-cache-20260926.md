# Register discard hints in uniform control flow

AGX3B compute and fragment shaders now use NIR's execution-divergence analysis
when deciding whether a block may discard last-use registers. Previously every
syntactically nested block disabled discard hints, including branches taken by
all lanes. Recompute divergence after late lowering; retain the conservative
rule for divergent blocks and other stages/encodings. This changes register
cache hints, not branch execution or masks.

Validation on 2026-09-26:

- 306 compiler unit tests and five captured hot-shader compilations pass.
- 24 pinned Vulkan CTS cases pass on each of M3 and M4: compute barriers,
  subgroup arithmetic, fragment conditionals and returns through nested loops.
  Catalog hash 72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf;
  indices and QPA results are retained. M4 experiment 115231711962.
- Eight CPU-exact mixed fetch/sample/divergent compute configurations pass on M3.
- Unchanged Satisfactory DX12 gameplay image budget passes at frames 6/20/35
  (check114717149978). Three warm 30-frame timings: 3.50918, 3.50640,
  3.50793 FPS, median 3.50793. Previous accepted median was 3.47167 FPS;
  this is a small gain, not achievement of the 5 FPS target.
- GPU runs used the mandatory 60-second watchdog and thermal monitoring.
  The normal desktop installation was not changed.

Exact binaries, patches, commands and results are retained in the local
benchmark provenance under satisfactory-dx12-optimization-20260926.
