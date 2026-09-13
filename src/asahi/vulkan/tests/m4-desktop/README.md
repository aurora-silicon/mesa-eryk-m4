# Development tools

Shared tools for M4 GPU development and debugging live here. Add or improve a
tool when real development work calls for it; the proposed tool suite is not a
requirement to build everything upfront.

Prefer reusable helpers over repeated manual commands and one-off script copies.
Document purpose and usage as tools are added, and keep generated captures,
builds, and experiment results outside the tool sources.

The development workflow and hardware experiment requirements are in
[AGENTS.md](../AGENTS.md).

Existing tools include `g16-isa/`; preserve them when extending this directory.

## Default Vulkan capture/replay workflow

Reuse captures and small deterministic replays before launching games or
reviewing desktop screenshots again. See [Vulkan trace tools](README-vulkan-traces.md)
for `vulkan-trace.py`, capability matching, recorded adaptations, saved-output
comparisons, build/deployment details and known tooling limitations. All target
runs retain the mandatory watchdog and thermal supervision.

## Persistent SSH watchdog

See [SSH watchdog](README-watchdog.md) for the host heartbeat and target broker.
It retains an armed 60-second watchdog between experiments and gives each test
an exclusive descriptor lease without bypassing thermal or progress checks.

## Bounded GPU experiments

Normal boot starts the display manager. Stop SDDM before guarded experiments
take over the seat; do not change the default boot to text mode.
After the experiment releases its watchdog and the driver is healthy, deploy
`restore-display-manager.py` to the target and run it with `--output NEW_DIRECTORY`.
It arms a 60-second watchdog, monitors temperatures during a short SDDM startup
check, saves evidence, then leaves the ordinary login screen running. A failed
startup leaves watchdog recovery armed. It does not install GPU/display modules.

`desktop-experiment.py --name NAME --seconds 180` runs the existing target desktop
guard for a shadowed 0 A.D. map under native Hyprland, saving source state, target
hashes, health/thermal evidence and screenshots. It verifies the guard hash and
does not nest watchdog owners. Existing uncompressed assets, disabled postprocessing
and per-draw synchronization are explicit in its manifest; `--batch-draws` removes
the synchronization diagnostic. `--postprocessing` removes the forced postprocessing
disable, and `--compressed-assets` removes the S3TC extension override. These
options test one remaining limitation at a time; they do not declare support.
Guard success still requires visual validation.

`gpu-experiment.py` reports compact pixel checks and per-channel temperature peaks
from fetched evidence, alongside the independently validated guard verdict.
It runs one experiment on `root@omarchy-m4` through the existing
mandatory watchdog/thermal guard. It saves source revisions and patches, target
hashes, the guard log, fetched evidence and a classified result under
`_provenance/experiments/`. It does not build or deploy binaries. The recorded
installed hash identifies the binary actually tested independently of source HEAD.

```sh
python3 tools/gpu-experiment.py --name lod-only --seconds 40 \
  --hypothesis 'Does the conditional LOD sample stall without preceding draws?' \
  --expect '^PASS: conditional mipmaps' \
  -- python3 /root/probe-conditional-mipmap-large-lod.py
```

`{output}` in an argument expands to the remote evidence directory. The success
regex must name the workload's real validation result; exit zero alone is not
accepted. Fault classification filters kernel timestamps to this experiment,
and the original first readback remains part of validation. The initial tool
supports bounded runs up to 1800 seconds. Runs over 40 seconds require the target
guard to match the workspace helper by SHA-256. The guard renews the 60-second
watchdog only after a fresh, increasing GPU retirement and fresh thermal evidence;
a stalled GPU or thermal monitor cannot keep it alive. Use a workload timeout
shorter than the outer bound so evidence can be saved before cleanup.

`run-m4-gl.py` supplies the private M4 Zink/EGL environment for small GL probes.
Copy it and the probe binary to `/root/`, then invoke it through the guarded
runner; never run it directly on the target outside the guard. It rejects the
lab module and leaves renderer verification and pixel checks to the probe.

```sh
python3 tools/gpu-experiment.py --name mipmap-readbacks \
  --hypothesis 'Does the same draw survive two readbacks?' \
  --expect '^PASS: conditional mipmaps' -- env \
  M4_TEST_WIDTH=1024 M4_TEST_MODES=3 M4_TEST_READS=2 \
  python3 /root/run-m4-gl.py /root/m4-conditional-mipmap-parameters
```

The target wrapper also writes paired `CLOCK_MONOTONIC` markers to stdout and
`/dev/kmsg`. Use these to correlate fence/readback timing with kernel retirement;
the raw clock domains have a boot-dependent offset. The wrapper can also run
small Vulkan probes using the same private ICD; the extra GL settings are unused.

The manifest also hashes the installed Asahi kernel module. When paired
markers are available, kernel fault classification starts at the matching
start marker rather than comparing unrelated uptime and kmsg clocks.

## Inspecting GPU memory after a fault

`image-compare.py REFERENCE ACTUAL --output NEW_DIRECTORY` compares saved PNGs
or raw `.rgba` GL readbacks (`--raw-size WIDTH HEIGHT`). It records hashes and
pixel errors plus a reference/actual/8x-error montage. `--threshold` defaults to
8 channel levels and is a reporting threshold, not proof of correct rendering.
Raw GL rows are flipped for display. The tool runs offline and requires NumPy
and Pillow; it never launches a renderer or changes input captures.

`gpu-bo-summary.py` reports SHA-256, a little-endian word histogram, and optional
expected-word mismatch counts for raw BO snapshots. With `--expected-word`, it
also reports contiguous mismatch lengths and their distribution across
`--regions` equal-sized regions (default 16), useful for distinguishing address
patterns in cold readbacks. It does not guess image
tiling. Constant-color probes can distinguish completed pixels from untouched
storage without using a GL readback after device loss:

```sh
python3 tools/gpu-bo-summary.py /path/to/bo-*.bin --expected-word 0xffff0000
```

The local Mesa diagnostic uses `HK_M4_FAULT_CAPTURE_DIR={output}` and
`HK_M4_FAULT_BO_BYTES=4194304` with the guarded runner. On the first M4 VM
failure in that process it snapshots live BOs of exactly the requested size.
Files carry their handle and GPU VA; the command log records labels and write
sizes. These are fault snapshots, not API-synchronized readbacks. A partial
snapshot must not be interpreted as successful rendering.
`HK_M4_FAULT_REPEAT_MS=200` requests a second fault snapshot to inspect
whether memory changes after the first. This delay is solely a diagnostic;
both snapshots still belong to a failed workload.

## Small firmware disassembly slices

`arm64-slice.py` prints a bounded ARM64 instruction range, or finds textual
instruction operands with surrounding context. It requires Capstone; the
existing `build/display-venv/bin/python` supplies it. Example:

```sh
build/display-venv/bin/python tools/arm64-slice.py firmware-text.bin \
  --offset 0x9c00 --length 0xb0
```

`--find '#0xe48' --limit 4` searches the image. Search hits are operand
matches, not resolved references; verify register bases before attributing a
read or write to a firmware object. Default addresses are image-relative.

## Installing a kernel module for the next boot

`install-asahi-module.py PATH --label LABEL` stages the full module, verifies
its SHA-256, module name and target kernel release, refuses active GPU clients,
preserves the previous file by its hash, atomically installs and runs depmod.
It does not hot-reload or reboot. Follow with a reboot through the guarded
experiment runner, allow boot completion, then test. The installed module hash
only identifies the loaded code after that reboot.

## Selecting firmware trace events

`gpu-trace-summary.py LOG --after-last TEXT --ids 0x4,0x5,0x14,0x35`
selects draw-scoped firmware events into JSON. It retains firmware ticks when
present and reports tick deltas without guessing their frequency. Kernel log
seconds are drain times, not firmware execution timestamps. It reports logging
suppression notices and excludes repeated recent-trace snapshots. Missing
requested start markers are errors rather than silently selecting a whole boot.

`guarded-command.py` is the target-side bounded watchdog/thermal command guard,
used by `gpu-experiment.py` at `/root/guarded-command.py`. It saves a bounded
current-boot kernel journal before disarming so verbose firmware logging cannot
silently evict startup evidence from the saved dmesg ring. Its target dependencies
remain the qualified watchdog and thermal modules under `/root/g16-submit-test`.
When a sealed M4 firmware coredump is reported, the guard copies it through
`g16-fault-dump.py` before fault recovery (five-second bound, no watchdog renewal).
This covers long replay wrappers that do not use `run-m4-gl.py`.

## Native M4 shader compiler

`native-metal-compile.py SOURCE.metal --output NEW_DIRECTORY --function cs`
uses the authorized macOS compiler host to compile a Metal compute kernel for
`applegpu_g16g`, without executing it. It retains the source, pipeline definition,
compiler version and command results, metallib/archive hashes, and checked
Mach-O text sections. The pipeline filename must use `.mtlp-json`; `.json`
is interpreted as a binary flatbuffer by this compiler.

For a render pipeline, use `--stage render --function fs --vertex-function vs`.
The source must provide both functions; the current comparison contract is a
single RGBA8Unorm attachment. This still only compiles offline. Both extracted
text sections and symbol offsets are retained; do not identify a section's stage
from its filename. Use `_agc.main` rather than assuming offset 64: the compiler
can hoist work into a constant program and move the main entry.

## Direct firmware address references

`build/display-venv/bin/python tools/arm64-xrefs.py TEXT.bin ADDRESS --size 8`
finds candidate memory references using straight-line ADR/ADRP/ADD/MOV constant
tracking. It clears tracked values at branches and caller-clobbered registers
at calls. This intentionally misses indirect and cross-block references; it is
not a control-flow-aware disassembler. Confirm surrounding instructions before
assigning a firmware field's meaning.

The long-command guard uses the kernel's explicit `monotonic_ns` retirement
field when present to establish fresh progress. Kernel printk clocks on this
port can differ from userspace CLOCK_MONOTONIC even within one boot. Thermal
liveness and increasing job IDs remain mandatory for watchdog renewal.

For structure fields, `arm64-xrefs.py IMAGE OFFSET --size SIZE --pointer-cell
ADDRESS [--pointer-offset OFFSET ...]` follows a known pointer cell and optional
nested pointers. For example, firmware cell `0x192758` then offset `0x469`
reaches PowerPerformance. Results remain straight-line candidates: paths through
function arguments, branches, and indexed accesses need separate inspection.

Native macOS counterparts and bounded exact-build capture tools: [macos-native/README.md](macos-native/README.md).

`render-state-extract.py KERNEL_LOG NEW_OUTPUT_DIRECTORY` reconstructs the last
fault's complete retained M4 TA/fragment commands and firmware sequences.
It refuses missing, duplicate, overlapping or out-of-bounds data. Its output
includes hashes and can be passed to `macos-native/registers.py` for a native
register comparison. These are sequential fault-time memory reads, not an
atomic snapshot, and no status MMIO is acknowledged by the collector.

## Offline native UAT translation

`native-uat-read.py STAGE CONTEXT ADDRESS --translate-only` walks saved page
tables from one macOS capture stage. Use `firmware` as CONTEXT for the independent
firmware global root; use a numeric context for a GPU address. Without
`--translate-only`, `--length` reads retained physical payload pages. Missing
tables or payloads are errors: the tool never borrows another stage or accesses
hardware. Output includes page-table entries and source hashes. Captures are
sequential observations, not atomic snapshots.

```sh
python3 tools/native-uat-read.py /path/to/capture/executing firmware \
  0xfffffc2000400000 --translate-only
```

## VDM batching diagnostics

`vdm-trace-extract.py COMMAND.log NEW_DIRECTORY` extracts complete `vdm-stream`
hex dumps from `HK_G17P_LAUNCH_TRACE=1`, saves exact bytes and hashes, and builds
`vdm-decode.c` against the local Mesa strict G16G decoder. It reports packet
boundaries, draw counts, barriers and links. It rejects interrupted/truncated
dumps; a partial decode is recorded explicitly. Links are reported, not followed.
Packet recognition does not prove barrier semantics or resource ordering.

