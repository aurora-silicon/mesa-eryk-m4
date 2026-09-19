# Factorio frame architecture diagnosis, 2026-09-19

User target: 5 ms/frame at unchanged quality. Not achieved. Latest separate
normal-menu observation averaged 41.041 FPS; this diagnostic is not live FPS.

The offline API audit distinguishes restored command buffers from submitted
ones. Frame CB28 has 257 draws across 17 API rendering passes. CB23/955 are
restored but not submitted. Four clears become compute jobs, leaving 13 actual
render passes. Setup and readback must not be counted as application frames.

HK_ARCHITECTURE_PROFILE is default-off submission/stream logging. Combine it
with HK_M4_SHADER_PROFILE and kernel SubmitTiming for a bounded diagnostic.
The uploader address differs from the kernel VDM alias; gpu-frame-cost.py
joins through HK_SHADER_SUBMIT, selecting an explicit ASID and matching exact
address lifetimes. Mismatches are excluded. Profiling perturbs throughput.

Uncompressed replay 070430510825Z: two actual frame submissions have 27.745 /
28.642 ms total fragment execution and 0.555 / 0.603 ms CPU submission. First
TA start to final fragment end spans 33.644 / 35.410 ms, including intervening
compute and scheduling. Compressed allocation adaptation 070504687842Z has
25.066 / 27.921 ms fragments and zero changed pixels against retained reference
88594c10407e243eb0617860b8f2abc1f06c0844ec67ddba3900875d7e36796e.
The compression-enabled replay did not actually allocate compressed render targets:
the retained trace requests HOST_TRANSFER usage, which disallows compression.
These two replays therefore do NOT measure compression benefit. A fresh capture
of the current application is required for representative performance work. Both replays retain normal draw and
shader content, offscreen swapchain adaptation and memory rebinding.

Basic native Zink fullscreen probe uses no timer queries, one triangle and
edge-pixel CPU readback. Kernel fragment spans at 2560x1664: solid 1.018-1.060
ms, texelFetch 1.835-1.974 ms, nearest sampling 1.840-1.881 ms. At 4K these are
about 2.05, 3.65, 3.62 ms. Invalidate hint before the solid draw does not improve
it (1.039-1.131 ms); an API hint alone does not establish which attachment load
operation Zink selected. Baseline behavior does not prove single-core execution
or a specific bandwidth/configuration bug. Investigate basic rendering first.

The mirrored tools are maintained in workspace-root tools/. Build GL probe
with cc -O2 -Wall -Wextra gl-frame-cost-bench.c -lEGL -lGL. Arguments: mode
0 solid / 1 texelFetch / 2 nearest sample; optional 1 invalidates old contents.
Run only through gpu-experiment.py, native Zink/Asahi environment, watchdog60s,
six thermal channels at 0.5s, cutoff95C. All six retained experiments passed;
replay peaks71/65.5C, microbench peaks61/60.5/60.5/60.25C. No full CTS run.
Mesa build passed. No execution policy is changed by this checkpoint.

## Current capture and scalar descriptor prefetch

The new five-frame capture is retained at
`/home/e/.local/share/factorio-driver/frame-architecture-20260919/menu_frames_1500_through_1504.gfxr`
on the target and `build/factorio-basic-vulkan/current-menu.gfxr` in the host
workspace. Capture072639832323Z uses the current native thunks and compression;
initialization9.564s, peak86C. Replay072903948794Z actually allocates23 compressed
images. Each frame submits67 draws,14 API passes,10 hardware render passes.
Its five fragment sums are13.906,14.312,14.362,14.227,14.121ms; spans from first
TA to last fragment18.831..19.136ms, CPUsubmission0.363..0.392ms. This includes
instrumentation and excludes setup; it is not live application FPS.
Compiler capture073451384544Z retains all inputs; peak77.25C.

