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
