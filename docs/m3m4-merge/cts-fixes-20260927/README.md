# Vulkan regression fixes, 2026-09-27

## Transform feedback metadata overflow

The full M4 catalog crashed at index 3071413,
`dEQP-VK.transform_feedback.simple.max_output_components_128`.
The core showed shader serialization reading corrupt pointers after 128 scalar
XFB descriptors had been copied into a 64-entry array. The release build removed
the assertion that was the only bounds check.

Size the storage for all NIR varying components, including both 16-bit halves,
and check the bound in release builds before copying. Increment the shader
binary version because the serialized metadata layout changed.

Candidate library SHA256:
`add52a02148b99f1b0568dc6c932dd40c58aeb71f23a441d1b72b28138b9c507`.
M4 isolated crash reproducer: Pass. All 7,891 simple transform-feedback cases:
4,988 Pass, 2,903 NotSupported, no failures/crashes. Watchdog and thermal guard
passed, peak 69.25 C. The original full-catalog crash remains in its results;
these are separate regression results, not overwritten outcomes.

Host evidence: `build/cts-fixes-20260927/m4-xfb-{fix,neighbors}` and
`_provenance/experiments/20260927T103948885650Z-m4-xfb-metadata-neighbors`.
