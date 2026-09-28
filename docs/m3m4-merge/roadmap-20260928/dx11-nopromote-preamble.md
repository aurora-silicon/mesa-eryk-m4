# Satisfactory DX11 memory preamble with nopromote

The descriptor-prefetch comparison stopped before enabling prefetch: its
baseline combined native read-only memory preambles with AGX_MESA_DEBUG=nopromote.
Earlier exact preamble checks used ordinary constant promotion (empty compiler
flags). The new combination fails exact image comparison on both GPUs.
Original M3 maximum errors at frames 1/16/31: 90/56/36. Original M4: 0/15/50.
A warm M4 repetition changes 6,227/325,977/157,157 pixels relative to its first
run; it also fails the retained reference. Successful execution is not an
image pass. No timings were approved and no tolerance changed.

M3's arithmetic-only preamble control (memory hoisting disabled), with the same
nopromote flag, matches frame 1 exactly. Full ordinary nopromote without native
preambles had already matched all three reference frames on both GPUs during
cold guard qualification. The memory transform is being bisected, with an
independent cache-disabled M4 repeat to separate cache/state effects. This is
an open correctness issue, not a demonstrated descriptor-prefetch regression.


## Isolation checkpoint

The single-frame M3 search first reproduced the failure with all 744 selected
sources and with the lower 372 sources. Subsequent subset checks were exact.
The final inferred source also passes when explicitly selected and captured;
a repetition of the formerly failing 372-source group is exact. Consequently
this bisection does **not** establish a responsible shader. The failure is
intermittent or depends on additional state/transform interactions. Do not add
a shader exclusion or treat the inferred source as a regression reproducer.

The interrupted step 20260928T163255332807Z was a separate test-guard defect:
an unchanged old retirement timestamp replaced more recent verified idle/CPU
progress just as new GPU work started. The host activity remained healthy.
Published M3 tools 91e0e40a3a78 preserve the newer eligible time, retaining all
stall/deadline/thermal gates. The corrected actual-branch regression rejects
the former assignment. Resumed cold segments complete without reboot.

Offline extraction now handles inline VkShaderModuleCreateInfo in pipeline
stages and validates all 1,020 SPIR-V modules from the capture. Compiler inputs
and shader binaries remain private with the game capture; public diagnostic
tools live under docs/m3m4-merge/tools/.


## Release-order hypothesis

Full-scene checks still reproduce the error after M3 recovery. M3 frame errors
are 10/154/59; M4 with AGX_MESA_DEBUG=nopromote,wait has 0/68/152. Thus forcing
instruction waits does not eliminate the discrepancy. Kernel guards complete.

The graphics Ioto builder emits its terminal PIXREL before STOP. Native
preamble insertion formerly replaces only STOP, leaving PIXREL ahead of the
new uniform calculation/writes. An isolated diagnostic moves that existing
release after the native preamble and before its STOP, without changing either
shader instructions or the release mask. This is a release-order hypothesis,
not yet a proven fix. Candidate Vulkan SHA256:
18bcee4b55528d1f8f1a3bc5ad9552e31d7b077698756e4b5f8d3ddd13138220.
Both full image checks complete but still fail: M3 maximum errors 203/81/84;
M4 27/79/226. Moving PIXREL alone is not a fix. The temporary source patch
was restored and the ordinary library rebuilt; the diagnostic and images are
retained privately. Ordinary desktop installations are unchanged.


## Stage and independent uniform controls

Two additional ordinary nopromote full runs per GPU match all three reference
frames exactly. M3 arithmetic-only preambles also match all three. M3 with
memory motion admitted only in fragment shaders still fails (33/33/32 maximum
errors); M4 with memory motion admitted only in compute shaders matches all
three. That M4 control first exhausted the 170-second bound during CPU pipeline
compilation, with GPU idle/healthy; its cached retry completed. The original
CPU timeout remains retained and is not a GPU fault or successful render.

The frame-31 shader search reproduces discrepancies for 760, 380 and 190 selected
sources. Complement verification is now required after a passing subset; no
single source is identified yet.

