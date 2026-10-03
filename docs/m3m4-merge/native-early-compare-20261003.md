# Opt-in early integer loop predicates on G15/G16

`AGX_NATIVE_EARLY_COMPARE=true` computes a plain 32-bit register loop
predicate before up to eight independent FFMA instructions in an ordinary
AGX3B shader. The condition's exec-mask update stays at its original position
after the arithmetic. The default remains false, and AGX2/AGX3C, internal
shaders and preambles are unchanged.

The conditional-break rewrite can retain an integer test at the old if's
position when both removed branches have a single predecessor. This changes
no surviving instruction's execution order by itself, and preserves the
forward-skip heuristic for expensive remaining loop bodies. It makes the
predicate available to the bounded same-block packer schedule. The packer
refuses crossings with memory/control instructions, any outstanding dependency
or local wait, and physical writes overlapping either compare operand. Early
sources retain their values without last-use discard hints; the loop's branch
fixups and predicate slot stay unchanged. No new ISA encoding is introduced.

The Vulkan cache identity uses bit 1 of the explicitly hashed native-schedule
byte; Gallium uses driver_flags bit 33. Existing false-option cache identities
remain unchanged. Gallium's changed cache source cross-compiles in a syntax
check, but its complete driver and hardware path were not tested here.

On M3, with FMA uniforms and late loop optimization enabled:

- Three alternating arithmetic pairs: control GPU medians 0.837625 /
  0.838250 / 0.838083 ms; early predicate 0.754167 / 0.753708 /
  0.754167 ms. This is about 10.0% less GPU time. Every output word is
  checked against CPU fmaf before and after timing. The 128-MiB read/write
  copy control stays 1.968459 versus 1.968500 ms.
- All 318 compiler unit tests pass. Added checks cover predicate/math/CND
  ordering, the backedge, physical write hazards, memory dependencies,
  execution boundaries, excluded stages, external entries into removed
  blocks, and preservation of the expensive-body forward skip.
- All 972 frozen Vulkan 1.4 loop/control-flow CTS selections Pass, with
  exact original global indices. Every segment uses the actual 60-second
  watchdog, a 45-second bounded experiment and all-channel thermal guard;
  per-case bounds remain 15 seconds. No failures or missing cases are hidden.
- All 32 retained Minecraft compiler inputs produce byte-identical binaries
  with the option disabled/enabled. Both replay image checks match the
  qualified reference exactly. Three completed 30-frame pairs give 163.7447
  versus 163.3540 replay FPS (-0.24%), within variation. This optimization
  does not provide a Minecraft gain and is not enabled by default.

Arithmetic/copy maximum recorded temperature was 59.25 C. Actual Linux GPU
frequency is still unqualified; firmware state 8 is not a clock measurement.
These are same-boot diagnostic comparisons, not live Java FPS, macOS parity,
full conformance or M4 hardware qualification. Desktop Mesa was unchanged.

Catalog SHA256:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
CTS selection SHA256:
`c0c3ac147aea4c7fa6dd4fde4ae4e4a661b5a6e5412423eadd0878430ebac076`.
Private Vulkan candidate SHA256:
`206536eceaa946eae5b22eda8a5bc5291f45f31ff9478c4be712435f6398fb0e`.
Raw compiler, test, guard, thermal, correctness and timing evidence stays in
`gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/early-compare-*`
and `throughput/early-compare-throughput/`.
