# Frostpunk memory-preamble regression

The M3 broad replay campaign reproduces about 17,330 changed RGB pixels per
frame (maximum channel error 27) in Frostpunk DX11 gameplay with native and
read-only memory preambles enabled. The same full package without those options,
and with arithmetic-only preambles, passes the existing reference policy. A
cold-cache repeat of the original package reproduces the failure. No reference
image or tolerance has changed; preambles remain opt-in.

Bisection identifies fragment source
`1e6eb9e054aca0ac56af230d3a751028d52393b57e3f81c02bd5ec5c28a8cd6f`,
also implicated in the earlier buffer-address regression. Excluding memory
motion for that source restores the sampled image. This is diagnostic evidence,
not a shader-specific production workaround.

## Dependency controls

Correctly rebuilt, shader-selected controls for source-register lifetime waits,
scoreboard slot reuse, explicit dependency waits and full serialization all
retain the discrepancy: approximately 17,340 changed pixels, maximum 27.
None is a demonstrated fix. Full serialization also covers the main shader.

An earlier diagnostic build series falsely appeared to pass: a stale object
still contained the previous shader-exclusion experiment after source had been
restored. Its undefined symbol table still referenced `nir_shader_bisect_select`
although the restored translation unit did not. The same-build no-change
control exposed this problem. Those results remain retained but are invalid
for diagnosing waits. Explicitly rebuilding the affected translation unit
restored the failing control. Always wait for a diagnostic build to finish
before restoring source; rewrite restored source with a fresh timestamp and
rebuild the affected objects. A clean Git tree alone does not validate an
incremental build's provenance.

Qualified diagnostic binary (dependency variants): SHA256
`e2c3a3f4ef5fab062c6fe24aff389adeb62975f989520edd88b19eda30ab9a73`.
Full-serialization binary: SHA256
`aebc820536a3648e471a5cd815e35b98967867b4aeb4fb385c205865b6cda941`.
Private compiler captures, diagnostic patches and original/invalid/corrected
results are retained in gpu-replay-bench. No diagnostic default or normal
desktop installation changed. All runs retain watchdog and thermal guards.

## Memory-read boundary

A selected-source diagnostic cumulatively admits memory definitions by their
indexed pre-preamble NIR definition. The last matching limit is 694 (8 changed
pixels, maximum 1); limit 695 reproduces 17,346 pixels, maximum 27. This permits one
additional scalar 32-bit `load_agx`: definition 694, reading UBO byte offset 132 after
native sparse-address translation. Its value contributes to the Y texture
coordinate. The surrounding arithmetic remains per-lane. These are diagnostic
frame 1 results, not a full-capture qualification or a production restriction.

The two retained host compiles have identical floating-op counts in their
lowered main NIR, but different address/control-flow and register allocation.
The complete failing compiler input has 404 uniform halves, 64 preamble GPR
halves, zero preamble scratch and no promoted-constant rodata. That rules out
an overlap with a promoted-constant range for this shader. The compiler replay
tool now prints those resource boundaries alongside its existing statistics;
its generated shader binary remains byte-identical to the retained baseline.
Disabling preamble float comparisons also retains the error.

## Additional isolation controls

Disabling register-cache hints, instruction scheduling or floating move folding
retains the image error. Disabling all backend optimization cannot render this
input: the compiler refuses an unlowered register-shift `extr`, and replay
aborts at pipeline creation. This is an inconclusive diagnostic, not a GPU hang;
the guard subsequently allowed watchdog recovery after the failed command.
The following boot is `83fdd197-a255-45d8-b01d-f1ab64afb228`; no firmware fault
snapshot was recorded. The normal desktop returned.

Clearing the original-coverage marker on the backfacing special-register read
also fails (17,541 pixels, maximum 27). An unconditional control-flow push/pop
at main-shader entry fails (17,341, maximum 27). Shifting all generated uniform
slots by 8 or 16 halves fails as well (18,233 / 17,344 pixels, maximum 27).
None of these temporary changes is included in the production compiler.

A second diagnostic duplicates definition 694 as a volatile main-shader read,
compares its bits against the preamble value, and substitutes a large coordinate
on inequality. Compiler inspection confirms both reads and the comparison remain.
At the 695 boundary, this restores the sampled image (9 pixels, maximum 1), but
it also changes uniform allocation and restores address control flow. With all
memory motion admitted, the discrepancy remains (17,328, maximum 27). This does
not establish a bad UBO value or a repair; it narrows the search toward the code
changed by that instrumentation. All results use the existing fixed reference.

## Draw boundary and normal-buffer isolation

Frame-indexed resource snapshots identify pipeline 117877, secondary draws
42253, 42257 and 42261, submitted at 42721. All four attachments match before
the first draw. Only attachment 1 (A2B10G10R10, the normal output) changes:
509 pixels after draw 42253, then 1,541 and 2,008 after the next two draws.
The other three attachments and the three fragment UBO snapshots match exactly.
This localizes the discrepancy before subsequent lighting/postprocessing.