The independent `gl-uniform-wide-probe.c` fixture uses 256 U32 UBO inputs, each
combined with a varying before accumulation. Across 32 changed UBO updates,
all 32,768 RGBA integer readback values match CPU arithmetic on llvmpipe and
M4. M4 compiles 480 uniform halves, 14,036 preamble instructions and no scratch.
This excludes a blanket large-uniform/preamble-capacity failure for this shape;
it is not qualification of conditional, derivative or game shaders. M4 evidence:
`20260928T171643587324Z-dx11-wide-uniform-oracle`. Guard verdict pass, full thermal
telemetry retained; M3 has not run this new fixture yet.


## Multi-tile and dependency controls

The frame-31 search fails successively at 95/48/24 sources, but each 12-source
half passes. A repeat of the combined 24-source selection fails again (777
changed pixels, maximum 32). Both searches are therefore inconclusive for
single-source attribution: this may be an interaction or intermittent behavior.
The M4 generated-source-only cold run exhausts the compilation bound with GPU
idle and healthy; it provides no image verdict. Its CPU ceilings remain
1.8/1.86 GHz despite requesting the normal policy maxima; this is recorded,
not silently treated as a full-clock performance sample.

Disabling fragment register-cache hints does not fix the images. Changing
replay memory translation to rebind also leaves differences (maximum 0/36/48
at frames 1/16/31). These are negative controls, not production changes.

The generic AGX_DBG_WAIT flag does not serialize native AGX3 instruction
emission. An additional diagnostic explicitly drains outstanding native
scoreboard slots before each preamble instruction. M3 completes, but still
differs at frames 16/31 (maximum 24/31); preamble serialization alone is not a
fix. All temporary compiler changes were restored after building the isolated
candidate. No compiler optimization default or reference tolerance changed.

The wide-uniform oracle now accepts bounded `--size WIDTH HEIGHT` and
`--rounds N` arguments. At 3840x2160, four changing UBO updates check
132,710,400 integer values exactly on **each** M3 and M4, with native memory
preambles and nopromote. Both pass; the main fragment uses 480 uniform halves,
75 GPR halves and no scratch. This broadens the earlier tiny framebuffer check
to thousands of tiles. llvmpipe independently passes two 4K updates. It still
does not qualify conditional, derivative, or game shaders.

Evidence: M3 `/root/m3-wide-uniform-4k-20260928`, M4 experiment
`dx11-wide-uniform-4k-oracle`; both retain watchdog and all-channel thermal logs.
The fixture source is `src/asahi/compiler/test/probes/gl-uniform-wide-probe.c`.


The `--gpu-updates` fixture mode replaces CPU buffer uploads with a compute
shader writing the UBO through an SSBO alias, followed by
`GL_UNIFORM_BARRIER_BIT`. The CPU oracle remains independent. M3, M4 and
llvmpipe each pass four 4K updates (132,710,400 values), testing compute-to-
constant-program visibility as well as CPU-to-constant-program visibility.

Two further full-scene M3 controls still fail: serializing native instructions
throughout fragment shaders gives maximum errors 100/66/46; forcing retention
of quad helpers for every fragment shader gives 2/29/123. Neither change is a
fix and neither remains in source. Boolean-producing preamble operations are
the next isolation target; the memory-only flag also makes their input loads
eligible, so its failure does not yet identify a memory-instruction defect.

M4's initial low CPU ceilings were observed with boost disabled. That alone
does not establish why they were low: ordinary OPPs reach 4.104 GHz without
boost, and QoS sysfs updates are asynchronous. An independent, temporary boost
test exposed a guard bug: the ordinary OPP list excludes
boost frequencies. The guard skipped applying the requested 4.104 GHz cap,
then rejected the effective 4.464 GHz ceiling before the workload started.
That guard failure triggered watchdog recovery. Kernel/tools commit
7e9e787edea6 consults the hardware maximum before skipping a cap; 15 host tests
and a guarded 4K GPU-written UBO run pass. The actual requested ceiling is now
applied without relaxing any thermal/watchdog/progress checks. Boost is restored
after the experiment. The original incomplete record is retained.

A later boost-toggle repeat is correctly rejected at guard startup because
the requested cap never becomes effective. Local cpufreq code provides a
possible explanation: the software boost callback rewrites policy min/max,
while an unchanged aggregate QoS request need not schedule a refresh. This is
an open kernel-policy issue, not a GPU failure or proof of that hypothesis.
No workload starts on the rejected run, and no cap check is bypassed. Further
GPU runs leave boost disabled and record the effective clock ceilings.