The Mesa `m4_gl_conditional_mipmap.c` probe accepts `M4_TEST_DRAWS=4` and
`M4_TEST_SPLIT_DRAWS=0|1` for a paired scissor/texture descriptor test (modes 0/2).
Each stripe alternates between a blue mip and a grey texture. All draws remain
observable. The two variants differ only by `glFinish` after each draw; inspect
actual streams before calling a variant batched. The target wrapper defaults to
`ZINK_DEBUG=sync`, which itself inserts barriers and splits rendering. Use an
explicit empty `ZINK_DEBUG=` for this comparison. Run only through the mandatory
watchdog/thermal guard. The llvmpipe control requires `M4_TEST_LLVMPIPE=1`.

`HK_M4_VDM_DRAW_BARRIER=1` is a local Mesa diagnostic: on G16G it inserts the
native-witnessed type-1 packet before each subsequent draw's state in a VDM
stream. It does not split the incomplete native bind tail. It is off by default
and is not an established fix or justification for permanent per-draw barriers.


Experiment evidence fetches retain interrupted files in `.rsync-partial` and
resume them on retry. Per-attempt transport bounds and verdict requirements
remain unchanged; partial fetches never count as complete evidence.


`verify-render-timestamps.py NATIVE_TA_SKU NATIVE_FRAGMENT_SKU` compiles the
current kernel wire encoder offline and compares each complete timestamp pair,
WFI, and restart displacement against the supplied M4 native records. It derives
only command/queue/UUID identities from those records and reports source hashes.
It does not compare the whole command or access hardware.

`render-state-extract.py` reads sequence lengths from the complete retained run
commands, supporting both the original and timestamp-bearing render sequences.
It still rejects holes, duplicates and out-of-range data.

The native M4 collector also supports finite repeated fragment captures without
its former compute dwell, and read-only firmware ktrace-ring capture. See
[macos-native/README.md](macos-native/README.md) for `--render-active`, the offline
`ktrace.py` decoder, validation and concurrent-snapshot interpretation limits.


Native firmware diagnostics now include `macos-native/run.py --firmware-trace`
for bounded macOS ktrace collection, `macos-native/trace-render.py` for canonical
SKU/event correlation, and `macos-native/power-select.py` for offline execution
of the exact 25G83 policy-selection arithmetic. See the native README for
identity checks, bounds, examples and limits; Linux watchdog/thermal rules remain.


`uma-state.py NATIVE_TABLE.bin` decodes a retained M4/25G83 8192-byte USC
private-memory hardware table. `--kernel-log KERNEL.log` decodes the existing
Linux fault snapshots instead. It reports ring capacity, both advancing cursors,
their indices and distance, and the total page count. Cursors are not free-page
counts; the tool does not infer allocator health or GPU completion. It works
entirely offline and retains the source hash and log line numbers.


`asm-slice.py SAVED_ASSEMBLY ADDRESS --lines 80` displays a bounded slice of
existing objdump or `arm64-slice.py` output. It prints the containing symbol
once and strips repeated branch-symbol annotations, retaining instruction
operands and addresses. Use it for compact comparisons of the saved native
kext and firmware listings; it does not disassemble or execute anything.
The requested address must exist exactly; output is limited to 1–512 lines.


`firmware-snapshot.py --guard-output DIRECTORY --label after-workload` is a
target-side, read-only snapshot of the exact J713 firmware reservations. It
requires a live ancestor guard owning the watchdog, its verified 60-second
timeout, the current boot ID, and fresh readings from all six thermal channels.
It resolves no-map regions from DT, verifies the same normalized firmware
identity as the kernel, and saves the original text and two sequential data
snapshots with hashes. Normalization changes only a private copy of the two
qualified entropy TLVs. All mappings are read-only; there is no MMIO access.

Set `M4_FIRMWARE_CAPTURE_DIR={output}` in a `gpu-experiment.py` invocation
using `run-m4-gl.py` to capture before and after the workload. Each helper call
is bounded to five seconds inside the existing watchdog/thermal guard. The
workload retains its 30-second bound and the outer experiment its existing bound.
A helper failure is a failure, not a substitute for a successful render.

`g16-fault-dump.py collect --guard-output DIRECTORY --allow-missing` collects
new sealed M4FWD001 device coredumps during that guard. The run wrapper calls
it after the workload when firmware capture is enabled. It leaves the kernel
copy intact and rejects old, truncated, or oversized dumps. It does not bypass
`/dev/mem` restrictions: ordinary driver allocations are copied by their owning
kernel code through devcoredump. The attempted userspace object walker was
removed after it hit those restrictions; its partial evidence is retained.

`g16-fault-dump.py decode DUMP.bin --extract NEW_DIRECTORY` works offline,
validates the sealed archive, extracts named records and emits hashes, firmware
VAs, job ID and observation intervals. No record is an atomic GPU snapshot,
and the archive intentionally excludes MMIO and application buffer contents.

`g16-fault-dump.py decode DUMP.bin --scheduler` summarizes 25G83 queue contexts, proportional scheduling weights and saved active-node cursors, validating associated record pointers. It works on the initial 18-record dump too (without contexts). Fields are sequential observations, not proof of completion.

The scheduler summary also accepts a single shared `queue-context` record and reports runtime priority totals and shifts. Captured counters may precede the next firmware maintenance tick; a nonzero dirty flag alone is not an error.

`g16-fault-dump.py decode DUMP --scheduler` also validates retained render allocator links and reports PB counts/statistics plus the TA peer-fragment event versus the fragment command event. Qualified on job815; zero TA peer fields exposed the missing partial-render dependency. Fault counts are not exact utilization measurements.

`install-mesa-library.py SOURCE --label LABEL --component vulkan|gallium` extends the existing verified Vulkan deploy helper for the installed26.1.4 Gallium library. It waits for staging, verifies full-fileSHA, refuses activeGPU clients, retains a hash-named previous library, fsyncs and atomically replaces it. It only installs a library; execute tests separately with the mandatory watchdog/thermal guards.

## Shader instruction fields

`shader-fields.py BINARY smp pack_smpir --encoding AGX3b --offset 0x80`
scans an explicit shader range for byte-exact composer matches. It reuses the
previous native-trace decoder and leaves that original helper intact. Every
reported candidate must re-encode exactly; matches are not control-flow or
hardware validation. AGX3b is selected explicitly for M4. The older generated
XML disassembler can reject valid native forms, so this scanner uses the full
composer tables. Build its local shared library after table changes:

```sh
cc -shared -fPIC -O2 mesa/m4-asahi/src/asahi/compiler/agx3_compose/agx3_compose.c \
 mesa/m4-asahi/src/asahi/compiler/agx3_compose/agx3b_tables.c \
 mesa/m4-asahi/src/asahi/compiler/agx3_compose/agx3c_tables.c \
 -o build/tools/agx3-compose.so
```

## Raw Vulkan buffer oracle

`vk-buffer-oracle.c` runs a supplied compute SPIR-V module against one storage
buffer at set 0, binding 0. It loads initial and independently generated expected
bytes, checks every 32-bit word, and saves the actual buffer even on a mismatch.
It requires the M4 device, a compute queue, coherent host-visible memory and
`shaderInt64`; it never substitutes a CPU renderer. Its fence wait is bounded to
20 seconds. Compile with the target sysroot and `-ldl`, deploy it to `/root/`,
and **execute only through the mandatory watchdog/thermal guard**:

```sh
python3 tools/gpu-experiment.py --name wide-multiply \
  --hypothesis 'Both halves of signed and unsigned products are correct' \
  --expect 'PASS: buffer oracle bad=0/6144' -- \
  env AGX_MESA_DEBUG= HK_G17P_LAUNCH_TRACE=0 \
  python3 /root/run-m4-gl.py /root/vk-buffer-oracle \
  /root/wide-multiply-cases/shader.spv /root/wide-multiply-cases/input.bin \
  /root/wide-multiply-cases/expected.bin 32 '{output}/actual.bin'
```

The initial case generator and shader live in Mesa's Vulkan tests as
`m4_wide_multiply.py` and `m4_wide_multiply.comp`. The generator creates a new
output directory; compile the shader with `glslangValidator -V -o shader.spv`.
The group count is checked against the device limit; the supplied shader must
respect the supplied buffer size. Evidence `103145538063Z` qualified all 6144
words on M4, including unchanged inputs and both halves of 1024 signed/unsigned
boundary and deterministic random products.

GPU experiment summaries also recognize the mip/BC tests' `pixels, N failures`
output, retaining mismatches independently of the expected success expression.

## Identical-frame llvmpipe reference

`desktop-experiment.py --trace` enables the existing EGL capture while retaining
its watchdog and thermal guard. The trace path is saved in
`remote/graphics-trace.json`; fetch that file separately after the guarded run.
The capture mode also enables the existing shader diagnostics, so it is not a
performance benchmark.

`replay-llvmpipe.py TRACE --output NEW_DIRECTORY --calls CALL1,CALL2` reuses the
qualified host replay setup, forces llvmpipe with four threads, checks its
renderer identity, saves trace SHA-256, logs and PNG snapshots, and rejects
shader/API errors or missing frames. It needs the existing `build/apitrace-host`
binaries, host EGL, `eglinfo` and `Xvfb`; display179 must be free. It never drops
individual draw calls or runs a hardware experiment. Use `image-compare.py` to
compare the corresponding hardware and reference snapshots.

`desktop-experiment.py --cpu-profile` records system-wide software CPU-clock
samples at 99 Hz, with call chains, during the guarded desktop run. It uses
private `/opt/m4-profiling/usr/bin/perf` on the target and saves `perf.data` and
`perf-record.log`. Decode after fetching, with `DEBUGINFOD_URLS=` to avoid
network symbol lookup inside a hardware guard. Add the exact tested module and
Mesa ELF files to `perf buildid-cache`, and pass the matching `--vmlinux` when
reporting kernel symbols. Idle-poll samples are not useful game CPU work;
report both the denominator and relevant process names. The first actual
capture retained 188,630 samples with no lost samples. Its report step timed
out on-target; report generation was moved offline in response.

`perf-summary.py SAVED_PERF_DATA --output NEW_DIRECTORY --vmlinux VMLINUX
--elf TESTED_LIBRARY_OR_MODULE` analyzes an existing capture offline. Repeat
`--elf` for the actual tested artifacts; perf matches symbols by build ID. The
helper disables network debuginfo lookups, saves artifact hashes and recorded
build IDs, and writes flat symbol/process reports plus a compact JSON process
summary. Percentages use all recorded CPU samples, including idle. It was first
used on the saved 0 A.D. profile above; it never accesses or drives the GPU.

`bo-cache-test.py HOST_MESA_BUILD --output NEW_DIRECTORY` compiles the real
internal Mesa BO cache with synthetic BOs and a fake final free callback. It
uses the host build's recorded compiler flags and retains command/build/result
logs. No DRM device is opened. Tests cover incompatible flags including hash
collisions, size/alignment rejection, LRU trimming, shared-object rejection,
and complete eviction. This is not hardware validation.

`desktop-experiment.py --game-profile` enables 0 A.D.'s built-in profiler on
127.0.0.1, disables only its optional GPU timing instrumentation, and captures
its overview and main-thread event stream before guarded
shutdown. It preserves the normal renderer/options and watchdog/thermal rules.
Analyze the saved JSON offline; capture bounds do not limit GPU workloads.

`0ad-profile-summary.py MAIN_JSON --output NEW_JSON [--frames 120]` reports
recent complete-frame rates and inclusive/exclusive main-thread scope times.
Nested inclusive totals overlap, and wall time includes waits; these are not
GPU or CPU utilization percentages. Source hashes and incomplete scope counts
are retained. A synthetic nested-event fixture qualifies the arithmetic;
real-game event captures are needed to establish any performance finding.