M4 cold-cache replay matches (8 changed pixels, maximum 1). Its captured compiler
key is identical to M3's; its frontend NIR adds generation-specific texture-LOD
bias correction. Both compile a 5,897-instruction, 64-half-register preamble
without scratch. The M4 main shader has 16 bytes of scratch versus M3's zero.
M4 is therefore not merely avoiding the problem through a preamble fallback.

Explicit fine/generic derivative emulation retains the error (17,340 pixels,
maximum 27). The normal calculation instead uses coarse derivatives; explicit
coarse quad broadcasts change the image further (37,931 pixels, maximum 80),
so neither transform is a repair. An exact nonlinear fine-derivative probe
(x*y, whose derivatives are y and x) passes on both GPUs. A first draft of the
instrumentation inserted fsub after its lowering pass and failed in the CPU
compiler; the corrected diagnostic was compiled locally from the captured input
before hardware execution. These altered shaders are diagnostic only.

## NaN isolation (investigation, not reference approval)

A source-selected diagnostic appends raw register copies after the final main
shader dependency drain, leaving its IR, scheduling and register allocation
unchanged. It routes normal RGB or alpha into attachment 2 (RGBA16F), forcing
that attachment's source alpha to 1 to avoid blending away the evidence.
Alpha readbacks match exactly. Normal readbacks identify 970 pixels where the
ordinary M3 result is NaN in all three RGB channels and the preamble result is
finite. There are no NaNs in the latter, no shared NaNs, and every other finite
pixel agrees exactly. Thus the image discrepancy does not establish that the
preamble path is the incorrect one.

An independent lavapipe replay replaces just the target fragment's attachment-2
stores with its normal RGB and alpha 1. All 970 locations are finite there too,
although normal directions differ from M3. This software replay explicitly uses
unsupported-feature removal and SPIR-V instrumentation; it is diagnostic, not
qualification of the original application or grounds for replacing references.
Investigation now follows the derivative-based normal/tangent normalization.
Explicit fine quad differences for all derivative variants still differ from
the reference (17,335 pixels, maximum 30); this is not a fix.

Diagnostic output-copy Vulkan library SHA256:
`5070dae7b8191de214c20ddfb62c7a032459f13a7e031986e98230bbc264986a`.
All original screenshots, references and guards are retained unchanged.

## Confirmed helper-lifetime bug and correction

The original reference concealed a pre-existing bug. Instrumenting the squared
normal lengths and transformed-position derivatives shows stale neighbouring
registers after the front-facing read. At pixel (3243,1945), ordinary compilation
produces a transformed-normal squared length of infinity, while the preamble
produces a finite but incorrect length. Clearing the BACKFACING SR read's
original-coverage marker preserves helpers: both compilations then produce the
same finite length (log2 approximately -18.47), agreeing with the independent
software calculation within the derivative differences under investigation.

