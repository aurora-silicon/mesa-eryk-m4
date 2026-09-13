# Historical Vulkan CTS sample

`cts-sample.py select` draws unique cases uniformly from a release's must-pass
list. It saves the seed, population hash, exact case list and category counts.
Do not redraw the sample after seeing results.

```
python3 tools/cts-sample.py select \
  --mustpass reference/vk-gl-cts-1.0.2.6/external/vulkancts/mustpass/1.0.2/vk-default.txt \
  --seed 20260913 --count 100 --output build/cts-1.0-baseline/sample
```

Copy the script and `cases.txt` to the target. `batch` requires the existing
60-second watchdog and fresh six-channel thermal guard. It runs each named
case in a separate process, retaining QPA, stdout, command, status, driver
environment, executable hash and failed compiler bundles. It starts new cases
for at most 10 seconds and by default bounds each case at 20 seconds, leaving time inside
a 40-second guard for evidence collection. A timeout is an inconclusive timed
out test, never a correctness failure or pass. Crashes/device loss stop the
batch; inspect/recover before continuing at its recorded next index.

```
python3 tools/gpu-experiment.py --name cts-1-0-sample-000 \
  --hypothesis 'Measure the fixed Vulkan 1.0 CTS sample on native M4 Vulkan' \
  --expect '^CTS_BATCH_COMPLETE:' --seconds 40 -- \
  python3 /root/cts-sample.py batch --guard-output '{output}' \
  --binary /root/cts-1.0.2.6/deqp-vk --cases /root/cts-1.0.2.6/cases.txt --start 0
```

The outer runner validates guard/process evidence; **CTS case results are in
`remote/cts/results.json`**, not the runner's `pass` verdict. `NotSupported`
means skipped capability coverage. A random 100-case sample is not conformance.
Keep per-case logs to reproduce a failure by name without GUI capture/navigation.
The driver environment matches the installed native M4 Vulkan configuration,
including its existing `ASAHI_MESA_DEBUG=nocompress` setting. No test expectations
or capability requirements are changed.

## Pinned 1.0 build

The baseline checkout is `reference/vk-gl-cts-1.0.2.6`, tag
`vulkan-cts-1.0.2.6`, commit `6b3bca439862e986409ffd76e857ada0cc7c9080`.
Dependencies are the exact revisions/checksums from `external/fetch_sources.py`.
The old zlib URL is now `https://zlib.net/fossils/zlib-1.2.11.tar.gz`; libpng is
`https://downloads.sourceforge.net/project/libpng/libpng16/older-releases/1.6.27/libpng-1.6.27.tar.gz`.
Both archives matched the original SHA256. Git dependencies were fetched by
commit with depth 1. Python 2 comes from the official PyPy
`https://downloads.python.org/pypy/pypy2.7-v7.3.20-linux64.tar.bz2` archive.
Build logs and patch records are under `build/cts-1.0-baseline/`.

```
cmake -S reference/vk-gl-cts-1.0.2.6 -B build/cts-1.0-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/home/council/dev/dirtyroom_m4/build/blender-m4-toolchain.cmake \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release \
  -DDE_CPU=DE_CPU_ARM_64 -DCMAKE_CXX_STANDARD=11 -DCMAKE_C_STANDARD=99 \
  -DPYTHON_EXECUTABLE=/home/council/dev/dirtyroom_m4/build/cts-1.0-baseline/pypy2.7-v7.3.20-linux64/bin/pypy
ninja -C build/cts-1.0-arm64 -j16 deqp-vk
```

The pinned glslang contains an unused `pool_allocator::setAllocator` definition
that attempts to assign a noncopyable allocator through a reference. Modern
Clang rejects it. Remove that unused method; no call sites exist in the pinned
source. Retain the dependency patch with the baseline. CTS test code remains
unchanged.

The pinned SPIRV-Tools also needs `#include <limits>` in
`source/validate_cfg.cpp`: it uses `std::numeric_limits` without including its
header. This is the second dependency-only build fix.

