# G16G half arithmetic from Firefox startup

Native Metal source: g16g-half-sub.metal. Offline compilation on macOS26.6.1
25G76, AIR-NT32023.883, target applegpu_g16g, produced archive SHA256
5f483513bf8b8ce779104b26d000bf82e32f442bdc893e85c235846aa2f7fcda.
Text offset82 contains half subtraction 10021c0300c8 (destination2,
sources2/3, both discard, wait32). The matching add is 10021c0300c0:
instruction bit43 selects source1 negation. The packer exchanges operands
when only source0 is negated; unsupported absolute/double-negated forms
still fail closed. Original artifacts: build/firefox-native-half-sub/.

Copy propagation exposed half inputs to full-width reciprocal, mixed integer
adds, multiply-adds and shifts in a Firefox WebRender fragment shader.
Before register allocation, materialize exact float widening and signed or
unsigned integer extension. Narrow integer multiply-add uses modulo65536
arithmetic through 32-bit operations and truncation. Preserve operand order
and source1 shifts. This does not relax unsupported-instruction checks.

The retained Firefox bundle 39f57ceacf9476256a4d9ba399e0d9e14406b3cbf32fe5f1584ea920c2e2b779
now compiles offline to2842 bytes. The Vulkan capture now returns VK_SUCCESS
for pipeline creation where it originally returned VK_ERROR_FEATURE_NOT_PRESENT.
The experiment classifier calls this api_error because it sees the captured
error in GFXReconstruct's result-mismatch warning; it is an expected improvement.

Validation on2026-09-12:
- ALU encoder71 checks pass; three focused mixed-width add/shift packer tests pass.
- m4_vulkan_half_arithmetic checks32 exact outputs covering subtraction,
  reciprocal, integer multiplication/addition, signed extension and shifts.
  Both lavapipe and M4 pass. M4 evidence20260912T043637804548Z-half-arithmetic-oracle,
  maximum temperature55 C, mandatory watchdog/thermal guard.
- ARM64 library SHA256 c774d794c57c80fff11b8567449a55d2168cb662e5b8929d701e7d216f566e70.
- Full AGX3PackBinary suite aborted in an odd-register diagnostic assertion in
  a negative half-FMUL case; it is not claimed passing or baseline-qualified.
- Firefox now starts and reports M4 Zink for WebGL1/2, but browser chrome and
  canvas remain black. This is compiler/numerical qualification, not a complete
  Firefox rendering pass. Capture20260912T043957314191Z-firefox-webgl-half-arithmetic.
