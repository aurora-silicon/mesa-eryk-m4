# Branch around scaled-format conversion on native GPUs

Enable the existing `HK_NATIVE_BRANCH_SCALED` lowering by default. Image-view
format metadata is uniform within a draw; ordinary floating-point samples do
not need both signed and unsigned integer-to-float conversions used by scaled
formats. The branch retains the same scaled-format implementation and descriptor
checks. The existing override and shader-cache discriminator remain in place.

Saved Satisfactory Vulkan gameplay replay, same capture, 30 measured frames,
3840x2160 output / 2880x1620 internal, offscreen, original adaptations:

| Machine | Baseline median FPS | Candidate median FPS | Candidate repeats |
| --- | ---: | ---: | --- |
| M3 Pro | 4.54075 | 4.60076 | 4.58549, 4.62046, 4.60076 |
| M4 | 3.21023 | 3.24920 | 3.24019, 3.24958, 3.24920 |

This is a small improvement (~1.3% M3, ~1.2% M4), not 5 FPS. Every RGB
pixel in frames 1/16/31 matches the retained baseline on both machines. Normal
clocks, normal non-instrumented boots, watchdog 60 s and temperature guards.
M3 baseline and candidate ran on boot 5faeff66-3e0c-4a38-931a-14c012650626;
M4 boot 7b02370e-2a9b-445b-935f-db0f87b80aa7. No desktop configuration changed.

Checks: M3 20260926T033430619358Z, M4 20260926T035315702569Z. Timings and
source patch retained in the private benchmark repository. This qualifies the
saved adapted replay, not an unmodified game or broad Vulkan conformance.