The default CTS CMake configuration selected the sysroot's shared libpng and
zlib; the downloaded historical image-library archives were not linked. The
shader compiler/validator use the pinned, locally built glslang and SPIRV-Tools.
Dependency patches use zero context; apply with `git apply --unidiff-zero` in
the corresponding dependency checkout.

For targeted diagnosis, `batch --start N --count 1 --log-sources` retains the
case's generated shader sources. `--renderer lavapipe` uses the system software
Vulkan ICD explicitly; use a separate experiment name and never merge it into
native M4 pass counts.

Use `--log-images` for a pixel mismatch to retain CTS reference/result images
in the QPA. Leave it disabled for routine compilation and passing-case checks.

`--capture-success` saves successful compiler inputs as well as refusals and
disables the disk shader cache for that run. Use it only for a selected pixel
failure; replay the retained bundles offline with `compiler-replay.py`.

`cts-images.py result.qpa --output NEW_DIRECTORY` extracts exact embedded PNGs,
source/image hashes, unique-color counts and unequal result/reference pixels.
It needs Pillow and runs offline. These byte comparisons are diagnostics; the
CTS verdict retains its format-specific tolerance and remains authoritative.

`cts-campaign.py` repeats the existing bounded batches until the requested
consecutive case range is recorded. Each batch retains its own full guard and
thermal evidence. It verifies continuity and stable boot/library/binary hashes,
stops on timeout/device loss/guard errors, and never retries or reboots hardware.
Process crashes stop by default. For a fixed baseline containing a known CTS
process abort, `--continue-userspace-crashes` permits the next case only after
successful guard completion; the abort stays in the result count.

```
python3 tools/cts-campaign.py --name cts-fixed-100 \
  --output build/cts-fixes/fixed-100 --count 100 --continue-userspace-crashes
```

Cases and binaries must already be installed; `--cases` and `--binary` specify
target paths. `results.json` preserves every original CTS outcome;
`summary.json` records batch evidence paths and identity. This is a smoke-test
campaign, not a conformance result.

`cts-patched-build.py PATCH --output NEW_DIRECTORY` builds a separately named
CTS diagnostic executable from a trusted patch. It saves the patch and hashes,
then restores the original source files and original executable on success or
build failure. Do not run a concurrent CTS build. It performs no hardware test;
install its `deqp-vk` under a distinct target name and use the usual guarded
runner. Keep adapted outcomes separate from the original release results.

CTS batches enable `HK_DEBUG_LOG=1` so command-recording errors include the
Mesa source file and line instead of appearing only at vkEndCommandBuffer.

`crash-backtrace.c` is an opt-in native crash-stack preload for a target whose
GDB ptrace path fails (the J713 currently rejects SVE vector-length queries).
Build with `cc -shared -fPIC -O2 tools/crash-backtrace.c -o crash-backtrace.so`;
set `LD_PRELOAD` inside a bounded guarded experiment. It logs a stack on SIGSEGV
or SIGABRT and re-raises the original signal, preserving failure status.
Symbolize offsets using `addr2line -f -C -e EXACT_LIBRARY OFFSET...`. Retain the
matching binary/hash. Stack unwinding is not async-signal-safe, so the external
timeout/watchdog remains mandatory. Do not install this into normal sessions.

`native-metal-compile.py --stage render --rasterization-disabled` compiles a
vertex-only pipeline, including void vertex functions, and preserves the
pipeline configuration and native archive in its output directory.

`vulkan-attachment-probe.c` checks packed BGR8_UINT attachment clear, reload,
and overlapping clear-draw rectangles with an exact 144-pixel CPU oracle,
using mip 1 and array layer 1. Build with `cc -O2 -Wall -Wextra SOURCE -lvulkan`
and execute only under `gpu-experiment.py` with the intended Vulkan ICD.
It requires the advertised color-attachment feature; it does not skip it.
Pass `--rgb9e5` to exercise shared-exponent RGB9E5 instead, checking exact packed
32-bit words for the same clear/reload/overlap regions. Success markers are
`BGR_ATTACHMENT_RESULT: PASS` and `RGB9E5_ATTACHMENT_RESULT: PASS` respectively.