The buffer oracle's optional `M4_ORACLE_WATCH_TRACE_US=1..100000` requires
`M4_ORACLE_WATCH_WORD`. After its immediate fence observation it records changes
to that word, stopping at the expected value or the bounded deadline. The full
buffer snapshot then follows this diagnostic interval and is labelled as such.
An incorrect immediate watched value still fails the whole test, even when later
reads match. It adds no GPU submission and implements no driver wait/workaround.
Observation records are bounded and any dropped records are explicitly counted.

`native-uat-read.py STAGE resources GPU_VA --length N` reads a captured base
buffer by the resource manifest's declared GPU VA when physical UAT payload
pages are absent. This explicit mode reports resource/task identity and file
hashes, performs no UAT walk, rejects views/ambiguous matches/truncated ranges,
and never mixes capture stages. The ordinary numeric-context and `firmware`
modes still require physical translation. Qualified on native061054 completed
resource27: exact48-byte CDM prefix and a rejected range crossing its end.

`gpu-phase-summary.py KERNEL_LOG` summarizes native24MHz tiler/fragment/compute
intervals and the kernel's last_prepare_ns/last_active_ns/last_cleanup_ns fields.
Use one complete kernel-live.log from an experiment, not concatenated snapshots.
It selects final fragment snapshots, unions overlapping intervals, and excludes
multi-command packets from host phase totals. Includes source SHA and explicit
limits: all clients, firmware pauses possible, no GPU-utilization or completion
correctness claim. Qualified on122040 retained0ADlog and a known-overlap fixture.

`desktop-experiment.py --game sauerbraten --name NAME --seconds 180` runs the
unmodified ARM64 Sauerbraten release on metl4 under native Hyprland through
Zink/Honeykrisp. It uses the same mandatory watchdog/thermal guards, records
binary/source identities, checks map loading, renderer identity and a surviving
owned game window, and saves screenshots for separate visual inspection. Private
installation is `/opt/m4-sauerbraten`, config is the user's
`.config/m4-asahi-sauerbraten`; audio uses SDL's dummy device during graphics tests.
No game rendering features are disabled. `--trace` captures the original EGL calls for same-frame llvmpipe comparison;
`--game-profile` currently only applies to 0AD. 0AD remains the default game. Sauerbraten source and cross-build
arguments are recorded in `build/sauerbraten/source.json` and `build-command.json`.

## Shorter original-trace replays

`trace-prefix.py TRACE --last-call EGL_SWAP_CALL --output NEW_DIR` keeps every
call from trace start through the selected complete EGL frame. This shortens
repeated experiments without guessing dependency closure or removing setup.
It verifies the complete textual call/argument sequence against the original
prefix and records both trace hashes. Blob contents are copied by apitrace;
the textual check represents blobs by size and does not independently compare
their bytes. The prefix must still reproduce the original observed failure
before it is used for causal isolation. Host replays use `replay-llvmpipe.py`;
hardware replays still require the mandatory watchdog and thermal wrapper.

`gl-mesh-replay.c` is a small differential GL diagnostic for captured position
and 16-bit triangle-index buffers. Build with `-lEGL -lGL`. Arguments are vertex
file, index file, 64-byte column-major float matrix, output prefix, vertex stride.
Positions are float3 at offset0; all indices must be in range. Its four modes
render plain color in one draw, depth prepass plus color, the same prepass plus
63-index draw batches, and perspective fog. The camera, winding (CW), D32F
depth and 1024x768 target remain fixed between modes. It writes raw RGBA files
for comparison with host llvmpipe (`M4_TEST_LLVMPIPE=1`). Saving output is not a
pixel-correctness pass. Captured assets and matrices stay in build evidence;
no game data is added to the tool. Run the hardware binary only through
`gpu-experiment.py` and `run-m4-gl.py`, preserving both guards.

`shader-variant.py DIRECTORY --contains TEXT --replace TEXT --with TEXT
--output NEW_DIRECTORY` creates explicit diagnostic variants of Mesa's dumped
GLSL sources, retaining original/replacement SHA-256 and transformation text.
Use `--glob 'VS_*.glsl'` for vertex shaders; default is fragment shaders. Each
selected source must contain exactly one replacement site. Original files are
preserved. Use the output only as a temporary `MESA_SHADER_READ_PATH` inside a
guarded replay; shader replacements are causal controls, never production fixes
or game correctness evidence. Removing the environment variable restores the
original shaders without modifying the game or driver.

The mesh replay optionally accepts `MATERIAL_VERTEX_SHADER` as its final argument.
This uses the captured Sauerbraten vertex interface and 32-byte attributes, keeps
all eight outputs live through a diagnostic fragment shader, and uses recorded
camera/fog uniforms. The original GLSL is loaded from evidence, not embedded in
the tool. `M4_TEST_FOG_PLANE=1` instead selects the small dynamic plane-dot shader.
These modes isolate compiler behavior; their output is compared to the matching
host run, not to a fully textured game screenshot.

`replay-m4.py OUTPUT TRACE CALLS [IGNORE_CALL_FILE]` consolidates the existing
target-side EGL replay helper. Invoke only under `gpu-experiment.py`; it does not
own or replace the mandatory guards. `M4_REPLAY_COMPILER_DEBUG` supplies explicit
compiler flags, alongside the existing queue/draw-sync and shader-dump controls.
Check that a selected flag actually applies to AGX3 before attributing a result
to it (`AGX_MESA_DEBUG=wait` currently only changes AGX2 wait insertion).

`trace-shaders.py DUMP --program ID... --output NEW_DIRECTORY` extracts
linked GLSL sources from an apitrace textual dump, tracking attached shader
objects and recording call IDs/source hashes. It extracts source only, not
GL state or a runnable trace. Qualified545 sources exactly match earlier
independent extraction and Mesa shader dumps; shader names may be reused.

`gl-mesh-replay.c` can additionally accept an original fragment shader after
the vertex shader. Original material samplers use complete1x1 diagnostic
textures: diffuse/lightmap white, normal/direction neutral, shadowmap zero.
Ambient1/specular0/parallax0/glow defaults produce a fog grayscale reference
while the compiler retains the original shader interfaces. The optional
shadow projection is identity. These are diagnostic material inputs, not
actual-game validation. All modes in this case retain the original fragment
shader, so compare them to matching host-pair outputs.

### Private Sauerbraten build installation

`python3 tools/install-sauerbraten.py SOURCE_BINARY` stages a checksummed
binary, refuses installation while the game is running, preserves the previous
binary by hash, and replaces `/opt/m4-sauerbraten/sauer_client` atomically.
It performs no GPU experiment. Use `desktop-experiment.py --game sauerbraten`
for all live validation; watchdog and thermal guards remain mandatory. The
runner records the local game source commit and patch as well as the installed
executable hash. The source repository is
`reference/sauerbraten-2020/sauerbraten/src` (local, no remotes).

### SuperTux modern OpenGL renderer

`python3 tools/desktop-experiment.py --game supertux --name NAME --seconds 180`
runs the installed SuperTux 0.7 `opengl33core` renderer on welcome_antarctica
through M4 Zink. This version requests OpenGL 3.3 Core, not 3.1. The harness
verifies the requested and returned profile, renderer identity, game survival,
and GPU/compositor progress; saves initial/live/final images for inspection.
Config is isolated under `~/.config/m4-asahi-supertux`. Sound/music disabled
for the graphics experiment; watchdog/thermal guards remain mandatory.
`--trace` records the game's EGL calls while preserving the renderer audit hook;
fetch the trace named in `remote/graphics-trace.json` after completion. Tracing
adds overhead, so do not compare its FPS with an untraced benchmark.
`--inline-uniforms` tests Zink's existing uniform specialization for this game
without changing its graphics options. Record this flag in comparisons.

`desktop-experiment.py --game supertux --frame-timings --cpu-profile ...`
adds optional CPU-clock SDL swap measurements and the existing perf capture.
`frame-timing-summary.py EXPERIMENT/remote/supertux.log --after-seconds 20`
reports measured FPS/frame/swap time with an explicit startup exclusion. All
original rows remain in the log; no GPU commands or synchronization are added
by the swap hook. Rejects missing/reset frame sequences. Use the same scene,
resolution and graphics options for before/after performance comparisons.

For SuperTux, EGL capture routes GLEW GLX procedure lookups through the EGL
wrapper; direct GL calls alone are not a complete trace. Capture180741 replays
with all shaders/API calls on host llvmpipe, three final frames, no API errors.
`M4_REPLAY_LAUNCH_TRACE=0` suppresses per-launch byte dumps in `replay-m4.py`
for an explicitly timed replay; default remains1. Guard/thermal requirements
are unchanged, and all calls are still executed.

For performance iterations use `--seconds 20 --frame-timings` and summarize with
`--after-seconds 10`; save long runs for stability validation after a clear gain.
`--prefetch-ubos` and `--prefetch-descriptors` enable their opt-in GPU uniform
prefetch paths and compile-time audits. `--shader-dump` dumps compiled shaders
once rather than tracing every draw. Keep the same flags across comparisons.

### Omarchy Quickshell panel and background

`desktop-experiment.py --game shell --shell-ui --seconds 20 --name NAME`
runs the actual shell, opens/closes its menu and audio popup, captures images
and validates Quickshell's own renderer identity. Default backend is Vulkan;
`--quickshell-backend opengl` uses Zink and `software` selects a reference.
The canonical GPU shell wrapper lives alongside Mesa's m4-desktop harness and
is installed at `/opt/m4-hyprland/bin/omarchy-launch-shell`. The root helper
records its deployed hash. No game is launched in shell mode. Renderer logs,
process driver maps, DRM descriptors and same-process survival are retained;
inspect screenshots separately. Watchdog and thermal guards remain mandatory.

`quickshell-latency.py --guard-output /root/asahi-evidence/RUN --label NAME`
 runs on J713 inside an already-running desktop guard. It refuses a completed
 or stale guard, checks the recorded 60-second watchdog and live thermal health,
 and measures two normal menu opens against sparse centre-card pixels from
 native scanout. It saves IPC timings, scanout sampling times, first visible
 change and full before/after snapshots under the guard evidence directory.
 It never renews the watchdog itself. The visible-change threshold is a latency
 diagnostic, not a rendering correctness oracle; inspect the saved images.

`desktop-experiment.py --copy-trace` selects `HK_M4_COPY_TRACE=1`, a
 concise Mesa buffer-to-image upload-region diagnostic. Useful for correlating
 batched atlas uploads to native kernel render jobs without full launch dumps.

`g16-job-summary.py LOG --start SECONDS --end SECONDS --extent WIDTHxHEIGHT
--output JSON` summarizes saved serialized M4 render jobs, optionally filtering
`--asid` or repeating `--extent`. Intervals use kernel bracket timestamps;
exact monotonic timestamps are preserved separately. GPU stage durations use
the qualified24MHz counter and exclude firmware, queue and CPU polling time.
Used to verify glyph upload batching67->4 jobs without losing timing evidence.

`quickshell-latency.py --motion` runs the same six-second,120Hz cursor path
with the menu hidden and open, recording actual native KMS presentation counts
and IPC response times. It restores cursor position and uses the existing
live desktop guard. No frame readback occurs during motion. The status node
is sampled only at the endpoints because reading it locks against presentation.
`g16-job-summary.py --clock monotonic` aligns those phases with exact retirement
timestamps instead of the printk bracket clock.

`desktop-experiment.py --cpu-max-khz 4104000` compares normal CPU policy
limits with the default 1860000 kHz test ceiling. The thermal helper still
only lowers limits, so reboot before switching from a lower ceiling. This
preserves CPU thermal throttling, six sensor samples every0.5s, startup65C,
stop75C and progress-dependent watchdog60. Record the actual per-policy caps
from evidence; this is a test configuration comparison, not a driver speedup.

`quickshell-latency.py --direct-ipc` starts the IPC client with the desktop
user's UID, groups and home, avoiding runuser's extra PAM session startup.
The default retains the old method for comparable before/after measurements.
Each latency row records its transport; do not compare unlike transports as
a rendering improvement. Neither mode measures the complete physical click
input path, and the40ms scanout sampling period limits timing precision.

