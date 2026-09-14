# Historical Vulkan CTS sample

`cts-sample.py select` now defaults to Linux cases with reviewed Vulkan 1.0
requirements, labeled **required** or **advertised**. It shuffles uniformly
within that eligible pool, with a recorded seed. It never reads previous results.

- `required`: all prerequisites are unconditional Vulkan 1.0 requirements.
  Missing advertised mandatory support does **not** remove the test.
- `advertised`: optional prerequisites are present in a saved device profile.
  Mandatory prerequisites still apply. This label is not proof the test passes.
- `not-advertised`: a mapped optional prerequisite is absent.
- `unclassified`: the case requirements have not been reviewed. Excluded and
  reported separately; this does **not** mean optional, unsupported, or invalid.

The initial catalog covers core information/format queries, vertex/fragment
GLSL operators/matrices/loops, tessellation invariance, all-format color blits,
and uncompressed 2D sampling/image-view cases. It is deliberately incomplete:
the September 14 profile gives 33,242 eligible cases (13,613 required and
19,629 advertised), 6,669 missing optional support, 186,351 unclassified and
7,533 platform exclusions. This is **not a complete required-test inventory**.
YCbCr, sparse execution, multisample combinations and other unmapped families
need explicit requirement rules before entering this filtered pool. Query tests
can be mandatory even when the feature they query is optional.

Refresh the profile after a driver change. Capture `vulkaninfo --json=0 -o FILE`
with the production ICD inside `gpu-experiment.py`; retain its `target-before.json`.
The following preparation and selection are offline and execute no GPU tests:

```
python3 tools/cts-sample.py profile \
  --vulkaninfo EVIDENCE/remote/asahi-profile.json \
  --identity EVIDENCE/target-before.json --output build/cts-device-profile.json
python3 tools/cts-sample.py select \
  --mustpass reference/vk-gl-cts-1.0.2.6/external/vulkancts/mustpass/1.0.2/vk-default.txt \
  --profile build/cts-device-profile.json \
  --seed 20260923 --count 1000 --output build/cts-filtered-sample
```

Output includes `cases.txt`, `cases.meta.json`, `case-labels.tsv`, the profile,
rule catalog/source, and `exclusions.jsonl` listing every excluded case and reason.
`selection.json` records hashes, population/label counts and the exact chosen list.
The rules are pinned to the original mustpass hash and reviewed CTS sources;
format minima are transcribed from that release's `vktApiFeatureInfo.cpp`.
Extend `cts_requirements.py` and `cts-requirements-vulkan10.json` as new families
are reviewed. Never infer mandatory status just from a case name, a mustpass
entry, or a prior Pass. General image layout does not mean linear image tiling.
The old CTS destination-linear-filter blit check is a known test defect, not a
Vulkan requirement; selected cases can still expose CTS defects.

Use explicit `--scope all --linux-only` to reproduce historical unfiltered
campaigns. Such cases are labeled `unclassified`, never retroactively required.
Do not redraw after seeing results. Copy `cts-sample.py`, `cases.txt` **and
`cases.meta.json`** to the target, keeping matching stems if renamed. The batch
validates metadata/list hashes and, on M4, the recorded ICD/module/kernel identity.
It retains each label and requirement in per-case/campaign results. Bare lists
remain usable for targeted reproductions but are labeled `unclassified`.

`batch` requires the existing
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

For a historical unfiltered Linux campaign, `select --scope all --linux-only`
excludes Windows handles/WSI and Android cases before drawing the sample.
The default selection instead uses the capability rules described above.
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

## Bulk execution with deqp-runner (default for throughput measurements)

Use `cts-bulk.py` for a frozen large list. Keep `cts-campaign.py` / the isolated
backend for one-case diagnosis. The bulk tool stages the list and metadata,
then runs one bounded `gpu-experiment.py` lease with the existing exclusive
60-second watchdog and six-channel thermal guard. For longer runs the existing
guard renews only from fresh GPU retirement evidence plus healthy thermals.
It does not add a timer-only watchdog feeder. Default: 20 workers, 50 tests per
process, 20-second per-case timeout, 90-second workload / 110-second outer bound.
The guard remains able to terminate the whole process group.

The source is pinned at deqp-runner `495e9969446ee34e5573828b0aafaf4d8f028d2d`
(version 0.23.3). `deqp-runner-first-attempt.patch` adds two explicit options:

- `--first-attempt`: disable automatic failure/flake retries, preserve grouped
  QPA/stdout/stderr/caselists, and stop scheduling on crash/timeout/parser failure.
  No expected-failure baseline, skips or flakes list is passed. GPU faults are
  additionally monitored by the outer guard; pending work may be interrupted.
- `--no-deqp-shadercache`: omit newer CTS shader-cache command-line arguments
  absent in CTS 1.0.2.6. This does not disable Mesa's shader cache or change tests.

The upstream defaults are otherwise unchanged. This is a retained local tooling
patch, not a shader or driver workaround. Build on the host (no M4 stress):

```
git clone https://gitlab.freedesktop.org/mesa/deqp-runner.git reference/deqp-runner
git -C reference/deqp-runner checkout 495e9969446ee34e5573828b0aafaf4d8f028d2d
git -C reference/deqp-runner apply --unidiff-zero ../../tools/deqp-runner-first-attempt.patch
rustup target add aarch64-unknown-linux-gnu
CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc \
CC_aarch64_unknown_linux_gnu=aarch64-linux-gnu-gcc \
cargo build --manifest-path reference/deqp-runner/Cargo.toml --locked --release \
  --target aarch64-unknown-linux-gnu --bin deqp-runner
python3 tools/cts-bulk.py --name cts-filtered-bulk \
  --cases build/cts-filtered-sample/cases.txt \
  --runner reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner \
  --binary /root/cts-1.0.2.6/deqp-vk-scaled-precision \
  --output build/cts-filtered-bulk
```