## Repeated subgroup and pipeline-switching checks

A 24-source selection reproduces the frame-31 error on M3 (231 changed RGB
pixels). Its lower 12 sources pass three repeats; the upper 12 pass twice then
fail with 1,424 changed pixels. Therefore one exact result cannot exclude a
source group. On M4 the combined 24 pass twice; no cross-chip attribution is
inferred. These are isolated diagnostic-frame checks, never timing qualification.

The wide-uniform CPU oracle now supports `--programs 1..4`, cycling fragment
programs that use 64, 48, 32 and 16 uvec4 inputs. With compute-written UBOs,
4K output and eight rounds, llvmpipe, G15S and G16G each match all 265,420,800
U32 values exactly. This qualifies the exercised program/footprint transitions,
not all game shaders. Native memory preambles remain opt-in. M3 evidence:
`m3-four-program-uniforms-20260928`; M4:
`20260928T180752639155Z-wide-four-programs-4k`.

The partial-coverage variant (`--partial-coverage`) renders viewports from
1 to 127 pixels wide/high at 4K, checking both the written pixels and cleared
background. llvmpipe, M3 and M4 again match all 265,420,800 values. This covers
partial pixel quads and tiles with changing uniform footprints.

Further negative control: restricting native preamble motion to top-level NIR
blocks still fails the full M4 check (frame maxima 1/65/62). It is not a fix.
Conversely, allowing only `load_global_constant`/`load_constant_agx` memory
operations matches all three frames in two M4 runs and one M3 run; ordinary
bounded UBO loads are excluded by this diagnostic. This narrows the relevant
transformation class but does not resolve or qualify full memory preambles.

## Physical lifetime hints for uniform writes

The native uniform COPY used the SSA `kill` bit. Normal register allocation
replaces SSA indices and clears that bit; the subsequent physical-register
analysis writes `discard`. Route the latter to the COPY encoder, preserving
the existing rule that only the final component may discard. This restores
last-use hints; it does not establish an image-corruption root cause.

A two-export regression keeps the source alive across the first export and
marks it dead after the second. It covers one through four components and
both compact/extended uniform destinations. The old encoder misses the
physical discard; all 219 AGX3PackBinary tests pass with the correction. ARM64
Vulkan builds. On M3 and M4 the four-program, 4K, GPU-written-UBO oracle matches
all 265,420,800 values. The complete M4 game check still fails (maxima 79/48/108)
and is retained as a failure, with no timing approval. Preambles stay opt-in
and desktop packages are unchanged. Candidate library SHA256:
152a2b5f00c922bf6ed29a1fd46721e84ff3706096ad4410a1dd52f97d4a80b0.


## Single-source and queued-draw isolation

Repeated source selection now reproduces the discrepancy on both GPUs with
only fragment source `031ea01afc88a023deef3fcd327278eda00c6249f4d264147a1cdd84dd1ef39e`
eligible for memory motion. Its compiler inputs are retained privately. This
is an actual reproducer, unlike the earlier unverified bisection inference;
it does not prove this is the only affected source.

After the post-RA uniform-discard correction, selected-source checks still
fail: M3 maximum 2 in 2 pixels, M4 maximum 44 in 1,379 pixels. Permitting only
the first bounded UBO load plus constant-address dependencies also fails on
M3 (maximum 7 in 161 pixels). That scalar supplies texture LOD bias, but it
is **not** established as the cause: excluding all bounded loads while
retaining constant loads passes once, then fails (maximum 4 in 27 pixels).
The earlier unbounded-only passes therefore do not exclude that path.
Individual constant loads and their resulting arithmetic are the next boundary.
All selections are isolated frame-31 diagnostics, never timing qualification.

The independent uniform oracle now offers `--queued-draws`. It prepares distinct
aligned UBO ranges, optionally via compute writes, then queues up to 32 striped
draws with changing ranges and four program footprints. A single final readback
checks every pixel, without a CPU readback between draws. At 3840x2160 and 32
draws, llvmpipe, M3 and M4 each match all 33,177,600 U32 values. This closes a
gap in the earlier per-draw-readback tests but is not a game-shader qualification.
Host checks also retain the partial-coverage mode and cover uneven stripe
heights with CPU-written inputs. M3 evidence: `m3-queued-uniforms-20260928`;
M4: `20260928T184914284503Z-queued-wide-uniform-4k`. Watchdog and thermal guards
remain enabled. Diagnostic replay/library identities remain with each record.


