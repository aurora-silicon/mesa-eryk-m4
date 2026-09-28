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