The selected set stays frozen; deqp-runner deterministically regroups/sorts
execution within it. Batch size changes scheduling and driver lifetime, so new
state-lifetime failures are possible. A group completion drives runner progress
counts; a displayed zero does not mean its CTS processes have executed no tests.
The wrapper saves exact runner/CTS hashes, ICD environment, metadata, command,
all QPA and worker logs. Reports preserve raw QPA status/message alongside the
runner's coarser status. `Skip` is never relabeled Pass. Cases with a stdout/QPA start but no
completed result are Interrupted; only never-started cases are Unrun. Workers
killed with SIGKILL during a coordinated runner stop are Interrupted rather
than independent crashes. Raw runner status and worker exit remain recorded. After an interrupted guard, the host recovers completed QPA entries using
`cts_deqp_backend.collect`, retaining the failed guard verdict. No automatic
retry or reboot is performed by this tool.

`summary.json` separates runner wall time, guard/transport wall time and staging
time. Do not equate a completed guard with all tests passing. Current default
binary is the explicitly patched CTS diagnostic build; results are not an
unmodified Khronos conformance submission. Use `--binary` to select another
exact build and retain its identity.

`cts-bulk-report.py --cases ORIGINAL_CASES --output NEW_REPORT SEGMENT...`
combines manually inspected continuations offline. It re-reads retained QPA and
worker logs, rejects changed software identities and retries of completed
outcomes, and preserves every interrupted attempt. It emits a compressed case
list and JSONL attempt ledger, failure TSV, counts/rates, active execution and
campaign time, and wall span including inspection pauses. First-attempt counts
remain separate from final outcomes after explicitly resumed interruptions.

## Seed20260924 corrections

See [the 10k failure fixes](cts-seed24-fixes/README.md) for all 44 original
nonpasses, CTS corrections, the 30.17s -> 10.87s CPU comparison optimization,
M4 multisample exports and Zink integer resolves. Bulk/campaign defaults use
the explicitly adapted `deqp-vk-seed24-corrected` binary. Original reports and
binaries remain unchanged; the bulk per-case timeout is now 60s.

## Vulkan 1.4 CTS

`cts-mustpass.py MUSTPASS --output NEW_DIRECTORY` expands the modern split
mustpass files recursively in their original order, rejects duplicate names,
cycles and includes outside the root directory, and saves every input hash.
It is an offline list operation and applies no capability or result filtering.

The separate `reference/vk-gl-cts-1.4.6.2` checkout pins the official
`vulkan-cts-1.4.6.2` tag (`f6a29701220f34dd1407513bfe80d74ca7b392ce`).
Fetch its dependencies with `python3 external/fetch_sources.py`. Configure
`build/cts-1.4-arm64` using the same ARM64 toolchain as above, Release,
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDE_CPU=DE_CPU_ARM_64` and
`-DSELECTED_BUILD_TARGETS=deqp-vk -DWAYLAND_SCANNER=/usr/bin/wayland-scanner`; build `ninja -j14 deqp-vk`.
The 1.0 diagnostic patches are not applied to this checkout.

```
python3 tools/cts-mustpass.py \
  reference/vk-gl-cts-1.4.6.2/external/vulkancts/mustpass/main/vk-default.txt \
  --output build/cts-1.4-baseline/mustpass
python3 tools/cts-sample.py select --scope all --linux-only \
  --mustpass build/cts-1.4-baseline/mustpass/mustpass.txt \
  --seed 20260925 --count 1000 --output build/cts-vulkan14-seed20260925-1000
```

The reviewed 1.0 requirement catalog cannot label the new list. This explicit
unfiltered mode labels requirements **unclassified**, skips Windows/Android
platform entries before selection, and retains CTS NotSupported separately.
It is a broad smoke test, not a required/advertised or conformance claim.
Do not replace skipped/failed cases after seeing their outcomes.
Use `cts-bulk.py` with the separately installed 1.4 `--binary` and its matching
`vulkan/` assets, the existing patched deqp-runner, 20 workers and the mandatory
watchdog/thermal guard. Retain exact executable hashes and original QPA results.

`cts-sample.py batch` discovers a live Wayland socket, preferring user 1000,
and falls back to SDDM's compositor before login. It records the actual runtime
directory and Xwayland display/authentication path when available. This avoids
window-system setup failures caused by assuming a logged-in user session.

## Reviewing skips in modern CTS

`cts-skip-audit.py --results RESULTS --profile PROFILE --registry vk.xml
--output NEW_DIR [--review REVIEW.json]` operates offline. It retains raw CTS
statuses, inventories unconditional core feature requirements (including the
promoted aggregate feature structs), and groups unreviewed skip reasons. Supply
explicit reviews keyed by exact message and a case regex, with a source-based
`basis` and `classification` of `NotSupported` or
`NotSupportedButShouldBeSupported`. Unmatched skips remain
`NotSupportedNeedsReview`; overlapping reviews are rejected. A recorded gate
can violate core requirements even if later optional prerequisites also exist.
Keep that distinction in reports. The registry inventory does not cover every
conditional feature, limit or format rule; it is an audit aid.

`x11-local-access.py allow|revoke --display :0 --user root` is the narrow
`xhost +/-SI:localuser:root` operation using libX11. Run it as the existing
compositor owner when tests need access to that Xwayland server. It neither
disables access control nor grants remote access. Record the change and revoke
it when finished. Graphics execution still requires the usual watchdog/thermal
guard; granting access alone is not a render test.

See [the Vulkan 1.4 sample](cts-vulkan14-seed20260925-1000/README.md) for exact
build, selection, raw and audited outcomes, timings and reproduction artifacts.