### Four-instruction reproducer and draw readback

Admitting only constant-load definition 22, while disallowing preamble ALU,
reduces the selected preamble to one 16-byte descriptor read, two IR uniform
exports (four native scalar COPYs), and STOP. The new uniform range is halves
48..55; preamble scratch is zero and its GPR footprint is eight halves. This
still fails on both GPUs: M3 2,789 changed RGB pixels, maximum 70; M4 228,
maximum 11. Therefore long preamble arithmetic is not required to expose it.
The identical candidate with no admitted loads gives three exact M3 repetitions
and then two changed pixels, maximum 2; that small control discrepancy remains
recorded. It is not classified as a perfectly stable baseline.

Adding three plain NOPs after the load wait, matching the resource loader's
four-NOP drain shape, still fails on M3 and M4 (maximum 22 / 44). Moving the
small preamble's GPRs from halves 0..7 to 32..39, with corrected reported
footprint, also fails on M3 after two exact runs. The main shader's 56,896 bytes
are byte-identical across these three candidates (SHA256
`ad34fda1f247abd6ed7f778cb626cd1dd6f13f48dddf455c651394e3883f0d85`).
These are negative diagnostics, not production changes.

Resource snapshots surround the first and last selected-shader draws in replay
frame 31 (global calls 9,284,712 and 9,284,939). All five color attachments agree
byte-for-byte with the no-load control, both before and after these draws, even
in a repeated run whose final image differs in 434 pixels (maximum 24). The
remaining search must cover earlier frames and subsequent processing rather
than assuming these final-frame draws directly contain the bad values. The
native centroid/nonlinear-derivative smoke probe at 31x31 passes on both GPUs;
it does not qualify every interpolation pattern. Original raw attachments and
all diagnostic patches are retained in the local-only benchmark repository.

### First-frame repetition and a captured material-buffer difference

The discrepancy does not require replay history. With the four-instruction
candidate, M3 frame 1 matches 13 times, then differs in 1,319 RGB pixels
(maximum 118). M4 matches ten times, then differs in 1,983 pixels (maximum
247). The identical M4 binary with no admitted loads matches all 20 first-frame
repetitions. These bounded repetitions improve isolation, not a statistical
qualification or proof that the control can never fail.

Twenty M3 first-frame replays with before/after readback around draw 406589 all
produce exact final screenshots. However, repetition 13 contains a difference
in three intermediate color attachments: 276 pixels in the rectangle
[1792,800]..[1841,831], totaling 2,384 changed bytes. All before-draw attachments
match. The differing pixels were unchanged by the reference draw; this is not
proof they were outside its coverage, since overdraw may reproduce the same
values. The emissive attachment and the fifth, write-disabled attachment match.
This localizes one observed discrepancy to the selected draw even though later
processing hides it in the final screenshot. Evidence:
`20260928T192617708770Z-m3-satisfactory-dx11-gameplay-check`.

A diagnostic candidate that limits native ioctls to one hardware command,
adds synchronous queue waits, disables CDM merging, and disables early tiling
matches all 20 first-frame M3 repetitions. These simultaneous changes only test
an ordering hypothesis; none is a proposed performance fix. Source shaders and
game settings are unchanged. The candidate is not the desktop installation.
M4 validation and a descriptor-equality diagnostic remain in progress.

The raw comparison helper now reports before-draw byte differences explicitly
and rejects asymmetric or truncated before-draw snapshots. Three offline tests
cover those cases. All game readbacks remain private in the local benchmark
repository; no assets or raw game attachments are published with Mesa.

### Follow-up controls: ordering and descriptor witness

The single-command/synchronous/early-tiling-disabled candidate also matches all
20 first-frame M4 repetitions, but full 31-frame replays still fail: M4's first
run differs in 1,604 pixels (maximum 54); M3 matches once, then differs in 696
(maximum 45). Thus the first-frame results do not establish an ordering fix.
Clearing only the preamble uniform-copy source-discard hints also fails on M4
(156 pixels, maximum 8). This changes two instruction bytes and leaves the main
shader byte-identical. Advertising the maximum preamble GPR allocation without
changing shader instructions fails too (579 pixels, maximum 17). In particular,
zero and eight GPR halves both round to the minimum group in the baseline state
encoder; the maximum-allocation experiment is not proof of an allocation bug.