`quickshell-latency.py --motion --cpu-profile` adds a bounded199Hz CPU-clock
profile during the existing hidden/open cursor phases. It stops perf before
returning; analyze the saved perf.data with perf-summary.py and matching ELFs.
Use unprofiled repetitions to report FPS after a change.

`under-desktop-guard.py --guard-output RUN --label NAME -- COMMAND...` runs a
short auxiliary probe while the named desktop guard already owns watchdog60.
It checks guard identity, boot ID and live thermals throughout, kills its
command on lost supervision or deadline, and saves output/result under RUN.
It never renews a watchdog; do not use it without the parent guard.

`wc-zero-bench.c` compares DC ZVA with scalar paired stores on a private32MiB
WC Asahi GEM allocation. It never binds/submits that allocation to the GPU or
writes scanout. Three interleaved repetitions check every64-bit word and two
16KiB untouched borders. Cross-compile with the Mesa include/ UAPI directory
and run via under-desktop-guard.py. This measures complete resets, not skipping
zeroing; the initial J713 result found no benefit from replacing DC ZVA.

### `hypr-ipc.py`

Send one raw IPC request to the sole Hyprland instance under a runtime directory
(default `/run/user/1000`). This avoids repeated socket-discovery snippets. Run
configuration changes through `under-desktop-guard.py` inside an active test:
`python3 /root/under-desktop-guard.py --guard-output /root/asahi-evidence/RUN
--label scheduler-on --seconds 5 -- python3 /root/hypr-ipc.py
'eval hl.config({render={new_render_scheduling=true}})'`. Read-only requests such
as `j/getoption render:new_render_scheduling` use the same IPC helper.
The helper itself does not start a watchdog or a compositor.

### `present-timing.py`

Collect the native DCP driver's small completion history without reading pixels
or taking its presentation lock. Requires the `m4_present_history` debugfs file.
Run `collect --seconds 25 --output /root/asahi-evidence/RUN/frames.json` through
`under-desktop-guard.py --seconds 30`. Run a short motion benchmark alongside it,
then analyze its exact monotonic interval offline with
`analyze frames.json --start START --end END --output summary.json`.
Reports frame-interval percentiles, missing records and long gaps split into
time before the DCP submission and time waiting for DCP completion. An idle
desktop has legitimate long gaps: select an interval with continuous motion.

`install-asahi-module.py` also stages the development DCP module with
`--module-name m4_dcp_rtkit --target /root/m4_dcp_rtkit-asahi.ko`. Both old and new
module names must match; the previous file is backed up. This still never
reloads a live module. Activate only after the guarded reboot.


`quickshell-latency.py --motion --motion-seconds 20 --motion-path menu
--motion-toggle --direct-ipc` continuously moves the pointer while opening and
closing the menu every two seconds. It records action and IPC timestamps without
reading scanout pixels. Run inside `under-desktop-guard.py --seconds 28`, alongside
`present-timing.py collect`, to include opening stalls in the measurement.


`guarded-reboot.py --output /root/asahi-evidence/UNIQUE-REBOOT` runs on an idle
J713 after the prior guard has completed. It refuses active DRM clients, arms
and verifies the 60-second watchdog, checks the thermal policy/sensors, saves
pre-reboot evidence and requests reboot without further watchdog renewal.
Allow at least 90 seconds before another SSH attempt. It never hot-reloads a
module or bypasses the desktop experiment guard.


`task-waits.py --pid PID --seconds 25 --output waits.json` samples the selected
process's kernel wait channel, syscall and blocked stack every 5ms. Invoke via
`under-desktop-guard.py --seconds 28`. For ARM64 DRM atomic ioctls only, it also
reads the small userspace request to identify flags and property values. Reads
can race with a task switching state; this is sampled evidence, not an exact
scheduler trace. It requires root and never changes process state or renews a
watchdog. Correlate timestamps with `quickshell-latency.py` slow IPC records.


`bash tools/build-hyprland.sh [JOBS]` incrementally cross-builds the local Hyprland
0.56.1 checkout against the existing Omarchy ARM64 sysroot. Default parallelism
is 8; `M4_GRAPHICS_WORKSPACE` selects the sysroot/toolchain directory. Python3.14
is required for the upstream Lua stub generator. The initial configuration
fetches upstream's pinned Glaze7.2.0 and initializes the two required submodules.
The resulting `build/hyprland-m4-arm64/Hyprland` is for private installation;
this helper never installs or starts it.

`desktop-experiment.py --hyprland-binary /opt/m4-hyprland/bin/Hyprland ...`
selects a private compositor through start-hyprland's supported `--path` option.
The selected target binary is hashed and local compositor source/patches are
retained in the experiment manifest. The default is the qualified private binary; pass `/usr/bin/Hyprland` for
an explicit unpatched baseline. Shell tests keep startup/final screenshots;
scheduled intermediate screencopies are reserved for games or explicit UI
capture tests because they stall the compositor input loop.

### Normal accelerated login and compiler replay

`normal-desktop/` contains the J713 normal SDDM and user-session configuration.
`install-normal-desktop.py --backup NEW_DIRECTORY` runs on J713 after staging
those files at `/root/m4-normal-desktop-stage`. It backs up replaced files,
installs the native DCP module and initializes the Asahi/DCP stack before SDDM.
It discovers the display card instead of relying on probe-order numbering.
SDDM's explicit GreeterEnvironment and the user wrapper select the private M4
Mesa stack; the user wrapper loads the complete normal Omarchy configuration.

`normal-login-check.py --output NEW_DIRECTORY [--capture-compiler]` runs on J713;
read the password from stdin. It arms watchdog60, starts the six-channel thermal
guard, caps test CPU clocks, simulates SDDM keyboard login, checks mapped panel
layers and private GPU libraries, opens Foot, pastes an ordinary SuperTuxKart
launch command through the clipboard and saves screenshots. It uses the real
user game configuration, including enabled dynamic lighting and compression;
this does not replace a separate GUI menu-to-race test. Inspect those
images before declaring success. Failure evidence is preserved and watchdog
recovery remains conservative until a reliable GPU drain check is available.
The optional compiler capture flag is removed at completion. On success, test CPU caps are restored after stopping the thermal monitor.
On failure they remain until recovery. This is not an unattended stress runner.

`compiler-replay.py BUNDLE --output NEW_DIRECTORY` runs entirely on the host,
using `build/mesa-m4-asahi-host/src/asahi/compiler/agx_shader_replay`. It saves
compiler diagnostics, input/build hashes and dirty-source identity. The Mesa
`src/asahi/compiler/README-replay.md` describes capture limits and serialization
compatibility. The original shader bundle remains unchanged for regression
comparison. Compiler acceptance and GPU execution correctness are separate checks.

For boot qualification, `normal-login-check.py --existing-greeter` uses the SDDM
already started by normal boot. `--native-screenshots` saves `*.m4fb` directly
from the DCP driver's read-only scanout snapshot (32-byte header followed by
stride-padded XR24/AR24 pixels). This avoids adding compositor readback draws to
a login test. It does not disable desktop effects or change GPU rendering.
`scanout-image.py INPUT.m4fb OUTPUT.png` converts a saved native scanout snapshot
to a viewable PNG on the host, validates its header/stride/format, and reports
the framebuffer ID. It performs no hardware access.


`hyprland-shader-source.py border.frag OUTPUT` expands the checked-out Hyprland
shader includes with rounding enabled and color management disabled. It preserves
the shader math; this is a diagnostic variant, not a replacement desktop shader.
`hyprland-border-probe.c` runs it in a surfaceless GLES3 RGBA framebuffer and saves
one- and two-color border readbacks as raw RGBA8. Compile with EGL/GLESv2; run M4
only through `gpu-experiment.py` and `run-m4-gl.py`, with the desktop stopped.
The host reference uses `M4_TEST_LLVMPIPE=1 LIBGL_ALWAYS_SOFTWARE=1
GALLIUM_DRIVER=llvmpipe`. Arguments are `SHADER OUTPUT_PREFIX`.
Optional environment controls reproduce concrete desktop state: `M4_TEST_BLEND`,
`M4_TEST_SCISSOR`, `M4_TEST_SQUARE`, `M4_TEST_DEPTH_ATTACHMENT`, `M4_TEST_RGB10A2`,
`M4_TEST_WIDTH`, `M4_TEST_HEIGHT`, and `M4_TEST_SECOND_GRADIENT` (default zero).
A completed draw is execution evidence only; compare all four readback channels
against the reference before claiming pixel correctness.

### Export the actual Hyprland GL stream

`gl-trace-reproducer.py TRACE --last-call NUMBER --output NEW_DIRECTORY` exports
a trusted local apitrace capture to `replay.c`, content-addressed buffer/shader
payloads, and a manifest. It retains GL mutations through that call and rejects
unsupported import shapes. `--apitrace PATH` selects the executable and its
adjacent `lib/apitrace/scripts` pickle modules; the workspace default is under
`build/apitrace-host/usr`. The tracked source is also in Mesa's
`src/asahi/vulkan/tests/m4-desktop/`.

Compile `replay.c` with EGL and desktop GL (`cc replay.c -lEGL -lGL -o replay`).
Run from the export directory so payload paths resolve. A host reference uses
`M4_TEST_LLVMPIPE=1 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe ./replay`.
Cross-compile the same source to `m4-replay` and stage the complete directory on
J713. Stage `run-gl-trace-export.py` and `run-m4-gl.py` in `/root`; invoke through
`gpu-experiment.py`, for example:

```
python3 tools/gpu-experiment.py --name captured-frame \
  --hypothesis 'Replay every captured draw with normal batching' \
  --expect 'PASS: captured GL draws finished' --seconds 35 -- \
  env ZINK_DEBUG= AGX_MESA_DEBUG= HK_G17P_LAUNCH_TRACE=1 \
  python3 /root/run-gl-trace-export.py /root/hypr-gl-replay '{output}'
```

The target runner stops display clients with a bounded quiescence interval,
records source/executable identities and diagnostic controls, and saves readback.
It requires the outer watchdog/thermal guard; it never renews the watchdog.
An application completion message is insufficient: Zink has returned from
readback after device loss, so the outer kernel/guard verdict is authoritative.

This is a rendering diagnostic, not a WSI replay: native XR24/AR24 renderbuffer
imports become retained local RGBA8 storage; texture imports use apitrace's
captured TexImage2D payload. EGL fences, presentation and context transitions
are omitted. The manifest lists adaptations. Reject unsupported captures rather
than replacing missing texture data. Do not use untrusted pickle input.

Optional `M4_GL_REPLAY_FINISH_BEFORE=CALL`, `M4_GL_REPLAY_SKIP_DRAWS=CALL,...`
and `M4_GL_REPLAY_REPEAT_DRAW=CALL` are explicit causal controls, never production
fixes. With all three unset, the original 177-draw prefix through call 5170
reproduced the Hyprland border hang. The compiler's coverage-ordering fix then
passed that unchanged prefix and the normal SDDM login. The readback matched
llvmpipe within one RGB level with exact alpha across all 4,259,840 pixels.

`normal-login-check.py --trace-stk --trace-hyprland` captures EGL calls and native
shader/command diagnostics from the actual applications. Hyprland traces are
saved under `~/.cache/m4-login-traces/` with apitrace's process/timestamp names; game artifacts
are under the run-named subdirectory. The Hyprland flag uses a temporary `/run`
marker and is removed on completion. Kernel fault monitoring stops input at the
first GPU retirement failure, before attempting further application startup.

`normal-login-check.py --login-only` qualifies login and Foot, leaving the game
for screenshot-guided interaction. `desktop-ui.py` supplies that interaction in
an existing normal desktop. Copy it to `/root/desktop-ui.py`, then start it with
`gpu-experiment.py --seconds 360 --expect 'PASS: bounded desktop UI session closed'
-- python3 /root/desktop-ui.py serve '{output}' --seconds 300` (also provide the
experiment name and hypothesis). The shorter server deadline bounds cleanup.
Send requests on J713 with `desktop-ui.py send REMOTE_OUTPUT JSON`, for example
`{"action":"paste","text":"/usr/bin/supertuxkart","name":"command"}`.
Paste does not press Enter: fetch and inspect the saved native snapshot first.
`{"action":"key","key":"Return","name":"launch"}` holds the key for
150 ms so a game frame can observe it. Use `snapshot` between menu transitions
and `finish` after the test. Every reply names a saved `.m4fb`; convert it with
`scanout-image.py`. Completion only means the bounded UI capture closed: inspect
the screenshots and game log separately to establish game/race success. The
server terminates newly started SuperTuxKart processes on exit and requires the
ancestor guard's live GPU retirement, watchdog60 and six-channel thermals.


