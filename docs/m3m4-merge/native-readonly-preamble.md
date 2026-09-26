# Native read-only compute and fragment preambles

The opt-in native preamble path now accepts compute shaders and read-only,
reorderable memory loads of at least 32 bits. Bounded UBO lowering produces
`load_agx` with NON_WRITEABLE/CAN_REORDER; previously these stayed per-invocation.
NIR's preamble pass still proves address uniformity, reconstructs uniform control
flow, and checks speculation permission across nonuniform control flow.
Mutable storage and narrow reads remain excluded. The input/output uniform
block separation from d65ab1d8a00 is required.

Compute submission appends the compiler preamble after the Ioto resource loader,
with checked program capacity and bounds, replacing the original terminal STOP.
The command-buffer upload owns the copied bytes. Preamble scratch and unsupported
shader stages still fail closed. Existing watchdog/thermal policy is unchanged.

Use `HK_M4_NATIVE_PREAMBLE=true AGX_NATIVE_MEMORY_PREAMBLE=true` to test.
Both remain off by default pending wider conformance validation. Float arithmetic
also remains off: enabling it in fragment shaders changed one channel by one unit
in one pixel of the middle reference frame. No tolerance was added.

## Validation, 2026-09-26

Same saved Satisfactory Vulkan gameplay capture, 30 completed measured frames,
3840x2160 output / 2880x1620 internal, unchanged replay adaptations. All RGB
pixels match in frames 1/16/31 on both chips. This is not live-game FPS.

| Machine | Scaled-branch baseline median FPS | Read-only preamble median FPS | Repeats |
| --- | ---: | ---: | --- |
| M3 Pro | 4.60076 | 4.93492 | 4.93492, 4.94417, 4.90551 |
| M4 | 3.24920 | 3.50686 | 3.50686, 3.50719, 3.50515 |

Normal boots: M3 5faeff66-3e0c-4a38-931a-14c012650626; M4
7b02370e-2a9b-445b-935f-db0f87b80aa7. 60-second watchdogs and all-channel
thermal monitoring retained; desktop configuration unchanged.

- 297 host backend tests pass; ARM Vulkan driver builds.
- M3: 40 strict sparse-buffer configurations plus three 32,768-word sparse
  alias oracles pass with the preamble enabled.
- Pinned Vulkan CTS UBO layout and robust out-of-bounds UBO reads: nine Pass,
  three NotSupported on M3. The three require an exclusive compute queue and
  also report NotSupported on the normal baseline; no coverage replacement.
- Full-scene checks: M3 20260926T040246067495Z and M4
  20260926T040443381606Z. Original logs and exact comparisons retained in the
  private benchmark repository.

Descriptor-only preambles were correct but neutral. Additional read-only UBO
loads account for the useful gain. Compute-only float arithmetic also retained
exact images but its timings were neutral, so it is not enabled by this change.
The 5 FPS target is not yet reached.