An instrumented shader reloads the descriptor in the main program and compares
all four words with the preamble uniforms. It exports 2.0 to an otherwise zero
emissive alpha channel on disagreement. Twenty repeated readback runs report no
markers; twenty additional runs comparing all five attachments also match.
Instrumentation changes the main shader and can mask the original issue. These
negative runs do not prove that the unmodified shader always receives correct
descriptors. A strengthened positive control flips one bit of the fresh read
before the actual comparison (rather than forcing its result): 2,877 pixels
contain the expected marker. Final rendering ignores this alpha channel and
still matches. Positive-control evidence: `20260928T195554035967Z`.

A separate 511x257 triangle probe checks centroid-derived nonlinear fine
derivatives and independently calculated partial-triangle coverage. It passes
on llvmpipe, M3 and M4 with checksum `f84e845a938bc6a8`. It exercises edge/helper
behavior more directly than the earlier full-screen smoke test but does not
qualify all interpolation patterns in the game.

Disabling asynchronous pipeline creation (`--pipeline-creation-jobs 0`) still
fails the first full M3 replay: 210 changed pixels, maximum 24. The M4 run reaches
the 170-second bound during CPU setup and produces no screenshot; it is retained
as incomplete, not an image result. The runner records this override as an
unscored diagnostic without changing frozen capture metadata. Pipeline setup
serialization is not a demonstrated remedy.

Native memory preambles remain opt-in. No diagnostic above replaces the normal
desktop driver or constitutes a new qualified performance result.

### Isolating the appended preamble's address input

A private replay-specific candidate replaces the exact 36-byte preamble with
an existing Ioto descriptor range (set 2 + 48 bytes, 16 bytes to uniform halves
48..55). It verifies the original preamble bytes and root layout before making
this substitution. It is an isolation experiment, not a general compiler or
application fix. The main shader is unchanged. Ten full M4 replays match exactly.
M3 matches five times, then differs only at (x=2,y=1121/1123): the same two RGB
changes seen in the earlier no-admitted-load control, byte-for-byte. This small
baseline difference remains open; it is not discarded through a new tolerance.

Copying the uniform pointer into GPR halves 8..11 before the original descriptor
load does not fix the issue: the first M3 replay differs in 14 pixels (maximum
8). The compiler reports 12 preamble GPR halves and no scratch. The main binary
still has SHA256 `ad34fda1f247abd6ed7f778cb626cd1dd6f13f48dddf455c651394e3883f0d85`.

Another private candidate materializes that same bound descriptor-set address
with the existing Ioto immediate loader, then executes the register-addressed
load and original uniform exports at the end of the constant program. Ten full
M3 replays match exactly. M4 repetitions remain in progress. This narrows the
investigation toward the appended program's input path; it does not establish
a synchronization contract or justify embedding application-specific addresses
or shader signatures in production code. The exact patches and binary hashes
are retained locally, alongside all negative results.

### Entry-delay hypothesis (not a production synchronization contract)

The direct-address M4 variant first matches twice, then produces exactly the
same two-pixel baseline image described above. Ten additional repetitions
include nine exact images and one identical baseline image; no larger material
difference appears. Every difference remains recorded. These runs are
unscored and do not relax the benchmark's exact image policy.

Eight plain NOPs before the selected compiled preamble give six exact M3
repetitions, followed by that same baseline image. This differs from the earlier
negative three-NOP experiment, which delayed uniform exports after the memory
load rather than delaying the first read of loader-written uniforms. Immediately
returning to the original candidate reproduces 640 changed pixels (maximum 43),
so the original fault has not simply stopped occurring during the session.

With the entry delay and the full memory-preamble optimization enabled, three
M3 final-frame diagnostics and then ten complete replays match exactly. Each
complete replay checks frames 1, 16 and 31. M4 qualification and a same-binary
NOP-count experiment remain in progress. The latter reserves an unchanged
amount of program space even at zero NOPs to separate instruction delay from
allocation-layout effects. A delay experiment alone does not establish the
hardware's required synchronization or justify a universal timing workaround.
The default desktop preamble policy is still unchanged.