`vulkan-queue-probe.c` requires eight Vulkan queues and enabled timeline
semaphores. It submits four waiting consumers before any producer, then swaps
queue roles for a second round and checks all copied words. Binary semaphores
cannot be used for this ordering. Build with `cc -O2 -Wall -Wextra SOURCE -lvulkan`
and run only under `gpu-experiment.py`; success is `QUEUE_DEPENDENCY_RESULT: PASS`.

For a Linux-only random campaign, `select --linux-only` excludes Windows
handle/WSI cases (`win32`) and Android WSI before drawing the sample. It records
every platform exclusion and the eligible population. It does not exclude
optional features, known defects, prior failures, or already tested cases.
Use a new recorded seed and freeze the case list before executing any case.

`cts-report.py --cases CASES --output REPORT SEGMENT...` combines first-attempt
results from multiple guarded campaign segments. It rejects duplicate attempts,
case/index mismatches and changed software identities, retains Unrun entries,
and emits counts, rates, category counts, temperatures and evidence links.
Boot IDs may differ after explicitly inspected recovery. It never runs tests.
The campaign tool preserves completed case results before stopping on a GPU
fault; the fault remains in the summary and still requires recovery before
starting another segment.

## Parallel campaigns and timing

`cts-campaign.py --jobs 4` schedules up to four independent CTS processes inside
one watchdog/thermal lease. `cts-sample.py batch --jobs 4` exposes the same mode
for a single batch. The default remains one process. Logs, QPA and process IDs
are separate per case; results remain sorted in frozen case-list order even
when completion order differs. A fatal case stops new launches and drains
already-running bounded cases before campaign recovery checks. An outer guard
trip terminates the shared process group. No worker owns or renews the watchdog.

The 10-second launch window remains fixed; the default per-case bound is 20 seconds.
See `--case-timeout` below for a recorded alternative for long presentation tests.
Each batch saves `cts/timing.json` with requested/peak concurrency, timestamps
and execution wall time. Campaign `summary.json` records total elapsed wall time
(including SSH, guard setup and evidence fetch) and summed batch execution wall
time. Per-case timestamps provide overlap evidence. These are warm/cold-cache
mixed campaign timings unless the shader-cache policy is separately controlled;
do not compare them as a compiler-only benchmark.

Example, using an already deployed frozen sample and identified CTS binary:

```
python3 tools/cts-campaign.py --name cts-parallel-1000 --jobs 4 --count 1000 \
  --cases /root/cts-1.0.2.6/random-1000-frozen.txt \
  --binary /root/cts-1.0.2.6/deqp-vk-input-aspects-tess-chroma \
  --output build/cts-parallel-1000
```

`cts-campaign.py --case-timeout 30` allows up to 30 seconds per case and uses
50-second outer experiments (10-second launch window plus 10-second evidence
margin). The default remains 20/40 seconds. `cts-sample.py batch` accepts the
same option; direct users must provide a sufficiently long outer guard. Bounds
are recorded in batch timing and campaign summary. Retain timeouts separately when comparing runs with different bounds.
For seed 20260920, increasing this bound did not fix the presentation stall: a
latched DCP failure was responsible, as established by the display status and
kernel journal. This does not
change CTS commands, frame counts, output checks, watchdog or thermal limits.

Before a batch containing WSI cases, `cts-sample.py` checks every native DCP
`m4_status` and refuses a latched failed-flip state. GPU retirement alone does
not establish a working display. The guard already records that state in
`target-before.json`; retain it and recover before rerunning presentation.
The historical CTS xdg-shell backport also needs an application event pump,
since its long Vulkan render loop does not call the platform event dispatcher.
