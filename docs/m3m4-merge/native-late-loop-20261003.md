# Opt-in late loop optimization on G15/G16

`AGX_NATIVE_LATE_LOOP=true` runs the existing NIR loop optimization once
on the fully lowered AGX3B shader, then restores SSA and performs scalar
cleanup. Its pipeline/disk cache flag is bit 60. The default is false; AGX2
and AGX3C compilation are unchanged. The offline compiler replay also accepts
`--opt-loop` to inspect this transform without opening a GPU.

The same-SPIR-V arithmetic diagnostic exposes a constant initial exit test
only after lowering. The original native shader reloaded its 1024-iteration
bound inside the loop. NIR peeling moves the first test outside the loop;
the existing backend then materializes that bound once before the loop.
Zero-trip semantics are preserved by NIR's outer conditional. No new ISA
encoding, application shader, workload setting or image tolerance is added.

M3 qualification, on one ordinary boot with FMA uniform operands enabled:

- Three alternating pairs: control GPU medians 0.894083 / 0.893834 /
  0.893875 ms; late-loop medians 0.834208 / 0.834333 / 0.834000 ms.
  This is about 6.7% less GPU time for this arithmetic probe. Every output
  word is checked against CPU `fmaf` before and after measurement.
- The 128-MiB read/write copy control is unchanged within variation
  (1.993333 versus 1.997000 ms).
- Minecraft's original first/middle/last images match exactly in both
  configurations. Nine alternating completed 30-frame pairs give medians
  163.9454 and 164.1715 replay FPS (+0.14%). This is not a material game
  improvement and does not qualify enabling the option by default.
- 972 frozen-index Vulkan 1.4 CTS cases all Pass: every GLSL generic/special
  loop, loop demote/discard/return/switch/derivative controls, descriptor
  loops and the WebGL compute loop. Case bounds remain 15 seconds inside
  45-second experiments, with the real 60-second watchdog and all-channel
  thermal protection. No failures or missing selected results are hidden.
- All 314 backend compiler unit tests pass with the option enabled.

Catalog SHA256: `72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
Selection SHA256: `c0c3ac147aea4c7fa6dd4fde4ae4e4a661b5a6e5412423eadd0878430ebac076`.
Private Vulkan candidate SHA256:
`5f87f50eb6b4723684c73ee1bbc50bde17858facd4db002c373cde1abba37e09`.
Exact source patch and transferred hashes are retained by the staging tool.
Desktop Mesa remains unchanged. M4 hardware was not tested for this option;
these targeted checks are not a full conformance or native-performance claim.

Raw evidence stays in the private benchmark directory
`diagnostics/m3-macos-driver-gap-20261003/throughput/late-loop-*`.