The simple texture shader c23b3f45 still loaded a16bit sampler index per
fragment. Descriptor prefetch matched16bit data but rejected non-word-sized
loads. Fetch containing words while preserving halfword offsets. The existing
command encoder validates expanded spans against descriptor-set bounds. No
application-specific shader matching. Twelve new host cases cover low/high
halfwords and vectors crossing words, before/after address lowering.
Host tests and ARM64 build pass. Candidate075304344092Z replay is pixel-exact
against the original current-capture screenshot, SHA256
12a681058bb24f962dbb8bed2a69a00e0858924cc138577884561ee27f8b2ba2.
Five fragment sums13.003,13.925,13.739,13.764,13.705ms; total render spans
18.236..18.608ms. This modest improvement does not reach5ms/frame.
Candidate installed SHAede1f5947a54e11785e07aa22e13c11486c855a9cbaeb1e3e919442dd13227de.

Further basic controls: directVulkan clear/load-discard both ~1.05ms for solid
2560x1664 output; full pixels checked. Discarded-store diagnostic ~0.265ms
(no correctness claim). Compressed solid ~0.317ms. RGBA16F solid~1.85ms.
Compute SSBO stores073723597492Z check every outputword;2560x1664 ~0.79..0.87ms,
4K~1.55..1.62ms from native24MHz stamps, peak63.25C. This suggests investigating
memory throughput more generally, but is not a peak-bandwidth measurement.
The GL probe adds mode3 compute stores and optional wide-format selector;
fragment probe adds4096 extents, discard-load and diagnostic-only discardedstore.

Focused sampling oracle075353187879Z: all six PASS, peak63C (ordinary, border, minmax and minimumLOD). Full CTS remains deferred.

## Persistent system cache and throughput checks, 08:41 UTC

Bootloader e38bf64 enables T8132's previously disabled system cache (native
multicast enable and all four plane status checks). See m1n1 docs/m4-system-cache.md.
The same current replay remains pixel-exact and takes 14.41..15.06ms from
first tiling start to final fragment end, with 11.53..12.06ms fragment time.
This is still above the user's 5ms target.

Normal-menu run 083626135065Z retained unchanged settings, native thunks,
shader options and invisible Gallium HUD. Initialization 7.820s. 117 samples
over 60s: mean46.704FPS, median47.47, range17.6..51.48; final12 mean47.638.
No sample >=59. Peak86.25C, watchdog/thermal guard PASS. Only once-per-second
SubmitSummary is enabled (debug_flags16777216); per-command SubmitTiming is off.
An earlier debug_flags0 attempt removed the guard's retirement progress signal
and caused a watchdog reset. Its incomplete evidence is retained under082939;
no driver-fault conclusion follows from that reset. Keep Summary for long tests.

The expanded GL probe accepts mode, discard, format, words-per-invocation,
local-size, arithmetic-count. Mode3 validates every output word; arithmetic
uses four distinct F32 FMA chains and compares every component against fmaf.
Build with -lEGL -lGL -lm. An explicit glFlush after the opening timestamp
prevents timestamp batching from collapsing both endpoints. The measured
interval includes the submission boundary and is NOT pure shader execution.
The two earlier unseparated-query runs produced identical endpoints: their
timings are explicitly unqualified in throughput-matrix.json.

At3840x2160, warm rounds1..5 mean query spans: pure vector store1.472ms;
64FMA per component1.526ms;256FMA1.911ms (4.247billion F32 operations).
All values matched the CPU oracle. Workgroups32/256/512 and4/16words per
invocation remain around1.44..1.47ms for stores. This rules out a large gain
from those simple dispatch-shape changes in this benchmark. It suggests
memory traffic as a stronger lead, without proving peak DRAM bandwidth or
a specific hardware configuration bug. Matrix/report files preserve exact
results. All six separated-query experiments passed bounded guards.

## Preserve page-contained vectors in alias lowering

The alias pass split aligned vector loads/stores into separate scalar address
translations, even when alignment proves every byte stays in one16KiB page.
Keep the original vector and write mask in that case, translating its base
once. Unproved/page-crossing accesses keep the old piecewise path.
Host sparse suite passes, including384 new vector cases across widths, masks,
ordinary/aliased addresses and absent/present leaves, plus previous crossing
and sparse-mask cases. ARM64 release build passes. Candidate checked4K stores
remain~1.46ms; no significant throughput gain is established.
Hardware40 strict buffer oracles PASS; current five-frame Factorio replay PASS
and frame5 is pixel-exact against cache-qualified reference12a681... Peak64.25C.
Normal application qualification follows; no fullCTS claim.

