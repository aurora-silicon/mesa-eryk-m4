# Native M4 comparison probes

For the owned J713 / Mac16,12 / Apple M4 on macOS 25G83 only. `run.py`
checks those identities before execution. Requires the qualified DevMem v10,
ABI 6 service and the matching staged tools. Raw captures stay private under
`_provenance/asahi-0ad-20260910-native-traces/`; do not add them to Git.

Build on the target with `xcrun clang -arch arm64 -fobjc-arc -Wall -Wextra
-Werror -mmacosx-version-min=26.0 fill-probe.m -framework Foundation
-framework Metal -o fill-probe` (same for fragment-probe). Stage the sources,
binaries and g16state.py in `/private/var/tmp/m4-native-traces-20260910/`.

```
python3 tools/macos-native/run.py fill
python3 tools/macos-native/run.py fill --capture --active
python3 tools/macos-native/run.py fragment --width 1024 --iterations 20
python3 tools/macos-native/run.py fragment --width 128 --iterations 20 --capture --active
```

Fill: 4 MiB, uint4 stores, 262144 invocations, 64 threads per group, CPU a5
initial pattern, GPU 7b2da916. Both immediate and 100 ms later full reads must
pass. Fragment: Linux conditional mipmap mode 2 behavioral counterpart,
runtime uniform loop count/offset/height scale, implicit LOD, same mip colors
and sampler. Native Metal coordinates differ in framebuffer row orientation;
constant blue expectation and derivative magnitude are unchanged. Metal blit
readback follows rendering. This is not a byte-identical OpenGL pipeline.

Active captures add a finite diagnostic compute dwell. GPU/CPU concurrent
atomic flag visibility is not a documented completion mechanism and several
initial long-gate trials expired. The current 20M iteration cap ends without
CPU intervention (about two seconds in fragment captures). Record expiry and
pixel checks separately. Never treat a gated timing as an ordinary baseline.
The `executing` stage is a host protocol marker: in the fragment probe, the
compute dwell precedes the render encoder. It does **not** establish that the
fragment engine was executing at snapshot time. Validate active-command
identity before comparing power or engine state. The later pixel check proves
render completion separately. A next capture should correlate ungated fragment
work with firmware execution events.
The last allocation before `created` is always the explicitly declared output.

`g16state.py` is a private adaptation of the existing collector. Provenance and
its diff are adjacent. Unknown descriptor vtables/backlinks are not expanded.
Render workqueue and TA/fragment descriptor types, sizes, firmware and SKU
edges derive from exact 25G83 native producers, with current-boot KASLR anchor.
There are no speculative MMIO reads; SGX reads require explicit opt-in, unused
for this session. Global engine prefixes are bounded read-only firmware RAM.
Raw scheduler state is captured; a decoded firmware ktrace is still a capture
gap. Python, POSIX CPU and Metal GPU clocks must not be conflated.

On Linux use the workspace watchdog/thermal guarded experiment tools instead.

`registers.py RUN.bin [OTHER.bin]` validates register-list bounds and compares
records by address and flag, preserving workload-specific values as literal
values. It does not equate differing pointers or infer their role.

Capture audit found that the mutable AGXSKUEncoder can already describe a later
command. Final compute run 193339 has an invalid producer SKU (context 1 versus
RunCompute context 16); exclude its derived SKU references. Earlier long-gate
captures and final render captures match their command backpointers. The local
collector now checks SKU register-pointer/context identity before following
references and uses the firmware-global UAT for canonical SKU pointers. This
post-return transport improvement has been syntax-checked, not run on hardware.

Finite render capture (added after the compute-dwell audit):

```
python3 tools/macos-native/run.py fragment --width 1024 --iterations 20 --repeat 32 --capture --render-active
```

`--render-active` is mutually exclusive with `--active`. It releases the
submission event, then lets a finite batch of render passes execute while the
host pauses for collection. There is no compute encoder or shader waiting for
the collector. Increase iterations/repeats only if the capture misses execution;
compare ordinary one-pass controls too. `--repeat` is bounded to 1..256 to bound
the diagnostic, not the driver. The final readback validates only the last pass.
The command status before/after collection and final Metal GPU times are logged.
A scheduled status or a directory called `executing` still does not establish
fragment-engine activity: correlate the retained render command and firmware
execution events. Collection remains sequential and may outlast the workload.

