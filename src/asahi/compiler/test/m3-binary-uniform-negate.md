# Native fragment uniform ADD/MUL experiment

`AGX_NATIVE_BINARY_UNIFORMS=true` is default-off. It retains one aligned
F32 uniform operand for a scalar FADD/FMUL in ordinary AGX3b fragment bodies.
The packer commutes the uniform into source zero, carrying negate modifiers
and the varying register's physical lifetime hints together. Source widths,
absolute values, saturation, memory operands, vector operands,
preambles, secondary/internal shaders and other stages/encodings retain the
ordinary materialization path. Two uniform operands materialize one into a
GPR. Uniform values and descriptor ownership are unchanged.

Own Minecraft pipeline 110 on J514S/macOS 26A428 supplies 19 complete
ADD/MUL instructions with ordinary GPR destinations; the new core test
reproduces all their bytes independently. Native uniform reads use f42=3
and f36 instead of the GPR operand f35. ADD includes negated uniform sources.
All six asynchronous producer waits are retained. Bounds, invalid lifetime
hints, commuted operands, source modifiers, fallback domains and asynchronous
load consumers have host tests. The policy has its own native-policy byte bit
6 in Vulkan's shader/pipeline UUID and bit36 in Gallium's disk-cache flags.

All 329 compiler tests and the 384 original modifier vectors plus 19 native
uniform vectors pass. Both modes compile all 32 retained fragment inputs;
26 binaries change and their optimized NIR is identical. The selected hot
body c14fba79 becomes d3e035dd: 259 to 236 compiler IR instructions,
67 to 44 machine COPY instructions, unchanged 71 register halves and no
scratch. Code size drops 1978 to 1934 bytes. Static instruction counts do
not establish a speedup.

Both guarded Minecraft checks reproduce all three approved frames exactly.
Nine same-boot alternating pairs measure 178.4743 versus 180.1978 FPS,
median paired improvement 1.2351 percent, all nine positive. A repeat measures 178.7138 versus 180.1760 FPS (+1.1724 percent median
paired, eight of nine positive). Across all 18 pairs the median paired gain
is 1.1829 percent, 17 positive. Focused 3036-case frozen-catalog arithmetic
and texture qualification gives 2575 Pass, 461 NotSupported and no failures
or device losses. Global catalog indices and all unsupported cases are kept;
this is not full CTS conformance.
All timings include 30 completed 3024x1837 gameplay replay frames and final
drain; setup, warmup and PNG work are excluded. These are replay FPS, not live
Java gameplay performance or macOS parity. No default, normal desktop,
M4 hardware qualification or published benchmark result is changed.

This extends the earlier rejected, unmodified-operand-only prototype to
negated operands and retests with the newer repeated-sampler path. Its earlier
negative timings remain historical evidence, not overwritten results.
Raw study: gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/
binary-uniform-negate. Isolated DSO SHA-256:
95e2ad7267c3ddecfa7d8e710a4fab9f3b571b1bc88223eee86c138c70062105.
