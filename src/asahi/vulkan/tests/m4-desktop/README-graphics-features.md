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