An independent 31x31 fragment probe reads gl_FrontFacing, multiplies x*y by its
sign, then checks dFdxFine/dFdyFine against y*sign/x*sign. Both M3 and M4 fail at
pixel 30 (the first row's last pixel) with the original compiler. Both pass all
961 pixels with the corrected marker. The exact regression shader is
`src/asahi/compiler/test/probes/g16-frontface-derivative.frag`; build it with the
workspace's `tools/vulkan-fragment-probe.py --extent 31 31 --rgba 0 255 0 255`.
Use the shared `g17p_offscreen_triangle.c` fixture and TEST_DEVICE_ID 0x6030/0x8132.
All runs used watchdog/thermal guards; expected pixel mismatches were recorded
as diagnostic failures without triggering reboot. The 306 host compiler tests
also pass.

The correction extends the existing helper-retention rule for active-thread
and input-sample-mask reads to backfacing. It does not special-case Frostpunk,
change derivative semantics, or enable preambles by default. Earlier rejection
of this control based solely on the old reference was insufficient: restoring
correct helper execution legitimately changes that reference's bad normals.
Full replay validation continues before accepting a replacement reference or
publishing new performance results.

Corrected shared Vulkan library SHA256:
`4a89e7e00dc85c7579b64d6f06fbc932c22da069e5ad26490530931da745fac2`.

### Broader qualification after the helper correction

Both GPUs complete the same frozen 2,058-case selection of front-facing,
derivative, helper-invocation, demote and memory regressions: 1,898 Pass and
160 NotSupported each, no failures. Global catalog indices are preserved.
The original Frostpunk frame agrees across both GPUs and both preamble modes
within 4–15 one-level RGB differences.

Full 31-frame replay without preambles completes on both GPUs. M3 with
preambles also completes; all three saved frames agree with corrected M4
ordinary output within the unchanged sparse policy (maximum 2/255, at most
24 pixels). The corrected ordinary M4 output is a new, explicitly reviewed
regression reference, supported by the independent failing/passing derivative
probe and software normal readback; old references remain retained.

M4's first full preamble attempt does not complete: watchdog recovery changes
the boot ID from 44bef51d-c664-4021-86ff-478397d751ad to
 d1735369-3bd2-4186-ae7a-ce2b0d1c8250. Telemetry continues to 83.75 C with no
explicit firmware fault in the retained stream. Host progress, replay telemetry
and the live kernel stream stop before thermal telemetry; the cause is not yet
identified. A fresh-boot repeat with read-only process-stack collection completes,
but its final saved image differs in 95 pixels (44 above 2/255, maximum 19),
localized to x=2081..2090/y=990..1005. The first two images match. This run fails
the unchanged image policy and receives no timing approval. Keep the reset and
localized mismatch open; neither invalidates the independently reproduced
front-facing fix, and neither is grounds for enabling preambles by default.

### Repeated checks and matched M3 timing

After the correction, three ordinary M3 timings are 1.661571154,
1.663252814 and 1.662818209 FPS; three read-only memory-preamble timings are
2.432703743, 2.428856547 and 2.434135885 FPS. Medians are 1.662818209 and
2.432703743 FPS: a 46.30% increase with the same trace, binary and policy,
following one warmup per mode. These are timing runs without image readback,
separate from correctness checks. Peak temperatures were 91–98.5 C under the
user-authorized development policy. This is not a default-enable decision.

Three further M3 preamble checks and three M4 ordinary checks all match the
corrected reference at frames 1/16/31: at most 16 changed pixels and 2/255.
Four subsequent M4 preamble checks also match (at most 19 pixels and 2/255),
but do not explain or erase the earlier localized mismatch.

An ordinary M4 replay, 20260928T140413177873Z, hits its 90-second wall bound
with two screenshots retained. Its guard terminates and disarms normally;
GPU retirements continue, the boot does not change, and the replay thread
waits normally on a sync object. Peak temperature is 105.75 C, below the
120 C emergency policy, with CPU clocks reduced to 912/1860 MHz. This is
an incomplete runtime-limited check, not evidence of a GPU hang. The next
three ordinary checks complete with a 170-second replay bound; the watchdog
remains 60 seconds. Screenshot-check durations are not performance scores.

The earlier cold preamble reset remains unexplained. The last retained
process sample is in CPU compilation with reduced CPU limits; a long gap
without GPU retirements is a possible watchdog trigger, not an established
cause. The host now independently records process stacks during repeats.
No watchdog renewal, temperature threshold or image tolerance was relaxed.

### M4 timing and broader compiler qualification

M4 now completes three ordinary timings (0.800853125, 0.761814353,
0.761858402 FPS) and three preamble timings (1.221102788, 1.236407068,
1.252184638 FPS), following one warmup per mode. Medians are 0.761858402
and 1.236407068 FPS, a 62.29% guarded-workload improvement. The unchanged
power policy varies the CPU/GPU limits during these runs; this is not a
clock-normalized compiler speedup or a maximum-performance claim. Original
clock, temperature, frame-time and completion records are retained. Earlier
M4 mismatch/reset incidents remain open and preambles stay opt-in.

Offline recompilation of the isolated original Frostpunk fragment with the
helper-corrected compiler reduces main-shader instructions from 9,518 to 3,960
and scratch from 36 bytes (9 spills/9 fills) to zero with read-only preambles.
The main register count remains 191; 5,897 instructions move to the preamble,
which uses 64 registers and no scratch. This is one shader's compiler cost,
not a GPU timing measurement or an aggregate cost for the game.

The complete rebuilt dd452c4dd66 package also passes M3's 184-case memory
selection (178 Pass, 6 NotSupported) with preambles enabled. Ten alternating
fresh-cache SuperTuxKart checks, five per mode, match the retained images
exactly. They do not explain the earlier intermittent SuperTuxKart failure.

Satisfactory DX11 menu now differs from the older M4 reference by up to
123/255. Corrected M3 ordinary/preamble runs agree within 4/255 for the menu
and 2/255 for gameplay, suggesting the corrected helper semantics changed
both paths. Retain the old-reference failure and collect corrected M4 output
before approving a new reference or these timings.

### Adjacent facing and Satisfactory follow-up

A stronger helper test covers a 63x63 target with 98 triangles, alternating
front/back facing across each of 49 cells. Pair
`g16-frontface-mixed.vert` with `g16-frontface-derivative.frag`, disable culling,
and draw three vertices per instance. Both original drivers fail at pixel 7;
both corrected drivers pass every one of 3,969 pixels under their guards.
This excludes that specific mixed-facing helper-lifetime hypothesis for the
remaining small differences, rather than broadening any image tolerance.

Fresh ordinary M4 Satisfactory DX11 menu agrees with corrected M3 ordinary
and preamble output within the existing 4/255 policy. The corrected menu
reference is reviewed and old-reference failures remain retained. Gameplay
ordinary M4 and ordinary M3 are byte-exact in all three saved images, but
M3 preambles change 70 pixels at frame 16 and 54 at frame 31, by at most 2/255.
Gameplay's existing policy is exact, so this remains a failed qualification;
no gameplay timing approval or tolerance change is made.