Normal-menu qualification085146495638Z passes, peak88.75C: mean41.95FPS,
median40.25, final12mean39.637. The animated scene differs from the previous
46.704FPS run; this is not a controlled before/after comparison. No5ms claim.

Fragment versus compute arithmetic probe: at4K the fragment shader executes
four times as many invocations as the words4 compute test. For256FMA per
component this is16.987billion versus4.247billion operations. Fragment wall
time for warm rounds2..5 averages6.579ms; compute query span1.911ms. These
include different overheads and cannot be compared directly as shader-only
time, but do not establish a fragment-specific arithmetic throughput defect.
All arithmetic outputs checked (fragment edgepixel, compute everyword). The
probe adds optional read_each=0 to keep compute output GPU-resident until the
final readback; this did not improve measured throughput. Fragment256FMA
output is UNORM-quantized and checked with one-unit tolerance.

## Controlled replay and tile-budget diagnostics, 09:04 UTC

Kernel debug_flags25165824 restores per-pass timestamps while preserving
watchdog progress. Before-vector085938 and after-vector090012 replay exactly
the same capture with cache disabled only for shader compilation. Both are
pixel-identical to the qualified reference. Before fragment sums11.60..12.73ms,
render spans14.50..15.65ms; after11.56..11.98ms and14.42..14.95ms. No large gain.
Use experiment-local kernel-live.log for joins: whole-boot journals contain
previous ASIDs and can reuse exactly the same command-stream addresses. The
new controlled-replay helper requires one unambiguous complete five-frame
match, a guarded PASS and pixel-exact final image.

HK_M4_MAX_TILE_SAMPLES diagnostically lowers the single-sample G16G tile
budget (256/512/1024); default2048 and other sample counts remain unchanged.
The common allocator and epilog generator carry the resulting geometry.
16x16 run090338: fragment11.96..12.37ms, spans15.28..15.69ms. 32x16 run090359:
fragment11.38..11.89ms, spans14.49..14.98ms. Bothpixel-exact, guardsPASS,
peak64.75C. No worthwhile gain established; retain32x32 default. NoCTS claim.

## Compressed mip chains, 09:20 UTC

The same capture with compression disabled takes22.69..23.03ms, versus
14.42..14.95ms with it enabled; both are pixel-exact. This confirms a large
memory-traffic cost, unlike the older HOST_TRANSFER trace.

The independent pinned 25G83 PBE constructor at0x33620c combines metadata
address and mip selection in word2. Its level2/7-level byte witness is retained
and tested in the host descriptor suite. Extend the G16 2D compressed PBE
codec accordingly. HK_M4_COMPRESSED_MIPS=true opts into compressed mipmapped
RGBA8/BGRA8 single-layer images; default allocation policy is unchanged.

Host descriptor and300374 core-codec checks PASS. Hardware oracle091628
checks all output bytes across144 copies:9 mip levels256x256 through1x1,
RGBA/BGRA directions, compressed/uncompressed source and destination, and
partial level0 rectangles. It does not qualify untouched other levels or
cross-level copies. GuardPASS, peak72C.

Current Factorio replay091943 allocates the8192x2560 atlas compressed, keeps
all final pixels identical, and spans14.26..14.94ms. No meaningful performance
gain or5ms claim. This is useful opt-in format support, not a default change.

Read-only MCC occupancy probe090935 observed GPU-workload-associated DSID31
reach16384 cache lines on each aperture while the checked compute workload
ran. This is occupancy, not a hit-rate or bandwidth measurement. DSID-to-quota
group mapping remains unqualified. No cache-policy registers were written.

## Memory floor qualification and per-draw diagnosis, 10:40 UTC