The collector now saves the firmware ktrace RAM ring at Runtime+1e0/+1e8:
512 records of 0x48 bytes, with before/after index samples. It neither enables
tracing nor consumes or acknowledges entries. A native configuration with tracing
disabled may return no useful events. Historical entries, other clients, wrapping
and concurrent writes are explicitly retained as interpretation limits.
Decode again offline with `python3 tools/macos-native/ktrace.py STAGE --ids
0x4,0x5,0x17,0x2b,0x35`. Raw records and UAT provenance remain in the capture.

Validation before deployment: the updated Objective-C probe compiles with
`-Wall -Wextra -Werror` on the authorized compiler Mac (no GPU execution).
The Python collector compiles; the ring decoder round-trips all 222 firmware
events from the Linux timestamp failure and rejects truncated/out-of-range
fixtures. This is not native M4 execution validation. Stage the updated probe
binary, source, common header and collector on mac-m4 before using the new flags.

Render-only collection now derives resource UAT contexts from validated render
RunCommands. Render SKU records are checked against the descriptor's firmware
command address, stage opcode and context; the canonical sequence is independently
read through the firmware-global UAT, with page walks saved. The mutable encoder
copy remains diagnostic evidence and is marked invalid if it belongs to a later
command. Four existing native stage records pass descriptor-backed identity
checks; wrong addresses/contexts and truncated records fail offline checks.
Firmware state/ktrace is also sampled at `complete`, to retain retirement events.


Native tracing (validated on M4/25G83 on 2026-09-11): add `--firmware-trace`.
The runner uses native `ktrace dump` with subclasses `S0x8590` through `S0x8599`,
which enable the AGX firmware ring through Apple's registered callback. It refuses
active tracing, allows normal arbitration of inactive background diagnostics,
retains session states, and bounds tracing to 180 seconds with cleanup. Binary
trace is under RUN-trace/firmware.ktrace. Decode on macOS with `ktrace trace -R
FILE -t -N --csv`. Trace-relative nanoseconds are not the probe's CPU clock epoch.
The capture is fetched as one compressed archive; partial archives survive errors.

`trace-render.py STAGE CSV` matches event UUIDs and workqueues to independently
validated canonical render SKUs, preserving per-command events and counts. It
filters out unrelated system metadata and reports all-client quiesce reasons
separately. Native 4096-loop/32-pass fixture matches 32 fragment completion events
and every final pixel; ring snapshots contain 512 retained slots and may race
consumption. Short work may retire before descriptor capture and yield no matches.
An event6 count applies to fragment completion, not generic tiling completion.

Updated finite-render and canonical-SKU collectors have now executed on native M4.
Their earlier predeployment-only qualification above is historical. The Linux
0AD/shadow correctness goal remains open.


`power-select.py FIRMWARE_TEXT STAGE [--power-u32 OFFSET=VALUE]
[--globals-u32 OFFSET=VALUE]` runs the exact 25G83 selector and state-conversion
arithmetic offline under Unicorn (`build/display-venv/bin/python`). It requires
the qualified firmware SHA, fixed structure extents, bounded instruction counts,
and admitted code ranges. All addresses are synthetic RAM; trace emission is
disabled and pointer-authentication prologue/return effects are explicitly elided
inside the emulator. No firmware file is modified or hardware accessed.

Native active 060403 fixture reproduces performance900, duty mode0, states9/9.
Zero host caps force performance0 and mode2 with states0/1. Zero demand alone
still allows the native activity floor40. A modeled fixed-low-OPP input selects
performance100, mode0, states1/1. This validates only arithmetic: controller
updates, timers, actual pstate transitions and thermal behavior are NOT emulated.


UMA hardware-table fields previously labeled `current_page_count` and
`page_count_remainder` are now named `ring_cursor0` and `ring_cursor1`.
Native sustained rendering advances both beyond the populated page count.
Older captured manifests retain their original labels; `../uma-state.py`
decodes their raw table with corrected names without altering evidence.

## Multisample launch-state comparison

`run.py multisample --samples 2 --mask 1 --width 128 --capture
--firmware-trace` captures a constant-red triangle over a black/opaque clear.
Counts1/2/4 and an explicit mask are validated. Every output pixel is checked
immediately and100ms after completion, and both raw images are retained.
The expected red channel is round(255*popcount(mask)/samples), alpha255.

Add `--resolve-compute` for store-only MSAA followed by an explicit GPU compute
resolve. The default uses native StoreAndMultisampleResolve. Record which path
was used: these are behavioral comparison probes, not identical Vulkan streams.
Use `--render-active --repeat N` only when the submitted/complete snapshots
miss needed runtime state; multisample repeats remain finite (1..2048). No shader waits for
the collector. Output must not be called an active fragment capture without
matching command/firmware evidence. The existing collector output-allocation
and ready/release protocol is retained.

