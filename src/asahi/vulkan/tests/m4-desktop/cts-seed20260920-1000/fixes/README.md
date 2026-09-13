# Seed 20260920 corrections and retests

## Nested SPIR-V blocks (case 36)

The historical compute generator decorates both outer_struct and input_buffer
as Block, although input_buffer contains outer_struct. Add a single-member
block wrapper for the separate inputA/inputB buffers and leave outer_struct
undecorated. Both original input layouts, binding numbers, pointer selections
and output comparisons are preserved. No Mesa validation is relaxed.

The separately named deqp-vk-seed20-block includes this correction and the three
previously documented historical CTS corrections. Build hashes and cumulative
patch are retained. Case 36 passes on native M4 with the unchanged baseline
Mesa, producing the expected output. Guard evidence:
_provenance/experiments/20260913T205424440238Z-cts-seed20-block-fix
(60-second watchdog, peak 57.75 C, unchanged boot). The original crash remains
in the first-attempt baseline; this adapted CTS pass is recorded separately.

## RGBA16 normalized storage images (cases 690 and 860)

Enable UNORM and SNORM RGBA16 in native bindless storage lowering and select
the existing formatted F32-lane write ABI; the PBE descriptor controls signed
or unsigned normalized conversion. Both original CTS cases pass with expected
output on M4. Evidence: _provenance/experiments/
20260913T205635734916Z-cts-seed20-normalized-store (60-second watchdog,
peak 57.5 C). Original Vulkan commands and output checks are unchanged.

## Optimizer operand legality (cases 766 and 933)

Fold immediate fmov conversions before allocation, preserving negative zero;
retain explicit zero-extension of half-width ballot predicates and only fuse
comparisons with native operand shapes. Saved compiler bundles replay
successfully offline, and both original CTS pixel comparisons pass on M4.
Evidence: _provenance/experiments/20260913T205911233931Z-cts-seed20-compiler2
(60-second watchdog, peak 57.5 C).
