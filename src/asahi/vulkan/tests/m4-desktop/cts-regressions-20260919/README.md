# September 19 CTS regression fixes

The full regression run was stopped at the user’s request after 393,459
terminal results: 221,905 Pass, 171,526 NotSupported, 20 Crash, four Fail,
and four QualityWarning. Original results remain in
`build/cts-vulkan14-regression-20260919`; targeted reruns never replace them.

## RGB attachment clears

The 20 recorded RGB8/RGB32 crashes all pass with the clear-format padding fix.
Native image allocation pads RGB to RGBA, but attachment clears supplied the
logical three-component view format to the physical memory fill. Apply the
same padding to the clear format, preserving mutable-view numeric semantics.
Candidate ICD SHA256: 10e4f34e53604d530497118cdcab7c7894d108e9825800e047f046de3168d61a.
CTS binary: pre-existing `deqp-vk-seed30-scaled-blit` (corrected regression
baseline, not an unmodified Khronos conformance submission).
One worker, 60-second watchdog, thermal cutoff 95°C; guard passed, peak 75°C.

## Copies spanning descriptor bindings

All 289 descriptor-copy cases pass, including both failing four-image immutable
sampler cases. A copy can span bindings with separately aligned storage; advancing
by the first binding's stride corrupted descriptors. Walk source and destination
bindings independently and also select their actual dynamic-buffer indices.
Evidence: `20260919T213815066818Z-cts-fix-descriptor-copy`, guarded peak 73.25°C.

## Packed-float multisample attachments

Remove the obsolete single-sample restriction for R11G11B10_FLOAT and
R9G9B9E5_FLOAT in both layout admission and M4 format properties. Existing
multisample tile addressing handles the packed four-byte texel representation.
All 588 packed-float clear cases completed:400Pass,188NotSupported; all44
2x/4x cases pass. Both originally failing format-property tests now pass,
as do both descriptor cases in the same final candidate. Host tile-layout
checks cover both formats at1x/2x/4x, including two attachments.
Full IOTO host unit suite exposed an older golden wait-mask mismatch from
eef4e004551 (expected08, actual04); tracked separately, not a full-suite pass.

## Allocation callback warnings

The four QualityWarning results remain unchanged. Common Mesa
vk_descriptor_set_layout_multizalloc and vk_pipeline_layout_zalloc deliberately
allocate reference-counted layouts using the device allocator and DEVICE scope,
since their internal lifetimes can exceed the application-visible object.
The CTS per-create callback injection therefore observes no allocations.
This is documented common runtime policy, not four rendering failures; do not
change allocation lifetime or hide these results solely to silence a warning.

## Host test expectations

Two byte fixtures missed the earlier eef4e004551 wait-mask correction. Both
loads signal completion slot3, so the wait mask is1<<(3-1)=4, not8 derived
from the register data source. Updating only these two fixture bytes makes
the full IOTO program suite pass7009 checks; uniform-program suite passes19267.
No shader/firmware bytecode was changed for this fixture correction.
