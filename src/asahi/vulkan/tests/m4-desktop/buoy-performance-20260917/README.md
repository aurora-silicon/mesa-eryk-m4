# Buoy full-resolution diagnosis and Apple compiler comparison

2026-09-17, J713 / Apple M4 G16G. The user requested one Chromium Buoy instance
at the panel's full 2560x1664 resolution, then comparison with macOS-generated
shaders. Shader and noise texture hashes are unchanged. No reduced resolution,
shader simplification or disabled production Vulkan capability was used as a fix.

## Deeper compiler investigation (19:54 UTC)

The installed follow-up enables Mesa NIR global code motion (full value
numbering) before backend lowering for AGX3B fragment shaders. Other stages
keep their previous default. It shares repeated expressions across blocks;
it does not change the Buoy source, resolution, texture or advertised features.

The exact large compiler input is retained in
`_provenance/experiments/20260917T193551209180Z-buoy-deep-backend-capture/remote/chromium/compiler/26931a44937445f2786cd0ed9c916555d9cdf75d3d43a6f7239dfb9503c2b1bb/`.
Offline replay establishes actual forward `jmp_exec_none` instructions around
emulation branches. The sampler reduction mode does not overlap ordinary
HAL200 sampler fields: bits 54:55 are reserved. Neither "all cold branches
always execute" nor an accidental native reduction-mode encoding explains
this workload. Descriptor loads do recur inside the ocean loops.

| Experiment | Expensive fragment p95 | Backend result |
| --- | ---: | --- |
| Previously installed conditional setup | 112.69 ms | 602,392 bytes, no spills |
| Loop-invariant motion plus CSE | 111.53 ms | 609,016 bytes, no spills |
| Conservative GCM / weak value numbering | 103.83 ms | 532,474 bytes, no spills |
| Full GCM before backend lowering | 92.13 ms | 292,894 bytes, 40 scratch bytes, 20 stores / 72 fills |
| Conditional scaled-format arithmetic | 812.43 ms | 2,964 scratch bytes, 6,807 stores / 5,812 fills; reverted |

Full GCM reduced GPU time about 18% against the previously installed fix in
these bounded runs; observed late page intervals were 9.35 FPS. Scratch/spill
counts are static, not the number of dynamically executed memory operations.
A late-GCM alternative compiled offline to 334,602 bytes with 64 scratch bytes;
it was not run on hardware or retained in the driver.

The scaled-format branch experiment demonstrates why merely avoiding ALU work
is insufficient: the extra control flow caused substantial register spilling.
That source change was reverted. The successful optimization deliberately uses
the existing shared NIR pass instead of adding a custom scheduler.

Validation: all 57 targeted Vulkan 1.4 CTS cases passed (16 fragment-loop,
16 derivative, 9 fragment-conditional and 16 ordinary 2D-filtering cases).
The frozen catalog hash and global indices are retained in
`deeper/cts-cases.meta.json`. This is targeted validation, not a new full CTS
campaign or blanket conformance claim. The bounded fixed-frame application
probe passed. Its readback is **not byte-identical**: 562 of 921,600 bytes in
428 of 230,400 pixels differ, maximum channel difference 49/255, mean absolute
channel difference 0.001272/255. Most changed channels differ by one; the
magnified diff is retained. Code motion can change downstream arithmetic
fusion, but the exact numerical origin of these differences is not established.
Do not call this an exact-image match or silently discard the differences.

The original macOS comparison above remains an offline translated-source
comparison; no Apple FPS or native register-count measurement was obtained.
Native constant programs and cheaper ordinary-sampler handling remain leads,
not completed features. Full-resolution 60 FPS is still not achieved.

Installed follow-up Vulkan SHA256:
`e6d7de50d5a7c9ebc0fe361685280a3f7b644770dfecb7b8f657d202172a965b`.
The previous library is retained as
`/opt/asahi-m4-port/lib/libvulkan_asahi.so.before-buoy-gcm-094df3afc410`.
No diagnostic environment override is required or installed. Kernel and
Gallium remain unchanged by this follow-up. Individual experiment identities,
thermal records, compiler statistics and image-difference metrics are in
`deeper/`; complete logs remain under workspace `_provenance/experiments/`.