`native-metal-compile.py INPUT --stage builtin --compiler ABSOLUTE_MAC_PATH
--output NEW_DIRECTORY` compiles a BTNF builtin on the authorized Mac without
GPU execution. It saves the exact input, command/version, native archive and
checked Mach-O text/symbols. Use Metal 32023.917 for the EOT factory; the host's
32023.883 compiler does not implement it. A separate 27A5194o export is mounted
read-only at /Users/m1/m4-metal-917. Render pipelines also accept
`--pixel-format RGBA16Float` (default RGBA8Unorm).

`hdr-attachment-probe.c` checks single half-float and single/dual packed HDR
attachments through clear, draw, additive blend and reload. Build with EGL, GL
and libm; run the ARM64 binary with `run-m4-gl.py` inside `gpu-experiment.py`.
Set `ZINK_DEBUG=` explicitly to exercise ordinary batching. Its desktop GL
half-float readbacks avoid an unrelated unsupported RGBA32F conversion target.
Host llvmpipe is permitted only with explicit M4_TEST_LLVMPIPE=1. The tracked
source/native references are in Mesa's src/asahi/compiler/test/probes/.

`desktop-ui-remote.py REMOTE_OUTPUT JSON LOCAL_IMAGE_STEM` sends one request to
an already guarded UI server, fetches its native snapshot, and runs the existing
scanout decoder. It returns a PNG path for visual inspection before the next
input. It does not own a watchdog or launch an experiment. The tracked copy
lives with the kernel normal-desktop test helpers.

`g16-fault-dump.py decode DUMP --scheduler` also compares the retained
parameter manager's page-list address with its hardware descriptor and shows
InitBuffer's full-reinit/incremental selector. A mismatch is evidence of stale
slot state; the sequential snapshot alone does not identify who wrote it.

The `build/linux-asahi-j713` running-kernel ABI uses Rust 1.98.0. Build modules
with `/home/council/.rustup/toolchains/1.98.0-x86_64-unknown-linux-gnu/bin` first
in PATH, then `make -C linux/linux-asahi O=$PWD/build/linux-asahi-j713 ARCH=arm64
LLVM=1 -j8 drivers/gpu/drm/asahi/asahi.ko`. The system Rust 1.94.1 regenerates
incompatible Rust symbols and fails modpost; never suppress those errors.

`firefox-probe.py` runs bounded normal Wayland Firefox with an isolated profile
and shader cache, records graphics diagnostics and native scanout, and optionally
captures Vulkan through GFXReconstruct. Use only inside `gpu-experiment.py`,
with target `--guard-output '{output}' --observe-seconds 2 --capture`.
The default WebGL page is MDN sample6; `--renderer lavapipe` selects an explicit
software reference. Tracked usage and evidence: Mesa's
`src/asahi/vulkan/tests/m4-desktop/README-firefox.md`.

`vulkan-trace.py replay --end-frame N` preserves all initialization and stops
after the bounded frame prefix. It is separate from `--frames` screenshots.
GFXReconstruct's local `${ProcessId}` capture-filename patch is retained in
`tools/patches/gfxreconstruct-process-id.patch` and the tracked Mesa tools.

`install-mesa-library.py BUILD --label LABEL --allow-live` atomically updates a
compatible private userspace library with checksums and backup. New processes
load it; already running processes retain their old mapping. Default refuses
live GPU clients. Hardware experiments still require the full guard.

`flatbuffer-field.py FILE --path 1.3 --type u32` reads explicit, zero-based
native metadata table fields without inventing a schema. Missing fields fail
because their schema defaults are unknown. `native-metal-compile.py` now retains
both __GPU_LD_MD and __GPU_ARCH_LD_MD alongside code and compiler provenance.

`macos-native/speclm-imageblock.py UMD` executes the exact retained G16G native
imageblock arithmetic and SPECLM encoder offline under Unicorn. Run with
`build/display-venv/bin/python`; it checks the UMD hash, bounds instructions and
uses synthetic owned memory. This verifies encoding, not GPU rendering.

`rgba32-attachment-probe.c` checks float, signed and unsigned RGBA32 clear,
draw and reload using full-width exact channel values. Cross-build with EGL/GL,
run under `run-m4-gl.py` and `gpu-experiment.py`. CPU llvmpipe is permitted only
with explicit `M4_TEST_LLVMPIPE=1`. A separate Vulkan host-image-copy diagnostic
is available by building `g17p_offscreen_triangle.c` with
`G17P_RGBA32_SFLOAT=1`, `G17P_HOST_IMAGE_COPY=1` and `TEST_DEVICE_ID=0x8132`.

## Saved firmware disassembly

`firmware-disasm.py IMAGE --offset 0x19364 --size 0x268 --output NEW.txt`
disassembles only a selected range of a saved ARM64 image. It records the full
image hash, range and exact objdump command beside the output. Offsets are file
offsets; extract a raw text segment first if the input is a container. This
offline tool does not access hardware or require a hardware experiment guard.

## Multisample coverage oracle

`multisample-probe.c` renders a small surfaceless OpenGL FBO through Zink. Pass
1, 2, or 4 for its sample count. It checks the actual allocated count, clears
black, writes each sample red, and compares all 4,096 resolved pixels after each
write. Each readback forces the attachment store/reload path. Use
`M4_MSAA_ALL_SAMPLES=1` to draw every sample together, isolating storage from
selective coverage. Neither mode changes application settings or qualifies an
application. Host llvmpipe reference execution requires `M4_TEST_LLVMPIPE=1`.

Build with EGL/GL development headers and `-lEGL -lGL -lm`; cross-build and deploy
for the target. Hardware execution must run inside `gpu-experiment.py` with the
watchdog and thermal guards, for example its existing `/root/run-m4-gl.py`
wrapper. The versioned copy is in Mesa's `tests/m4-desktop/` directory.

`vulkan-fragment-probe.py SOURCE --output NEW_DIR --samples 1|2|4
--rgba R G B A` builds a custom fragment shader into the existing Vulkan
triangle fixture and retains hashes, commands and exact expected RGBA bytes.
It does not execute hardware. The versioned mirror needs `--fixture` pointing
to `../g17p_offscreen_triangle.c`; the workspace helper finds it automatically.
Use the watchdog/thermal experiment wrapper to run `NEW_DIR/probe-arm64`.
The fixture clears to black with alpha 255 before drawing. Use its actual
`g17p-msaa2-triangle: PASS` success marker for 2x tests. Diagnostic fixtures are
not application captures and cannot by themselves qualify application support.
# Offline M4 pipeline compilation

`integer-helper-reference.py NEW_FILE` generates the independent numerical
oracle for `m4_vulkan_integer_helpers.comp` using exact Python arithmetic.
Use the existing gather/store harness for lavapipe and guarded M4 comparison.

`vulkan-fragment-probe.py FRAGMENT --vertex VERTEX --rgba 255 0 0 255
--output NEW_DIR --run-host-shim` builds and compiles a pipeline through the
host M4 DRM shim, retaining shader bundles, logs, exact library hashes and
source changes. It exits before command recording/submission and times out
after 20 seconds. Build the host Asahi Vulkan library and DRM shim first.
Fixtures live in `mesa/m4-asahi/src/asahi/vulkan/tests/m4-desktop/`:
`solid-red.frag` and `xfb-pipeline.vert`. The XFB fixture intentionally tests
an unsupported compiler path; it does not establish valid application support.
Ordinary ARM probe execution still requires the Linux watchdog/thermal guard.

`vulkan-fragment-probe.py --allow-unadvertised-samples` explicitly opts into
a diagnostic sample-count attempt. It retains the requested count, prints a
warning and records the capability bypass in manifest.json. Default remains
to refuse unadvertised counts. Neither a successful diagnostic nor host shim
compilation qualifies public Vulkan capability support.

`vulkan-fragment-probe.py --occlusion-query` wraps the diagnostic draw in a
Vulkan occlusion query and reports `SAMPLES-PASSED non-precise` after fence
completion, before the pixel oracle. This distinguishes zero visibility from
later color/store failures. A non-precise positive result is not an exact
sample count. This adds query operations to the diagnostic fixture, preserves
its shader and pixel expectation, and records the option in its manifest.
Run only through the same watchdog/thermal guard as ordinary ARM probes.

The multisample oracle accepts a second argument `rgba8` (default) or
`rgb10a2` to check both exposed color storage formats with the same per-sample
resolve/reload oracle, for example `multisample-probe 4 rgb10a2`.

An optional third argument `depth16`, `depth24`, or `depth32f` adds a depth
attachment. Near red samples must reject a farther green draw; unresolved
samples stay green. Every step resolves and reads all pixels, forcing depth
store/reload between draws. The default `none` keeps the color-only oracle.

`gfxreconstruct-external-rebind.patch` makes external-memory translation
consistent during Vulkan replay. The rebind allocator already removes external
image/import metadata; it must not export the resulting non-exportable VMA
allocation. The patch reports the omitted export and returns an invalid FD
sentinel without submitting that invalid Vulkan operation to the driver.
This mode does not reproduce inter-process DMA-BUF sharing and is only a
rendering diagnostic; retain native application validation for sharing.
Apply to the matching GFXReconstruct source and rebuild `gfxrecon-replay`.
The replay manifest hashes the actual binary and records memory adaptation.

`install-mesa-library.py --restore-sha256 SHA256 --label LABEL` restores a
previous target library without transferring it again. It verifies the backup
hash, preserves the current library, installs atomically, and checks the result.
The same client checks apply; `--allow-live` permits compatible userspace-only
updates while existing processes retain their old mappings. Source-library
installation and restoration are mutually exclusive.

Set `M4_MSAA_DEPTH_BIAS=1` with `depth16` to verify the integer depth-bias
formula: 32768 units must move red behind green. The ordinary float-depth
quantum would leave red in front and fail the full-image oracle.

`depth32fs8` and `depth24s8` select combined depth/stencil attachments.
Red writes stencil1 per sample; green requires stencil0 with depth testing
disabled, so successful depth rejection cannot conceal a broken stencil test.

`vulkan-fragment-probe.py --transform-feedback --vertex xfb-pipeline.vert`
builds a triangle that captures three vec4 positions into a guarded buffer and
checks all captured bytes as well as the normal pixel oracle. It requests
VK_EXT_transform_feedback; unsupported drivers fail at device creation. Use
`--run-host-shim --record-command-buffer` for offline compiler/recording triage,
then run the ARM executable only through the mandatory experiment guard.

`shim-first-error.py build/PROBE` reruns a retained host-shim probe under GDB
and saves the first Vulkan error's backtrace, or a compiler crash backtrace.
It validates the compile-only manifest and probe hash and breaks at the
non-inlined Vulkan error logger, avoiding misleading optimized inline locations.
This helper is for offline recording only, not hardware execution.

Transform-feedback extensions to the probe: `--rasterizer-discard` verifies
feedback without color writes; `--transform-feedback-resume` verifies two
draws, six positions and a 96-byte saved counter; `--right-rgba R G B A`
checks a different color in the right half. With xfb-varying.vert/frag use
`--rgba 0 0 0 255 --right-rgba 255 0 0 255` for a nonconstant interpolation
oracle that cannot pass with a missing/constant varying.

Guarded M4 evidence: 185711 xfb-discard-exact passed; 185907
xfb-varying-and-resume passed counter save/resume (its constant color was
optimized away and does not qualify interpolation). 190320
xfb-split-interpolation passed all positions and pixels with checksum
`c6714fd7dac17725`, matching lavapipe. Peaks 57 C, no GPU faults. These
checks qualify enabling transformFeedback on the experimental M4 path;
geometryStreams remains disabled. Chromium application validation is pending.

