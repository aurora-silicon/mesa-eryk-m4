# Random Vulkan 1.4 sample: failure fixes, 2026-09-17

The original 10,000-case record remains unchanged in the sibling
`cts-random-10000-20260917` directory. This follow-up addresses its 139 Fail,
11 Crash and 1 ResourceError. `resolutions.tsv` preserves all 151 original
catalog indices and classifies each follow-up. No full Vulkan conformance or
fresh 10,000-case pass is claimed.

## Results and causes

- 137 image failures pass with the already-corrected CTS build on the unchanged
  driver. These include scaled-format and YCbCr reference errors. The original
  executable reintroduced old reference problems into the regression sample.
- Five scaled-texture swizzle crashes occur inside CTS `createShaderModule`,
  before the driver call. They also pass on the unchanged driver using the
  existing corrected CTS build. `swizzle-backtrace.txt.gz` retains the stack.
- Six SSBO cases exhausted the compiler's fixed 2,048-half spill-slot table.
  Release builds then entered a GPR-only shuffle allocator with memory SSA,
  causing SIGSEGV. The compiler now models the supported stack range, sizes
  bookkeeping to the shader/copy set, and reports exhaustion safely. All six
  original cases pass with the unmodified CTS binary.
- Native sin/cos magnitude clamping converted NaN to 1.0. Preserve required
  non-finite results according to NIR float controls; fast-math shaders retain
  the old path. The original fp16 sin(infinity) case passes.
- Shadow-array gradient sampling wrote offsets then overwrote the same staging
  word with the layer. Compose both into one PACK_SMPIR publication. The
  mirrored 1D-array shadow textureGradOffset case passes with original CTS.
- The 91-case WSI sequence reproducibly exhausted the private headless
  compositor's GPU virtual address space. Zink could retain thousands of
  queued batches. Limiting this queue to 64 and waiting for the older half
  through its existing timeline mechanism fixes the sequence: 1 Pass,
  90 NotSupported, no ResourceError or compositor crash. The original
  ResourceError case is unsupported when a live compositor is available;
  this is not a newly supported WSI feature.

The combined private Mesa build passes all 150 non-WSI cases with corrected
CTS (51.816 s). Another 324 original-CTS float-control and shadow-gradient
neighbors finish with 219 Pass and 105 NotSupported, no failures (10.459 s).
All hardware runs used the 60-second watchdog and six-channel 0.5-second
thermal monitor with 95 C cutoff. These runs peaked at 66.25 C or below.

Offline checks: 259 register-bank permutation cases plus spill-capacity and
exhaustion boundaries; 19 spill/parallel-copy tests; 6,413 native texture
encoder checks. The large captured SSBO shader passes full register validation
and compilation in 11.14 s. A broader packer negative-test run hit an existing
diagnostic-printer assertion on an intentionally unaligned operand; that run
is not counted as passing.

## Kernel fence warning

The original 20-worker memory-pressure incident also warned in
`dma_fence_set_error`: Runtime::pump finished a failed allocation, then
Job::run finished the same packet again. Cancellation could repeat completion
too. Kernel commit `566e77f49635` claims completion once, retains the first
result, and leaves ordering to the actual fence. It builds with Rust 1.98.0.
The underlying machine can still run out of memory under excessive parallel
workloads; no deliberate system-wide OOM was induced to recheck this path.

## Installed versions

Vulkan SHA256: `7dfcc35ce402c8727abaad3f0c1346fa2df456fa64bb0e2168acf5565bcd597d`.
Gallium SHA256: `c0f940f82686992079b651f0644de8d38ee75b8535a2a6dae8131b5a0a94a6b3`.
Kernel module SHA256: `5652210b1726e0629a0a8ccdef4f7c04b1899aa26b20fce845c945c54a985d84`.
Libraries were installed atomically with retained backups; the module was
installed for the next boot and included in the active initramfs. Autologin
and normal display-manager boot remain configured.

Mesa commits: `a994488ea4c` spill allocator, `6e25251faf5` sin/cos,
`a2d5c6fa8ff` shadow gradients, `c7de7ddb183` Zink queue retention.

## Evidence

Each `runs/` directory retains its summary, compressed per-case records and
available guard/binary/environment manifests. Complete logs and shader captures
remain under workspace `_provenance/experiments/` at the paths in those records.
Private compositor overrides are explicit and retained. Ordinary `--env`
configures CTS; `--headless-env` configures only its private compositor.

## Installed-build validation after reboot

Boot `c1d4e2ae-a093-41cb-b7b1-bccb8f6dcf36`, same kernel release with the new
module/initramfs. SDDM autologin and the desktop start normally; Hyprland maps
the updated Vulkan library.

All eight actual driver cases pass with the original CTS executable and the
installed libraries after reboot (44.781 s, guard pass, peak 68.5 C).
The full 91-case WSI sequence also passes without overrides on the installed
build: 1 Pass, 90 NotSupported, zero errors, clean compositor teardown
(12.261 s, guard pass, peak 67.25 C).

Buoy renders and animates normally at 2560x1664 on the installed build after
reboot. Normal Chromium launch, 30-second bounded run, visible page; WebGL 2
reports the Apple M4 Zink renderer, no context loss and GL error zero. The
retained screenshot was inspected and shows the buoy/ocean scene.

Expensive fragment p95 is **92.403 ms**, versus **92.27 ms** in the preceding
30-second optimized-build measurement (and 92.291 ms immediately before these
fixes). The difference is about 0.14%; this run shows no material performance
regression. After the first warm-up interval (3.99 FPS), intervals measure
9.18, 9.37, 9.16 and 9.16 FPS, comparable with the prior 9.18–9.38 FPS.
This remains roughly 9 FPS, not 60 FPS. Peak temperature 81.75 C; guard pass,
no new GPU fault or reset. Evidence:
`20260917T205506164515Z-buoy-random10k-fixes-installed`.