Boot initialization b60940d now enables the AP memory/fabric floors using
validated stock table indices. See m1n1 docs/m4-memory-performance.md. The
unchanged five-frame replay spans 8.08..8.11 ms with pixel-identical output.
A bounded normal main-menu run has median 59.99 FPS, final 12 samples mean
60.0 FPS. The 5 ms replay target remains unmet.

HK_M4_PROFILE_EACH_DRAW=true splits render work at each draw while retaining
attachments. It is a diagnostic only: extra stores/reloads change workload
cost. The 67-pass replay is pixel-identical, but spans about 18 ms. The largest
draw is the 4,690-quad sprite batch (2.05 ms FS in the split diagnostic); its
main shader has 106 instructions and no pixel interlock or coverage writes.

A separate experiment prefetched immutable root fields into uniform registers,
using the existing bounded descriptor allocator. Replay stayed pixel-identical
at 8.10..8.14 ms: no useful gain. The patch is retained as a negative experiment
and removed from active source. Neither diagnostic changes default behavior.

The observe helpers now preserve explicit guest exit codes and partial FPS on
failure. A prior profiled application run exited with swapchain-killed output,
without a recorded GPU fault; its cause is not established. The successful
non-profiled repeat does not prove profiling caused that failure.

## Covered fragment counters

HK_M4_COUNT_FRAGMENTS=true reads completed fragment-only API query pools at
destruction. With no API statistics query active, it adds a separate guarded
counter per non-meta draw through the existing statistics prolog, capped at
4096 records per command buffer. Counts accumulate across repeated executions
until reset. Zero cannot distinguish unsubmitted from fully uncovered work.
New diagnostic atomics and barriers change timing; never use that path as a
performance baseline. Existing API queries are preserved.

The current replay already has fragment statistics enabled for all draws.
Retired API counters report 49,448,124..49,470,745 covered samples per main
frame, plus a separate 40,000-sample query. The 2512x1552 window has 3,898,624
pixels. Final replay pixels remain identical; the 5 ms target is not reached.
The exact-count GL oracle passes all dimensions, edge pixels, query-buffer
reads and split submissions with ordered GPU availability. Firmware-completion
availability produces incomplete CPU-visible counts even with diagnostics off;
this separately reproduced synchronization failure is being fixed next.

## Statistics availability visibility fix

The completion-timestamp shortcut advertised fragment statistics before their
shader-atomic result was visible to the CPU: a 2560x1664 fullscreen draw
returned 1,479,072 instead of 4,259,840 in the retained failure. Disabling only
that shortcut restores exact counts. Keep ordered shader-cache flushes before
the GPU availability write, including pools using the optional u64 status
layout. Ordinary timestamp-query completion fanout is unchanged.

The fixed u64-status path passes exact counts, odd edge sizes, readback-buffer
results, both query-end orders and queries spanning multiple submissions.
Five original replay frames remain pixel-identical at 8.155..8.189 ms. This is
a correctness fix, not a performance gain. Default installed driver has not
yet been replaced in this checkpoint; candidate asahi-query-visibility.json
contains the fix. Full CTS remains deferred.

## Rejected counter striping experiment

Sixteen cache-line-separated counters preserve replay query totals and every
image pixel, but slow the frame: XY tile hash 8.66..8.76 ms; simplified integer
X stripe 8.48..8.53 ms versus 8.155..8.189 ms unstriped. The additional prolog
work outweighs any contention benefit. The patch is archived and removed from
active source. The X oracle also exposed a split-submission one-pixel query
returning 1 instead of 2, despite passing larger exact counts; this is not a
fully qualified query implementation. Investigating the statistics prolog's
missing writes-memory annotation separately. No default stripe mode was set.

## Track writes from the statistics prolog

A colour-only main shader can still execute the separately linked statistics
prolog, which atomically writes query memory. Include these writes in the
render stream's memory-dependency tracking, so the existing G16 publication
barrier also covers counters carried across render passes or submissions.
The exact-count oracle passes; the full replay keeps every pixel and counter
total identical. The additional required boundaries cost about 0.4 ms: spans
8.546..8.628 ms. Further optimization must preserve those dependencies.