## ANGLE format admission and sRGB resolve

`gl-format-caps.c` queries the required ES3 renderbuffer sample counts and
transform-feedback component limit. Build with `-lEGL -lGL`; run under the GPU
guard through `run-m4-gl.py`. A query pass is not rendering qualification.
`multisample-probe 4 srgb8` enables framebuffer sRGB and compares incremental
sample resolves/reloads against linear averaging followed by sRGB encoding.
The same oracle supports 1x/2x and explicit llvmpipe reference execution.
`vulkan-fragment-probe.py --format srgb8` selects an sRGB attachment while
retaining the supplied byte oracle; its host-shim mode never submits to GPU.

`mip-bias-probe.c` tests implicit derivatives plus varying positive/negative
shader mip bias and a separate sampler bias. Seven constant-color mip levels
provide an exact oracle, including fractional trilinear interpolation across
six 64x64 readbacks. Build with `-lEGL -lGL -lm`; run J713 only through the GPU
guard and `run-m4-gl.py`. Explicit `M4_TEST_LLVMPIPE=1` enables the reference.
The Vulkan capture uses the same Zink build over lavapipe; rebinding, omitted
pipeline cache and removed unsupported features are recorded replay adaptations.

`fragment-image-probe.c` writes a four-quadrant RGBA8 image and a red framebuffer
from one fragment shader, then checks both complete64x64 outputs. Its default
uses a formatless writeonly image; `typed` adds the rgba8 shader qualifier.
Build with `-lEGL -lGL -lm`, run only under the hardware guard, or use explicit
`M4_TEST_LLVMPIPE=1` for the reference. Vulkan capture retains the commands.
The Chromium probe now records WebGL context/device/loss status and a small
central readback in `webgl.json`. This is supplementary evidence: a later
screenshot failure or GPU-process crash still means the browser test failed.

`fragment-depth-probe.c` checks three explicit fragment depths against a depth
attachment cleared to0.6, reading both color and float depth. Its `constant`
mode exports the same per-draw depth as red to isolate staging failures from
source arithmetic. Build with `-lEGL -lGL -lm`; run through the mandatory GPU
guard or explicitly select the llvmpipe reference. Each mode checks4096pixels
per draw; neither a compiler success nor a correct color alone counts as pass.

`fragment-image-probe attachmentless` tests storage writes with no color/depth
attachment. `fragment-depth-probe 2|4` additionally attempts color and depth
resolves; the current Zink path rejects the multisample depth blit, so these
modes intentionally fail even when their color depth-test result is correct.
`M4_PROBE_OUTPUT=DIR` retains raw depth and RGBA data from the depth probe.

`shim-first-error.py NEW_DIRECTORY --trace CAPTURE.gfxr` diagnoses an existing
Vulkan capture under the host no-op DRM shim. It records exact binary/trace
hashes and rebinding/offscreen/feature adaptations, and stops at the first
actual Vulkan error logger. It never proves GPU output or external sharing.
The host replay must include the documented external-memory rebinding patch.

Fragment writeout evidence (J713,2026-09-12):200729 and200752 validate exact1x
color/depth and typed/formatless image outputs;201415 validates attachmentless
image output.201447 replays Chromium's retained failing capture successfully,
with explicit external-sharing/rebinding adaptations and no replay pixel claim.

`fragment-image-probe sampled buffer attachmentless` reproduces a sampled
texture copied by a fragment shader into a storage texel buffer, as used by
Chromium screenshot readback. It checks all4096 destination texels against
known quadrants. Omit`buffer` to check a2D storage-image destination instead.
The202608 lavapipe capture passes;202655 reproduces4096/4096 incorrect native
texels with the old null storage-texel-buffer descriptor.
Native descriptor fix202826 passes all4096 texels;202844 replays the retained
Chromium capture without a GPU or Vulkan error. The latter is an adapted
process replay, not a pixel or external-sharing qualification.

Chromium probe explicitly foregrounds the tested page and records document
visibility. An animation callback has a3s diagnostic timeout so a stalled
callback does not prevent saving the page image; timeout still fails the test.
203030 confirms visible correct CDP WebGL output after the texel-buffer fix,
but native display capture fails because DCP had latched its idle callback
failure at20:19; retained screenshot alone does not qualify live presentation.
`app-observe.py --seconds40` allows enough loading time before a≥10s STK race
observation. The enclosing watchdog/thermal guard remains mandatory and must
allow cleanup time; the helper does not infer racing from process survival.

Evidence transfers use rsync compression to retain full raw scanouts without
repeated transport timeouts. Native203330 qualifies Chromium WebGL live
output;203436 validates STK raceclock15.050s with correct visible scene and
M4 Zink renderer. Peak temperatures60.25C and59.5C respectively.

`sample-fetch-probe.c` creates a2x or4x color texture, writes a different known
color to each sample using sample masks, and fetches every sample separately
into a single-sample framebuffer. All4096 pixels per sample are checked. Build
with`-lEGL -lGL -lm`, run through the GPU guard.203931 captures the4x lavapipe
reference;204115(native4x) and204146(native2x) pass onJ713 at57C maximum.
The Vulkan sampledImageColorSampleCounts limit must include qualified2x/4x
support: denying it makes Zink reject Firefox's multisample RGBA-to-BGRA blit.
Integer/depth/stencil texture sample limits are not expanded by this test.
Library staging now compresses SSH transfers while retaining SHA256 validation
and atomic installation; existing processes retain their previous mapping.

`app-observe.py` cleans up descendants by their exact per-run shader-cache path
using pidfds, including GNOME processes that outlive their launcher or ignore
SIGTERM. This prevents a stale compositor from occupying the next test socket.
`fragment-image-probe pbo-bgra` checks explicit BGRA byte ordering through a
pixel pack buffer;204656LVP and204802M4 pass all4096color pixels. Firefox's
204212 live scanout has correct colors, but its page screenshot swaps R/B in
the WebGLcanvas. The latter remains a readback gap, not a live-display claim.

## GNOME presentation diagnostics

`sddm-session-probe.py --session gnome --renderer asahi --capture` uses temporary
SDDM autologin and a runtime user environment generator under the existing GPU
experiment guard. It restores the ordinary display manager configuration and
retains process maps, native scanout, per-process Vulkan captures and the journal.
The generator preserves GFXReconstruct's literal `${ProcessId}` filename token;
`environment.d` would expand it prematurely. Presentation errors fail the probe.
A successful observation still needs a meaningful visual check.

`gbm-window-probe.c /dev/dri/card2` reduces standalone compositor buffer handoff
to four 64x64 alternating-color frames. It verifies GL pixel results, distinct
locked front buffers and exportable DMA-BUFs without taking DRM master or changing
display state. Compile with `cc -O2 -Wall -Wextra ... -lgbm -lEGL -lGLESv2` and
run through `gpu-experiment.py` and `run-m4-gl.py`. The latter forces the old Zink
loader override; to test normal display-node selection pass `/usr/bin/env
MESA_LOADER_DRIVER_OVERRIDE= /root/gbm-window-probe /dev/dri/card2`. Keep
`GALLIUM_DRIVER=zink` and the selected native ICD.

`egl-swap-timing.c` forwards EGL swaps, extension-proc lookups and GBM front-buffer
lock/release calls unchanged. It logs process/surface identity, timing and results,
without consuming EGL error state or adding synchronization. Build as a shared
library (`cc -shared -fPIC -O2 -Wall -Wextra ... -ldl`); supply its absolute target
path with the session probe's `--egl-trace` only during a bounded diagnosis.

The permanent Zink display-node correction is in Gallium's DRM target and kmsro:
when Zink is explicitly selected for a node without rendering capability, select
a compatible render node through kmsro and retain Zink (do not choose a different
native Gallium driver). The normal `MESA_LOADER_DRIVER_OVERRIDE=zink` remains
required by this installation's Hyprland/GL loader. Clearing it globally broke
the greeter and was reverted; the earlier cleared-override probe is diagnostic.
Native GNOME run `20260912T212138983849Z-gnome-native-zink-kmsro-first-frame`
shows the desktop/top bar without GBM presentation errors, peak 58.25 C.
The runner now waits for first scanout instead of treating cold-start ENODATA
as a rendering failure. `normal-login-check.py` likewise waits for compositor
IPC sockets after observing the process.

`scanout-image.py --allow-kms-allocation` explicitly accepts display-allocated
buffers in addition to GPU-exported scanout. Its output records the allocation
origin. Kmsro can import a display allocation into the GPU and render directly
into it; the origin flag alone proves neither GPU nor CPU rendering.

`vulkan-color-blit.c` isolates BGRA-to-RGBA readback with exact per-byte checks.
It uploads patterned BGRA bytes, or uses `--clear`/`--render-clear` to produce
the source through transfer clear or render-pass clear, then blits to RGBA and
checks both source and destination readbacks. Build with `cc -O2 -Wall -Wextra
... -lvulkan`. Hardware requires gpu-experiment.py and run-m4-gl.py; `--reference`
permits a separately selected software ICD. The three M4 cases passed on
2026-09-12 (212444,212840,212954), so they do not yet reproduce Firefox's
external-image/MSAA readback discrepancy. Do not treat these as a Firefox fix.

`integer-texture-probe.c` checks all 2048 signed/unsigned 32-bit components
through normalized-coordinate nearest sampling and integer render targets.
Compile with `cc -O2 ... -lEGL -lGL` (ARM64 sysroot for hardware).
Reference: `M4_TEST_LLVMPIPE=1 GALLIUM_DRIVER=zink LIBGL_ALWAYS_SOFTWARE=1
MESA_LOADER_DRIVER_OVERRIDE=zink VK_DRIVER_FILES=... probe`.
Hardware: gpu-experiment.py wrapping run-m4-gl.py. M4 passed run
20260912T221956435252Z, 55.75 C, zero mismatches; high bits and negative
values distinguish integer sampling from accidental float conversion.

`flat-line-probe.c` checks full-frame output for line lists and strips,
first and last provoking vertex (4096 pixels); `--constant` isolates raster
coverage from varying interpolation. Build with -lEGL -lGL; run references
with the same Zink/lavapipe environment as integer-texture-probe, hardware
under gpu-experiment.py. M4 20260912T223221136775Z passed, 55.75 C.
Native HAL200 encodeISPPrimitiveType arm64 0x41d458..0x41d464 sets the
secondary-face object class to line (1), replacing the triangle (0) template.
This probe exposed and fixed missing line rasterization, not a shader-color
approximation. Dynamic line width still needs separate qualification.

### Scratch lifetime and forced-spill oracle

`tps-oracle.py repeat --output build/tps-repeat` produces a 512-byte private
array with overlapping dynamic and static reads across all 32 lanes.
`tps-oracle.py spill --vectors 8 --output build/tps-spill` keeps eight vectors
live; run with `AGX_MESA_DEBUG=spill` to exercise scalar and 64-bit pointer
spills. The tool generates SPIR-V, exact SSBO input/reference bytes and hashes;
it executes no GPU work. Use `vk-buffer-oracle` with one workgroup inside
`gpu-experiment.py` for hardware execution, with the normal watchdog/thermals.

G16G Apple `graphics-features/repeat-tps.metal` demonstrates RLD f27 and
LDTPS f66 clear on retained reads and set on consuming reads. Tests on
2026-09-12 initially failed only at overlapping array locations (12/4224
words), and all outputs of forced pointer-spilling shaders were zero.
Preserving TPS across reloads passed the 512-word minimal spill and the
4224/2048/10240-word array/eight-vector/40-vector tests, peak 55.75 C.
Evidence: experiments `20260912T225839789763Z-forced-spill-retain-tps` and
`20260912T225930577131Z-tps-retention-regressions`. Encoding unit tests retain
byte-exact consuming and non-consuming Apple witnesses. Dynamic stores and
16-bit scratch lowering remain unimplemented.