Installed normal-launch validation (`20260917T195342126412Z`) passed for
30 seconds at 2560x1664, fragment p95 92.27 ms and maximum 93.70 ms. Steady
five-second intervals measured 9.38, 9.18 and 9.38 FPS; thermal peak 80 C.
This reproduces the private comparison with the installed default and no
optimization override. The prior 112.69 ms result improves by 18.1%; against
the original 122.85 ms result, the two changes together reduce this GPU time
by 24.9%. The best result remains around 9 FPS, far from 60 FPS.

Debugging additions:

- `chromium-probe.py --compiler-stats --compiler-min-instructions 10000`
  captures large compiler inputs without exhausting the 32-bundle quota on
  browser startup shaders. Failures bypass this successful-capture filter.
- `--code-motion driver|none|gcm|gcm-no-vn|licm` makes private A/B choices
  explicit. `driver` leaves the default unchanged; `none` disables GCM for
  comparison. These map to the diagnostic `AGX_CODE_MOTION` override.
- `agx_shader_replay INPUT --gcm|--gcm-no-vn|--licm` validates and compiles
  a transformed retained NIR input without GPU execution. Explicit options
  suppress the default pass so it is not applied twice. Without an option,
  replay uses the driver's current default; use `AGX_CODE_MOTION=none` for the
  older lowering. `gcm-no-vn` still performs Mesa's weak, sibling-branch GVN.

## Findings

Normal Chromium uses ANGLE OpenGL -> Zink -> Honeykrisp. The installed baseline
rendered approximately 6 frames/s at 2560x1664. Expensive Buoy fragment passes
take approximately 123 ms on the GPU; a 60 FPS frame has 16.67 ms for all work.
This is directly measured GPU execution, not solely browser or presentation
overhead. Each animation frame also involves other render passes: do not count
the kernel's render-pass retirements as animation frames.

The compiler expands 2,907 intermediate instructions / 48 texture operations
to 153,145 / 864 through sampler-reduction, border, descriptor and texture
lowering. Baseline backend statistics: 69,935 instructions, 623,818 code bytes,
191 register half-slots, 512 estimated resident threads, 15 loops, zero scratch
and zero spills/fills. These are static compiler statistics, not dynamically
executed instruction counts or measured occupancy.

Diagnostic isolation, each in a private browser with a fresh cache:

| Path | Expensive fragment time, approximately | Backend code bytes |
| --- | ---: | ---: |
| Installed baseline, all features | 123 ms | 623,818 |
| Existing descriptor/UBO prefetch experiment | 119 ms | 627,646 |
| Custom-border emulation disabled for diagnosis | 104 ms | 213,628 |
| Border and reduction emulation disabled for diagnosis | 47 ms | 38,864 |
| Conditional explicit-LOD setup fix, all features | 113 ms | 602,392 |

The disable switches establish substantial overhead in the emulation path for
this ordinary repeat-addressed texture. They are not valid general rendering
configurations, feature qualification, or the deployed fix. The remaining
47 ms diagnostic result also does not establish a path to 60 FPS by itself.
Animation, CPU thermal policy, browser startup and host scheduling affect
observed FPS; GPU timestamp comparisons are more specific than wall-clock FPS.
Full records and per-pass statistics are retained beside this document.

## Apple comparison

Apple AIR-NT 32023.917 on the existing macOS compiler host compiled for
`applegpu_g16g`; no M1 results are represented as M4 hardware measurements.

The original Buoy GLSL body and Shadertoy main wrapper were translated with
glslang and SPIRV-Cross to Metal. Only the frontend version, uniform bindings,
unused header declarations and fullscreen vertex wrapper were adapted.
`translation/` retains those inputs, exact commands and hashes. This is an
offline native-compiler comparison, not a capture of Chromium's ANGLE Metal
backend, and not a native pixel or FPS comparison.

