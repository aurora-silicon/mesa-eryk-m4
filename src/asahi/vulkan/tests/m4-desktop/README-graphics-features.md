# Graphics feature oracles

`vulkan-fragment-probe.py` now accepts `--geometry`, `--storage-stage`,
`--storage-expected`, `--extent`, and `--reference-host`. It retains generated
sources, SPIR-V, build commands and hashes. Storage binding 0/set 0 starts at
zero, with a 64-byte guard beyond the descriptor range. Expected bytes are
independent little-endian uint32 data. Both pixels and all storage words must
match. The executable requests the corresponding Vulkan features; it never
silently drops the request. Host reference builds accept only llvmpipe.

Example (fixtures in the Mesa mirror's `graphics-features/`):

```
python3 tools/vulkan-fragment-probe.py plain.frag --vertex storage.vert \
  --storage-stage vertex --storage-expected vertex.bin --rgba 255 0 0 255 \
  --output build/vertex-storage
```

Use `--reference-host` for the software reference. Hardware binaries must run
under `gpu-experiment.py` with the watchdog and thermal guard. These are small
feature oracles, not application or full Vulkan-conformance qualification.

2026-09-12: initial native runs rejected vertex TPS scratch, fragment helper
coverage, and the application geometry draw path before submission. The native
SPECTPR path already supports scratch allocation: removing the stale graphics
restriction passes exact vertex writes/atomic returns/counter/pixels in
214621804283Z (56 C peak). Applying the existing XFB generated-VS draw routing
to application GS passes passthrough geometry in 214639828042Z (55.75 C).
Capabilities remain off by default while broader cases are investigated.

Apple offline compiler witness `helper.metal`, AIR-NT32023.883 targeting G16G:
`simd_is_helper_thread()` reads original SR2 as a 16-bit value (f48=1), then
compares it with zero. The read bytes are `0484110601000000`. Original archive,
metadata and field decode are retained in `build/native-helper-thread/`.

The adapter also supports `--indexed` (uint32 input indices). Native indexed
GS amplification with smooth varyings matches the two-color software oracle
in 215512224981Z. Odd 31x29 fragment storage/atomic-return checks pass in
215255889302Z after implementing initial helper coverage and allowing folded
unsigned immediate atomic offsets. Peak temperatures remain <=56.25 C.

Do not assume one fragment invocation per pixel with MSAA: clipping/coverage
can split invocations even with sample shading disabled. The attempted fixed
count oracle at 4x failed on both lavapipe and M4 and is not qualification.

`half-image-store-probe.c` exercises RGBA16F storage writes via Zink in compute
(default) or fragment (`fragment`) shaders. Four channels include negatives,
values above one, and exact fractions. It verifies all 1024 pixels after GPU
readback. Compile with `cc -O2 -Wall -Wextra ... -lEGL -lGL`. Software references
set `M4_TEST_LLVMPIPE=1`; hardware must use the guard. M4 220032/220100 both pass
at <=55.75 C. Blender's captured RGBA16F compute pipeline now compiles in replay.

`reciprocal-signs/` is a vk-buffer-oracle fixture covering abs/neg inputs to
reciprocal and reciprocal square root over powers of four. M4 220439398493Z
matches every expected float bit (128 words), peak55.75 C. The implementation
materializes sign modifiers as GPU bit operations before register allocation.
The original Blender pipeline at capture API608 now compiles; replay proceeds
to a separate deep-break control-flow failure at API617.

`nested-break/` tests exits through two nested conditionals with divergent
per-thread bounds and observable atomics. CPU and lavapipe agree with guarded
M4 220737762002Z (256 words, <=55.75 C). Apple offline `break2.metal` and
`break3.metal` establish CNDCF depth fields3/4: `8f0554030000` and
`8f0554040000`. The backend now uses CNDCF for nested exits, retaining CNDLT
for direct loop tests; no guessed unwind/pop sequence is needed.

`high-compare/` keeps80 volatile floats live across a negated branch. The M4
compiler actually compares r83 against -r2, exercising the upper half-register
range. 221006716398Z matches5248 words (<=55.75 C). The old encoder rejected
half-register indices>=128 despite the field carrying8 bits. The normal
alignment, sign and operand-hint checks remain enforced.