### Image dimensions and copy strides

`vulkan-image-dimensions.c` checks 1D, 1D-array and 3D optimal images at mip1,
RGBA8 and RGBA32F. It uploads a full subresource, patches a rectangle/slice
range with padded buffer rows, then reads the whole image and a padded partial
region. Untouched texels, source bytes and destination padding are checked.
Compile with `cc -O2 vulkan-image-dimensions.c -lvulkan`; `--reference` allows
lavapipe. Hardware execution requires `gpu-experiment.py`.

Lavapipe and M4 passed all six exact byte comparisons on 2026-09-12, guarded
experiment `20260912T230401875508Z-image-dimensionality`, peak55.25 C. Blender's
retained reference capture no longer reports unsupported raw uploads in the
host noop-shim replay; attachment layouts still stop API4004.

### Mixed render-target oracle

`mrt-attachment-probe.c CASE` uses Zink to check full clears, a left-half draw,
then a right-half draw while preserving prior pixels across readback/reload.
Case0 is R16_UINT; case1 is sRGB RGBA8 plus linear RGBA8; case2 is RGBA16F plus
R16_UINT; case3 adds RG16F between those attachments. Compile with
`cc -O2 mrt-attachment-probe.c -lEGL -lGL -lm`. Cases0/1 passed Zink/lavapipe
and M4 on 2026-09-12 (`20260912T230756398204Z-scalar-integer-srgb-mrt`, peak
55.5 C). Cases2/3 are retained targets for ongoing Blender bring-up, not passes.
Always run hardware through `gpu-experiment.py`.

### Offline capture replay and native EOT inputs

`replay-driver-shim.py TRACE --output DIR --graphics-feature-probe` runs a
bounded host-only noop-DRM-shim replay. It retains the Mesa patch/HEAD and
binary/trace hashes. Offscreen swapchains, memory rebinding, omitted pipeline
cache and removed unsupported capture capabilities are explicit diagnostics;
this is a compiler/recording check and never evidence of GPU application support.

`native-eot-input.py 12:4 9:1 --output build/float-uint.btnf` constructs the
witnessed EOT flatbuffer for RGBA16F and R16_UINT. Add `12:2` between them for
the three-attachment case. Native format numbers and component counts are
explicit. Default partition0 means one tile partition; `--split-partitions`
is only a compiler diagnostic. The same-partition dual input matches the
existing witness byte-for-byte. Compile with `native-metal-compile.py --stage
builtin --compiler /Users/m1/m4-metal-917/Metal.xctoolchain/usr/bin/metal-nt`.
The default883 compiler crashed on these BTNF inputs;917 succeeded. Neither
helper executes a GPU workload.

Mixed float/integer case2 now passes clear/partial draw/reload through
Zink/lavapipe and M4: `20260912T231419550352Z-float-uint-mrt`, peak55.5 C.
The native120-byte EOT differs from the existing mixed float/R8 program only
in its second typed-store selector. Compiler unit tests verify that complete
program against the retained Apple witness. Case3 remains under development.

### Three-target EOT and attachment bindings

`mrt-attachment-probe 3` now passes RGBA16F/RG16F/R16_UINT clear, partial draw
and reload on M4 (`20260912T232433342510Z-triple-mrt-general`, peak55.5 C).
Cases0/1/2 and the existing HDR clear/draw/blend/reload oracle also passed
`20260912T232514651135Z-mrt-general-regressions`.

The EOT compiler now generates typed imageblock instructions from attachment
count rather than patching a fixed dual-target text. Native two-target120-byte
and three-target164-byte outputs match byte-for-byte. Descriptor/clear/offset
arrays grow with the active attachment count; shader acceptance validates ABI
metadata instead of whitelisting binary lengths. The packet unit suite passes
6841 checks. This qualifies the listed formats up through the three-target
layout; it does not claim arbitrary MRT formats or eight-target hardware tests.
Blender's host replay proceeds past attachment setup, but API4004 now exposes
a separate fragment-varying state restriction. No live Blender qualification yet.

### Point sprites and generated coordinates

`point-coord-probe.c` checks a shader-sized 16-pixel point, generated coordinate
gradients, both coordinate origins and both provoking-vertex conventions with
a flat vertex input. Build with `cc -O2 point-coord-probe.c -lEGL -lGL -lm`;
run hardware only through gpu-experiment.py and run-m4-gl.py. Host Zink/lavapipe
and M4 pass all four pixel checks (one-byte tolerance). Evidence:
`20260912T233209116887Z-point-size-output-select`, peak55.5 C. The native
OUTPUT_SELECT packet must preserve vertex point-size export bit18; otherwise
these points produce no fragments. Point coordinates come from the rasterizer,
not a vertex UVS slot. The saved Blender host shim replay now records all nine
frames without a rejection; that shim result executes no GPU work.

### G16G graphics feature reporting

G16G now advertises the implemented geometry and fragment storage paths without
an experimental feature override. Vertex storage retains Honeykrisp's upstream
DRI opt-in policy (introduced for vertex timeout reliability on M1); the G16G
chip restriction no longer suppresses that opt-in. Other HAL200 chips retain
their existing feature boundary. This is not a full Vulkan conformance claim.
`20260913T002518030119Z-graphics-features-default-geometry-fragment` passes
vertex and odd-size fragment storage/atomics with exact 64-byte guards, indexed
geometry amplification and variable geometry emission; peak55.75 C. Only the
vertex test sets `hk_enable_vertex_pipeline_stores_atomics=true`.

`vulkan-replay-software.py --validation-layer PATH` enables the explicit Khronos
validation layer for retained host lavapipe replays and records its manifest
and library hashes. `--remove-unsupported` is a separately requested cross-device
diagnostic adaptation; it is retained in the command manifest and is not proof
of unmodified support. Blender's splash-free reference and native captures
replay with validation without reported VUIDs. Firefox's retained native capture
needs the explicit adaptation and external-memory rebinding on lavapipe.

`fragment-image-probe cpu-bgra` exercises client-memory GL_BGRA readback,
complementing the PBO case. Both host Zink/lavapipe and native M4 pass all4096
pixels (20260913T003710162106Z-firefox-cpu-bgra-readback, peak57.25 C).
This does not reproduce the remaining Firefox screenshot-only R/B swap.

`firefox-probe.py --freeze-animation --readback-trace /absolute/tracer.so`
freezes future requestAnimationFrame callbacks after the observation period
and retains actual GL readback bytes alongside screenshots. The freeze is an
explicit diagnostic, not a performance or unmodified application claim.
Build `gl-readback-trace.c` with `cc -shared -fPIC -O2 -ldl`; it wraps both EGL
lookup and explicit glibc dlsym lookups used by Firefox, passes original GL
arguments unchanged, logs GL3/GLES3 pack state, and saves at most16MiB per
client-memory read when M4_GL_READBACK_TRACE_DIR is set. PBO reads are logged
without dereferencing buffer offsets. The helper sets/retains the isolated
output directory and tracer hash; no global preload is installed.
Native frozen Firefox run004409 returns swappedRGBA bytes before PNG encoding.
`vulkan-color-blit --alternate` checks eight alternating BGR/RGB resource
lifetimes. CPU-calculated byte expectations pass on lavapipe and M4 (004722,
peak55.75 C); this excludes the simplest descriptor-format reuse case.

`dmabuf-readback-probe [MODIFIER] [SAMPLES] [renderbuffer]` renders a known
four-quadrant colour pattern into EGL-imported ARGB/ABGR GBM images. It checks
RGBA/BGRA readback through CPU memory and PBOs; SAMPLES=4 resolves actual shader
output into the imported image first. It uses /dev/dri/renderD128 and must run
under gpu-experiment.py plus run-m4-gl.py. Compile with EGL/GL/GBM libraries.
Linear and Apple-tiled imports, texture and renderbuffer attachment paths,
and multisample resolve all pass the CPU-computed colour oracle (005235/005420,
peak55.75 C). No host software DMA-BUF qualification is claimed.

`half-select-oracle.py --output NEW` generates the static WebGL coordinate
selection reduction with Python binary16 reference values. Run vk-buffer-oracle
with M4_ORACLE_FLOAT16=1 to query and enable shaderFloat16 explicitly. The
256-lane oracle checks both alternatives, negative/transformed coordinates,
conversion and division across2048 words. The old cached half-select encoding
redirected low-half destinations to their upper neighbour; guarded run
20260913T011206399346Z-half-select-destination-fixed matches all2048 words (peak55.75 C).
`webgl-readback.html` draws four fixed colours and records four synchronous
WebGL pixel reads in documentElement.dataset.m4Probe. firefox-probe retains
that JSON in its page metadata; normal profile settings are preserved.

`HK_G17P_COMPUTE_TRACE=1` emits one recording-time line per native compute
launch: CDM, Ioto, shader/root/set addresses, register/scratch use and grid.
It avoids mapping shader BOs and dumping graphics streams. Correlate the last
record for a fault-owned CDM address; command-pool reuse may repeat addresses.
`g16-fault-dump.py decode DUMP --scheduler` also decodes optional compute
command/mapping/UMA records added by kernel89161783ee5c. Collection was verified
in012356/job1992: bootstrap/client physical pages agree and the hardware UMA
slot points at the current pool. These snapshots do not prove TLB contents,
retirement, or timeout resolution. Old dumps still decode unchanged.
`webgl-readback.html?read=0&animate=1` omits early point reads and redraws on
requestAnimationFrame. The runner records frame count and explicit mode.

`vk-buffer-oracle` also accepts diagnostic M4_ORACLE_FILL32=0x12345678 or
M4_ORACLE_COPY=1 (mutually exclusive). Copy moves the first half of the input
buffer to the second, requiring two equal word-aligned halves. In these modes,
the groups-x argument is the repeat count; the positional SPIR-V file is read
but no shader pipeline is created. Each submission waits for its fence and
checks the first CPU observation against expected.bin. Inputs remain unchanged
between repeats, so these are serial transfer/retirement checks, not proof of
independent per-iteration writes or asynchronous lifetime safety. Native32-repeat
fill013519 and copy013647 pass all4096/8192 words; the copy generates the same
138-byte internal compute shader found in Blender's fault records.

`dmabuf-readback-probe MODIFIER 4 blit` resolves into the imported image then
blits into ordinary RGBA8 before checking CPU/PBO reads. `blit-prefetch` adds
an initial read and a checked BGRA read before that blit. Both pass with the
Apple tiled modifier0xc00000000000001 (015534/020245, <=56.25 C). These exclude
the simple imported BGRA-to-RGBA blit from Firefox's remaining capture-only issue.

`webgl-readback-check.py GL_READBACK_DIR --output NEW.json` compares every saved
640x480 GL_RGBA read with the independent four-quadrant oracle. It retains
hashes, error counts and a failing exit status; requires NumPy, executes no GPU.
The identical continuous-draw fixture passes79 lavapipe readbacks and the normal
M4 snapshot (020017), while Vulkan capture enabled produces307200 swapped pixels
(015254). Disabling disk cache also passed, but the no-capture/cache-enabled
control excludes that as an established fix. Do not disable caching globally.
Normal Firefox MDN sample6 now has correct orange/blue page and compositor
colors with hardware WebRender and WebGL1/2 via Zink/M4 (020103,58.25 C).
A requestAnimationFrame freeze was used only to compare the final snapshot;
three seconds of animation ran first. Capture-only behavior remains unresolved.

`eevee-image-probe.c` isolates Eevee storage operations through Zink. Modes
`fetch3d`, `float`, `half` (R16F), `uint`, `3d`, and `array` compare 256 texels against exact CPU
inputs. Compile with EGL/GL; run only through the mandatory GPU experiment guard
on M4. `array` is an outstanding qualification case. The software reference
accepts llvmpipe only with `M4_TEST_LLVMPIPE=1`.
`macos-native/pbe-state.py --kind 3 --depth N --layer-stride B` extends the
pinned offline constructor witness to volume descriptors. Requires Unicorn;
executes no GPU commands and does not establish hardware correctness itself.


