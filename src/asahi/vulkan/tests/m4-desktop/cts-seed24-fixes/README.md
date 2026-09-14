# Seed20260924: tessellation crash, failures and timeout diagnosis

The frozen 10,000-case report remains in `../cts-seed20260924-10000`.
These are separate corrected-CTS diagnostic results, not rewritten original
outcomes or a Vulkan conformance claim.

## Crash and false primitive-count failures

The real SIGSEGV was inside CTS readback, not a kernel/Mesa crash. Its mapped
buffer held 3,720 primitives, but the shader returned 10,974 (exactly three times
the expected 3,658). CTS copied using that count before its disabled assertion.

Khronos commit [2489fee39ac1](https://github.com/KhronosGroup/VK-GL-CTS/commit/2489fee39ac14e623a7b403dd50e60ad3f3d6e0c)
fixes this historical assumption: geometry shaders may execute extra invocations.
The unmodified upstream patch is retained. Our backport also checks the readback
allocation bound before copying, including negative counts. Invariance and exact
coordinate checks remain. The old count-inequality failure occurred before many
of those checks could run.

`indexed-combined.patch` includes all prior seed22 CTS corrections plus this
backport and the CPU comparison optimization. `cts-indexed-build.json` identifies
the independently built binary `/root/cts-1.0.2.6/deqp-vk-indexed`, SHA256
470ba3a2e317c8a529c63c968e9d218b8ab24a40acb20e2639b2c97f7fdc0ed2.
The build tool restored the pinned source files and original executable.

## CPU bottleneck and deadlines

At 100 Hz CPU-time sampling, 1,966 of 2,057 samples were in the CTS
InvarianceTestInstance::iterate function. Disassembly places the dominant PCs
0xa56444/0xa5643c inside the inlined cyclic primitive-search loop, including its
integer division. This was quadratic CPU comparison work, not GPU execution.

The indexed comparison preserves the original membership semantics: ordered
vertices, exact floating-point coordinates, duplicate invocations, signed zero,
and NaNs (which cannot match). It indexes B once in an ordered set. The retained
CPU harness extracts the actual CTS functions/types and checks 10,000 reordered,
duplicate, mismatched, NaN and signed-zero inputs against the old function.

Same complete outer_triangle_set.triangles_fractional_odd_spacing case:

| Version | Wall seconds | 100 Hz CPU samples | Outcome |
| --- | ---: | ---: | --- |
| Invocation correction, old search | 30.173 | 2057 | Pass |
| Invocation correction, indexed search | 10.869 | 126 | Pass |

No draws, subcases, precision checks or geometry were removed. These timings use
the same Mesa candidate library and 1.86 GHz guarded CPU cap. Sampling is
statistical, not a measurement of GPU time. The CPU sampler records PCs without
requiring kernel perf support; use it only with an application that does not own
SIGPROF/ITIMER_PROF and inside the normal experiment guard for GPU workloads.

Both original timeout cases pass alone in 1.68/1.63 seconds. The 20-second
parallel deadline also prematurely killed the now-complete outer-triangle tests.
The bulk runner now defaults to a recorded 60-second per-case wall bound;
`--case-timeout` is explicit and bounded (up to 120s). Thermal/progress checks,
the 60-second hardware watchdog, and the overall experiment bound remain active.
There are no automatic retries, baseline exclusions or status relabeling.

## Final targeted validation

All 19 affected tessellation cases (16 original failures, 1 original crash,
2 original timeouts) pass with the indexed diagnostic build, 20 workers,
47.232 seconds runner execution, peak 61.5 C. The boot ID remained unchanged.
Retained reports include binary/target identities, runner arguments and guard
results. This is the affected set only; no broad regression suite was run.
The separate API-format fixes are still being qualified.