Stage multisample-probe, multisample.metal, native-common.h and the current
collector in the same remote directory used by run.py. The probe enforces
Apple M4 identity and queries native sample-count support. The runner checks
Mac16,12 and25G83. The Objective-C and Metal sources compile on the compiler
Mac with the documented warnings-as-errors command; this is not M4 execution
validation. The versioned runner mirror accepts `--workspace-root` pointing
to the development workspace so evidence is retained in its provenance tree.

The multisample repeat bound is 2048 so trivial shaders remain queued long
enough to capture descriptors (about 210 ms for the native 4x fixture).
This changes the diagnostic only. No shader gate is added. Match canonical
SKU identities with firmware completion events and check both output reads.
The private collector now polls the executing marker every 1 ms, captures
render before compute, and writes the submitted manifest before releasing
the finite render batch. Some descriptors may still retire during collection;
failed identity/backlink checks remain excluded.


## Eevee array image writes

`run.py image-array --width 8 --capture --firmware-trace` captures a bounded
native array write plus a GPU blit readback. Width 32 qualifies page-aligned
layers. The probe checks every RGBA32Float component of four layers and the
untouched padding of its 4 MiB output allocation. It uses the existing
created/encoded/submitted/complete handshake and a host-released submission
event, with no shader dwell. Stage `image-array-probe` and `image-array.metal`
in the same remote directory as the other probes. Build the Objective-C source
with Foundation, Metal, ARC and native-common.h, as for the existing probes.

Native M4/25G83 runs 20260913T031205Z (8x8x4) and 031430Z (32x32x4) passed
exact readback. Resource payloads, GPU shader bytes, PBE descriptors and the
first run's firmware ktrace are retained under the workspace provenance tree.
The native array PBE has dimension 3, layers-minus-one in word1[59:44], and
word1 bit60 changes from zero to one for the larger allocation. These are
native references; Linux array stores and Blender Eevee still fail validation.

### Layered depth witness

`layer-depth-probe.m` + `layer-depth.metal` render a full triangle to each
Depth32Float array layer, with exact depths 0, 0.25, 0.5 and 0.75. A separate
blit reads every layer; two raw readbacks and the original command state are
retained. Use `run.py layer-depth --width 128 --layers 1` for a baseline and
`--layers 4 --capture --firmware-trace` for the layered witness. A finite
`--repeat 256 --render-active` capture can retain active render descriptors.
Build with the same Foundation/Metal clang command above. Stage the executable,
Metal source and native-common.h alongside the existing collector. This probe
was compile-checked on macOS and its AIR-NT pipeline compiled for G16G; native
M4 execution is still pending as of 2026-09-13 05:32 UTC.

20260913T055736Z native depth128x128x4 passes two exact65536-value readbacks.
Add `--color` to run RGBA32Float layered color (same executable/source);
055829 passes two exact262144-component readbacks. Both retain full encoded
resources and firmware ktrace. Finite active captures055901(depth1024x1024,
256passes) and055957(color128x128,256passes) retain validated command/SKU
state. Native four-layer RunFragment+0x50 is1, not4; do not infer its meaning
from single-layer records. Compiler type3 EOT emits the same layered header
and PACK_IMGWIR sequence observed in the working native color capture.

## Completion and cache-generation captures (2026-09-15)

`run.py fill --dispatches 2 --capture --active --firmware-trace` uses two
compute encoders writing disjoint halves of the same 4 MiB output. Metal may
coalesce them into one RunCompute: count captured commands before interpreting
this as a multi-command flush experiment. Both immediate and delayed full reads
remain mandatory. The finite dwell is diagnostic, not a performance baseline.

The collector retains validated compute notifier, user/firmware stamps and SKU
bytes at execution and completion, including UAT walks for every backing page.
Completion records refer to previously validated addresses; compare UUIDs and
stamps because the allocation may have been reused. Physical SKU capture now
supports page crossings instead of assuming contiguous single-page backing.

Native M4/25G83 captures 20260915T030333Z (compute) and 20260915T030432Z
(render) passed immediate and delayed output checks. Compute notifier +8 and
StartCompute +0x34 contain generation0. Canonical StartVertex +0x4c and
StartFragment +0x64 also contain generation0. This evidence motivates checking
Linux generation matching; it does not establish Linux cache coherency.
