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
The completed API-format fixes and their additional qualification are below.

## Image-format and limit fixes

All 25 original API failures now pass (`api-final-results.json`). The 24
image-format queries pass without changing their original expectations. The
properties case additionally needs the version compatibility adaptation below.

M4's query path still imposed the initial RGBA8/RGB10-only multisample boundary,
while its global limits promised 2x/4x operations. Non-attachment-capable scaled
formats had the opposite problem: a storage-only override returned multisample
counts where the Vulkan image-format query requires a single sample. Remove
that override and enable the typed renderable-format path on M4. The minimum
attachmentless/depth/stencil sample limits now include 4x.

Actual rendering exposed and fixed two more boundaries:

- Native EOT stores need the physical format's conversion selector with the
  multisample addressing mode. Emit IMGWBLK field30=2 through the composer,
  preserving normalized8, integer8, normalized16, half/integer16 and raw32
  selectors. Five compiler tests match independently compiled Apple programs
  byte-for-byte. Native input bytes, compiler commands and exact G16G binaries
  are retained under `native-ms-eot`; raw format IDs, not experiment names,
  identify those inputs. No native GPU execution or Mac reboot was needed.
- The render-layout validator incorrectly restricted wide and integer formats
  to one sample despite sample-scaled tile storage. Retain its byte capacity,
  alignment, layer and layout checks while admitting the supported sample counts.
- Zink's dynamic rendering paths requested AVERAGE for integer color resolves.
  This triggered an assertion in the common Vulkan resolve shader. Both Zink
  paths now use SAMPLE_ZERO for integer view formats; float resolves still
  average. No driver-side acceptance of invalid average/integer operations.

The incremental 64x64 pixel oracle passed R8/RG8 normalized, RG16F/RGBA16F,
RGBA32F, RGBA8 signed/unsigned and R32 signed at both 2x and 4x: 16 combinations.
Each checks clear, sample masks, resolve and reload. Failed development attempts
are retained alongside the later passing results; the whole matrix was not
repeated unnecessarily. Six CTS depth/stencil 2x/4x controls also pass.
This qualifies those paths, not every possible multisample combination. Existing
layered/MRT export boundaries and untested packed-format paths still need their
own cases; the full Vulkan feature surface has not been certified.

## Legacy version check and final outcomes

After the real sample-limit failures were fixed, the original properties case
rejected any device API minor version newer than its 2017 headers. A separately
recorded `MESA_VK_VERSION_OVERRIDE=1.0` control passed. The final diagnostic build
instead leaves the native device version intact and logs the newer version while
checking all original Vulkan 1.0 properties, guard bytes and initialization.
[VkPhysicalDeviceProperties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceProperties.html)
permits the physical-device API version to differ from the instance version.
This adaptation is retained as `device-version.patch`; it does not test or
qualify the newer API's added properties or features.

`final-combined.patch` and `cts-final-build.json` identify the new default
`/root/cts-1.0.2.6/deqp-vk-seed24-corrected` executable, SHA256
2276dc0acf772673e1f173be98b30379467c17d46f5f36bffb5f571a7a4c5f06.
The bulk/campaign tools explicitly select that diagnostic executable. Original
binaries and the frozen 10k attempt ledger remain untouched.

`fixed-results.json` maps all original outcomes to retained targeted evidence:
**41 Fail, 1 Crash, 2 Timeout -> 44 Pass**. Of these, the 19 tessellation cases
need the invocation correction, and the device-properties case needs the newer
API compatibility adaptation. This is not a claim that 44 driver bugs were fixed
or that an unmodified official CTS now has those results. No broad regression
run was performed. The boot ID stayed c2f22724-ed20-4eba-9f9e-4d98de78a9d2 and
all hardware checks retained watchdog60, 0.5s thermal/progress supervision and
the 75 C cutoff; the largest recorded peak was 61.5 C.

Final installed Vulkan library SHA256:
2f5a2c3fdf57ec176dde32008f7cd3d7749e21152b2ca157b50225aab6b19271.
Final installed Gallium/Zink library SHA256:
d1abd8b367ef62d25f10f9bf126fa482b65819a97c94c6d76bd0cb8717381716.
The source commit may be newer than the build's embedded Git ID; these hashes
identify the exact tested libraries. Follow-up installation did not rebuild
already-tested code merely to change its embedded ID.

Normal SDDM/Hyprland login was restored under the watchdog/thermal check;
`display-restored.json` and `display-status.txt` retain the result. The default
boot remains graphical.target. After returning the lease, the persistent broker
still owns the armed 60-second watchdog. No workload was left running.
