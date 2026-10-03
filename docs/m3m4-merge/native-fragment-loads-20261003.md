# Opt-in fragment read scheduling on G15/G16

`AGX_NATIVE_FRAGMENT_LOADS=true` extends the existing bounded, post-allocation
native load scheduler to ordinary fragment shaders. The default is false.
Compute scheduling, constant programs, internal shaders and other encodings
are unchanged. A read can cross at most 32 independent arithmetic operations;
physical half/vector register RAW/WAR/WAW hazards, memory order, coverage,
system reads, uniform reads and control-flow boundaries remain barriers.
Scoreboard assignment and register lifetime hints still run after scheduling.

The original Vulkan 64-bit compiler option key is full. One explicit option
byte now extends the pipeline/shader UUID hash and disk-cache namespace;
no device-debug or code-motion bit is reused. Native Gallium separates the
same option in driver-cache bit 32, above its 32-bit device debug mask.
The public debug declaration stays outside serialized shader-key headers,
so trusted existing compiler replay inputs remain compatible.

M3 qualification on boot `f1472553-243f-455d-bc6c-652052b428bf`:

- All 32 retained Minecraft compiler inputs replay successfully; nine fragment
  binaries change order. No shader arithmetic or application setting changes.
- All 314 backend compiler unit tests pass with the option enabled.
- All 1,674 selected frozen-index Vulkan 1.4 cases complete: 1,656 Pass,
  18 NotSupported, zero failures/crashes. Selection covers the full GLSL
  derivative catalog plus fragment discard controls. Unsupported cases remain
  in the record and are not passes.
- Three original Minecraft gameplay images match every RGB pixel in both
  configurations at 3024x1837. Nine alternating pairs of thirty completed
  replay frames give medians 167.6572 / 168.3059 FPS (+0.39%). The sample
  distribution varies between about 163 and 169 FPS, so this does not
  establish a material improvement or justify enabling the option by default.
- The final lightweight-header cleanup reproduces all 64 shader binaries
  byte-for-byte (32 inputs, both options), passes the compiler tests again,
  and passes the native Gallium cache source's ARM64 syntax check. Final
  Vulkan candidate image checks also match exactly in both configurations.

The broad CTS and nine-pair study use isolated Vulkan library SHA256
`31e8cd2cb8ee81ce17d779409ac7a6747539217bcbb7a3c67ae311459b930f3c`.
The final header-only rebuild is
`00399a92b627b848ea758b6fe05aba98666f32030d2e6d6af397302a52b43f9b`;
its separate image checks and short timing control are retained. This is not
an M4 hardware qualification, native Gallium hardware test, full conformance
claim or desktop deployment.

Catalog SHA256: `72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
Selection SHA256: `b348aefedbe12fc4e1be19073bdd6f06d7a08a4d2945f441fb38077131b0818b`.
All hardware runs retain the real 60-second watchdog, bounded lifetimes and
all-channel thermal protection. The desktop uses its previous Mesa package.
Raw evidence remains private under
`diagnostics/m3-macos-driver-gap-20261003/fragment-loads*`.
