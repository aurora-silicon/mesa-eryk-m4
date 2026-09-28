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