| Apple frontend configuration | Fragment text bytes, including constant program | Main entry offset |
| --- | ---: | ---: |
| Default math settings | 20,450 | 1,216 |
| `-fno-fast-math` | 76,176 | 1,344 |

Both Apple binaries contain `_agc.main.constant_program` at offset zero.
Our M4 backend explicitly disables preambles because the legacy AGX2 preamble
protocol is not the M4 Ioto protocol. Its zero preamble-instruction statistic
confirms no shader preamble was generated. Implementing the native constant
program is a separate optimization; simply removing `no_preamble` is unsafe.

The baseline is about 30.5x larger than Apple's default-math binary and 8.2x
larger than the stricter-math binary. These are **code-size ratios**, not speed
ratios. Our diagnostic shader without emulation is smaller than Apple's strict
build, so attributing the whole gap to poor arithmetic instruction selection
would be unjustified. Sampler emulation and missing constant-program support
are concrete leads. Apple register/loop/instruction counts were not established:
the available decoder rejects native forms, Apple's objdump lacks an AGX3
printer, and the bounded `metal-nt -S` attempt timed out without an output.

## Applied fix and verification

For explicit LOD and explicit gradient samples, calculate the software reduction
footprint only inside the branch that actually needs it. Keep implicit
derivatives and LOD queries outside the branch so nonuniform samplers cannot
make their quad derivatives undefined. All sampler modes remain supported.

- Six host control-flow cases pass: explicit LOD, explicit gradients and
  implicit derivatives, each with and without image-view minimum LOD.
- 48 min/max sampler oracles pass, with zero watched-byte mismatches.
- 32 weighted-view and 8 minimum-LOD combination oracles pass.
- The fixed 640x360, time=10 image matches the installed baseline byte for byte
  across all 921,600 RGBA bytes. The baseline run retained valid readback but
  failed a separate diagnostic rAF timeout check; its overall verdict remains
  process_failure and is not relabelled a clean application pass.
- The installed build passes a normal Chromium 2560x1664 animation, with late
  five-second intervals of 7.96 and 7.76 frames/s, expensive fragment p95
  112.69 ms and maximum 114.78 ms. No GL error/context loss. Thermal peak 76.5 C.
  This is roughly an 8% reduction from baseline fragment p95 122.85 ms, **not**
  full-resolution 60 FPS or a broad performance qualification.

Installed Vulkan SHA256:
`094df3afc410ccb6b13ec58e7935c99e818ab1407454ffa24761b8b56d7e3a03`.
The original `5cd202d31d68...` library is retained beside it. No diagnostic
switch or prefetch override was installed in the desktop environment. Gallium
and the kernel were unchanged during this investigation. All hardware runs
used the 60-second watchdog and six-channel thermal guard; owned test browsers
were closed after their bounded runs.

Further work: avoid generic emulation overhead for ordinary sampler state
without breaking descriptor changes or removing capabilities; implement and
qualify native constant programs; compare the same scene on native hardware
before making claims about the achievable full-resolution frame rate.

## Reproduce the offline native comparison

From the workspace root:

```sh
python3 tools/buoy-metal-source.py tools/shadertoy-buoy build/NEW-buoy-source \
  --spirv-cross build/spirv-cross-host/usr/bin/spirv-cross
python3 tools/native-metal-compile.py build/NEW-buoy-source/buoy.metal \
  --output build/NEW-buoy-apple --stage render --function main0 --vertex-function vs \
  --frontend /Users/m1/m4-metal-917/Metal.xctoolchain/usr/bin/metal \
  --compiler /Users/m1/m4-metal-917/Metal.xctoolchain/usr/bin/metal-nt \
  --remote-python /Library/Developer/CommandLineTools/usr/bin/python3 \
  --frontend-flag=-fno-fast-math
```

Omit the last flag for the default-math comparison. Native archives and checked
Mach-O sections are in `apple-default/` and `apple-strict/`. Use
`tools/buoy-profile-summary.py EXPERIMENT_DIRECTORY` to summarize the Linux
hardware evidence; its kernel-timestamp filter uses recorded target uptime.