`compiler-corpus.py CAPTURE/compiler --output NEW_DIRECTORY` replays every
retained compiler bundle offline through compiler-replay.py. It retains each
input/binary hash, source patch and diagnostic log, and emits a short JSON
index of the first unsupported instructions. It never executes GPU code.
Use it to iterate on a captured application without repeated GUI launches.
A compiler pass is not evidence of rendering correctness.

Eevee image-probe diagnostics: `array-oob` also checks invalid layers. Array
controls accept `M4_TEST_WIDTH=32` or `M4_TEST_LAYERS=1`; the default is 8x8x4.
`M4_TEST_2D_LAYER_VIEWS=1` writes each array layer through a separate 2D view.
`M4_TEST_UPLOAD_ONLY=1` skips dispatch and checks uploaded reference data.
Neither control qualifies the array-write instruction.
`M4_TEST_SAMPLE_READBACK=1` reads the 8x8x4 result via array texelFetch into an
SSBO. Record all selected modes in the experiment evidence.

`native-ioto-shaders.py CAPTURE/encoded --shader-va 0x10000000440` locates
GPU0 LDSHDR candidates for a known native shader and reports the adjacent
SPECTPR staging allocation, source offsets and SHA256. It runs offline and
scans records, not control flow; f1 is reported without semantic interpretation.
The retained M4 array write has f1=2 and 64 staging bytes. Neither changing
f1 alone nor matching that allocation alone fixed the packed-write failure.

Eevee image probes also cover `atomic` and `atomic-array` (R32UI atomicAdd
memory effects), plus `hdr`/`hdr3d` (R11G11B10F storage). HDR readback compares
packed RGB bits directly, avoiding a separate format-converting 3D blit. The
selected quarter-integer inputs are exactly representable in each channel.
Hardware atomic probes 042124/042233 and HDR3D 044242 passed all 256 texels
on J713; this does not qualify ordinary packed array writes or complete Eevee.

`array-lod-probe.c` uploads distinct texels at four mip levels/four array
layers, then samples a varying layer and LOD in a fragment shader. All 128
pixels compare exactly on llvmpipe and guarded J713 run 20260913T044705
(57.5 C). Compile with EGL/GL; run through gpu-experiment.py and run-m4-gl.py.
Native PACK_SMPIR must combine both values into bank6; separate packs overwrite
one another. This test checks reads, not the outstanding array-write path.

`array-lod-probe 3d` tests implicit 3D sampling with distinct XYZ texels and
the sampler clamped to mip0. J713045830 passes128pixels at58.25C; this is
not a cross-mip derivative qualification. The native AIR-NT32023.883 fragment
SMP is retained at build/eevee-native-sample3d-fragment/text-1250.bin+0x90.
Eevee045848 completed a192x144,16sample Vulkan render in9.6s, but the image
is black against the lit software reference. Completion is not correctness.

`compute-indirect-probe.c` validates GPU-written XYZ dispatch group counts,
non-unit local sizes, gl_NumWorkGroups and zero counts on all three axes.
Build with `cc -O2 compute-indirect-probe.c -lEGL -lGL`. Use run-m4-gl.py
inside gpu-experiment.py on hardware; software control accepts
M4_TEST_LLVMPIPE=1. gpu-experiment.py now classifies both the outer command
log and the retained application/application.log from the same experiment;
a successful render marker cannot conceal Vulkan errors in the child log.

`layered-geometry-probe.c [depth]` checks geometry-produced layer routing
with four exact-color or exact-depth array layers. Build with `cc -O2 FILE
-lEGL -lGL` and run inside the watchdog/thermal guard on M4. The llvmpipe
control passes both modes. Native depth currently reaches the kernel
one-layer rejection; native layered color also needs BG/EOT support.

`native-kernel-function.py IMAGE SYMBOL --output NEW_DIRECTORY` extracts one
exact ARM64 Mach-O text symbol and disassembles its file-backed bytes. It saves
the image and function SHA256, symbol VA, file offset, bounded size and command.
Use this for offline native-driver investigation; addresses are translated
through Mach-O segments. Requires llvm-nm and aarch64-linux-gnu-objdump.
It does not execute GPU code or modify the input image.

`g16-layer-state-test.py --kernel linux/linux-asahi --output NEW_DIRECTORY`
compiles the retained native-derived C and Rust state encoders and compares
layer modes, metadata pointers and fragment state across 1/2/4/2048 layers
and both empty-tile modes. Retains binary outputs and JSON result; offline
encoder agreement is not hardware qualification. Tracked with kernel tools.

`mrt-attachment-probe 3 sparse` discards RT0/RT1 between draws and checks that RT2 survives at its original tile offset. `3 mixed` clears RT1 while reloading RT0/RT2, then checks all attachment pixels. Build with `cc -O2 mrt-attachment-probe.c -lEGL -lGL -lm`; run only through `gpu-experiment.py` and `run-m4-gl.py` with the mandatory watchdog and thermal guard.

`fragment-stencil-probe [depth]` verifies all1024 stencil, depth and color pixels with a varying shader stencil reference; `depth` also exports fragment depth. Compile with `cc -O2 fragment-stencil-probe.c -lEGL -lGL`. Run only through `gpu-experiment.py` and `run-m4-gl.py`; set `M4_PROBE_OUTPUT={output}` to retain raw stencil/depth arrays.

`eevee-image-probe.c`: `M4_TEST_COORD_COLORS=1` uses XYZ-derived colors instead
of SSBO input for the ordinary `array` test. `M4_TEST_PACK_READBACK=1` with
`M4_TEST_LAYERS=1 M4_PROBE_OUTPUT=DIR` is a diagnostic for a separately retained
compiler staging override: saves `pack-payload.bin` and checks only B/A control
channels. Its DIAGNOSTIC result never qualifies normal array rendering.

`patches/gfxreconstruct-sparse-dynamic-attachments.patch` fixes resource dumps
with absent or null dynamic color/depth attachments, retaining real attachment
locations and before/after matching. Apply atop the existing local replay fixes
and rebuild `gfxrecon-replay`. Qualified on the five final Eevee sample draws
14998/15033/15055/15067/15097 in retained app-6918.gfxr. The separate index-buffer
dump path crashes in FindMinMaxVertexIndices for this capture; retained gdb
backtrace in build/eevee-fresh-dumper-backtrace-v2.log. Select image/descriptor
dumps with DumpVertexIndexBuffer=false until that diagnostic path is repaired.
This changes collected evidence, not replayed application commands or features.

MRT probe case4 matches Eevee's five formats (RGBA16F, R32UI, RG16 UNORM,
two RGB10A2), with raw RG16 readback to avoid a float-format conversion blit.
Case5 isolates RG16 alone. Both pass the independent llvmpipe expectations.
The initial M4 case4 fails RG16 in clear/draw/reload while all other targets
pass; use these bounded reproducers instead of repeatedly launching Eevee.

MRT cases5..8 exercise RG16/RGBA16 UNORM/SNORM, including additive tile-load
blending and raw 16-bit readback. SNORM disables GL read-color clamping. The
software references and M4 all pass after normalized tile conversion, correct
native normalized16 EOT selection and two-lane W16 tile-load support.

`vulkan-dump-compare.py REFERENCE ACTUAL --descriptors --output REPORT.json`
compares dumped descriptor buffers and image subresources as well as attachments.
It records binding identities, hashes and nonzero/differing byte counts, rejecting
metadata mismatches. These diagnostics do not assign floating-point tolerances or
qualify undefined regions. Only explicitly dumped subresources are compared.

`eevee-image-probe.c`: array tests accept `M4_TEST_LAYERS=16` and
`M4_TEST_ARRAY_RGBA16F=1` (use four layers for exact quarter-integer half floats).
`M4_PROBE_OUTPUT=DIR` retains actual.bin and expected.bin even on mismatch;
float RGBA or integer RGBA words, except packed HDR words. Guarded example:
`gpu-experiment.py ... -- env M4_PROBE_OUTPUT={output} ... eevee-image-probe array`.
All Linux GPU execution still requires the watchdog and thermal guard.

`array-lod-probe.c` with `M4_TEST_IMAGE_WRITES=1` fills an immutable array's
four mips/four layers on the GPU, verifies all 340 texels, then checks 128
varying-LOD samples. Default mode retains the earlier upload-based test.

`vulkan-draw-dump.py TRACE.jsonl --dispatch INDEX --inputs --all-subresources
--output CONFIG.json` selects a primary compute dispatch and its actual submit.
Use either dispatches or draws; vertex/index buffer dumps are now opt-in with
`--vertex-inputs` because that upstream dump path crashes on the saved Eevee trace.
`vulkan-dump-compare.py --descriptors` also compares dispatch descriptors and
before/after writes, including all explicitly dumped layers/mips. As usual,
raw byte differences are diagnostics, not floating-point tolerance judgments.

`cube-compute-probe.c` samples 16 directions on each of six cube faces at two
constant LODs in compute. Distinct face/texel patterns and SSBO readback check
192 samples exactly, without a render-target export. Compile with `-lEGL -lGL`
and run only through `gpu-experiment.py ... -- python3 /root/run-m4-gl.py
/root/cube-compute-probe` on M4. The host software oracle uses
`M4_TEST_LLVMPIPE=1 GALLIUM_DRIVER=llvmpipe LIBGL_ALWAYS_SOFTWARE=1`.

### Depth gather and consecutive image writes

`depth-gather-probe.c` checks 64 compute gathers from R32F, D32 and D32S8,
including clamp-to-edge. Build with `cc -O2 -Wall -Wextra depth-gather-probe.c
-o depth-gather-probe -lEGL -lGL`. Set `M4_TEST_GATHER_STORES=1` to route each
gather into four consecutive R32F image writes; `M4_TEST_QUERY_SIZE=1` uses
textureSize for normalization. Run hardware only through gpu-experiment.py.
The four-store mode reproduced an IMGWCI completion-slot mismatch: before
the fix 128/128/153 words differed, afterwards all three formats were exact.
`eevee-image-probe.c float` additionally accepts `M4_TEST_QUAD_WRITES=1`,
which writes four adjacent texels per invocation from independent SSBO loads.
That mode alone did not expose the dependency bug; retain the gather case.
`vulkan-shader-probe.py --descriptors` compares replaced compute shader
dispatch resources against the same diagnostic shader on lavapipe. Shader
replacement is diagnostic adaptation, not application qualification.

### Shadertoy page diagnostics and rematerialized vertex outputs

`firefox-probe.py --page-script /absolute/read-only-diagnostics.js` retains
the diagnostic JavaScript and its JSON result after navigation. Use it to
distinguish site verification/error pages from a working WebGL canvas; never
create a context just to claim one exists. Existing guards/cleanup apply.

XdsGDB investigation20260913: initial Firefox aborts on saved WebRender
vertex bundle398b8af55d77. RA rematerialized1.0 between outputs47/48 after
the pre-RA output bundling pass. Allow ordered wide exports separated only
by MOV_IMM and retire their staging COPYs before GPR reuse. Contiguous tails
keep batched waits. Offline replay now compiles6446bytes; guarded092843
Firefox survives15s with WebRender/WebGL reportingZink/M4. Page remains
Cloudflare verification, not Shadertoy content or shader qualification.
Thirteen focused output tests pass; three older SixVertexOutputs tests fail
identically on unmodified99b3302b66d and are recorded as baseline failures.
An ordinary-profile40s run092942 faults the GPU later; snapshotjob8887
retained and watchdog recovery completed. This fix is not a stability claim.

`vulkan-image-dimensions.c --depth24` checks exact low-24-bit depth words
through D24S8 mip1 uploads, partial updates and padded readbacks in 2D and
2D arrays. Unused high bytes are ignored; meaningful bytes and padding are
compared. Use the mandatory experiment guard on hardware.
`depth24-normalization.c` exhaustively checks the host arithmetic model for
all 2^24 values against correctly rounded double division and exact round trips.
Build with `cc -O2 -ffp-contract=off tools/depth24-normalization.c -lm`. This
CPU model complements, and does not replace, guarded GPU transfer checks.
