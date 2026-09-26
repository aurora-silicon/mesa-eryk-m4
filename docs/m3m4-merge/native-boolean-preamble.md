# Native Boolean preamble exports

Uniform Boolean decisions can now leave the native preamble. Reserve one
aligned 32-bit word per Boolean component, store an explicit 0/1, and restore
the Boolean with an integer nonzero comparison in the main shader. The native
encoder still uses only its qualified 32-bit uniform writes. Narrow memory
reads, floating-point arithmetic, and 64-bit exports retain their restrictions.
The existing native/memory preamble options remain off by default.

## Validation, 2026-09-26

- All 298 host backend tests pass. The new Boolean-export regression test fails
  on the old implementation and passes with this change.
- Twelve hot compute and twelve hot fragment compiler inputs replay successfully.
- `tests/m3m4-preamble/boolean-uniform.c`: all 32,768 U32 readback values match
  the CPU reference on M3 and M4, across 32 changing UBO updates through Zink.
  The first probe used an integer `mix` unavailable in GLSL 330 and failed shader
  compilation before drawing; corrected component selects pass on both chips.
  M3's follow-up guard completed without reboot; the failed attempt is retained.
- Six pinned robust out-of-bounds UBO CTS cases pass on both chips (the same
  cases in the previous read-only qualification).
- Saved Satisfactory Vulkan gameplay frames 1/16/31 match the reference RGB
  exactly on both. Same capture, replay adaptations, 3840x2160 output,
  2880x1620 internal resolution, and 30 completed measured frames.

| Machine | Read-only baseline median FPS | Boolean candidate repeats | Median FPS |
| --- | ---: | --- | ---: |
| M3 Pro | 4.93492 | 4.95829, 5.00254, 4.99878 | 4.99878 |
| M4 | 3.50686 | 3.55974, 3.55027, 3.56010 | 3.55974 |

This is a small replay improvement, not live-game FPS or a sustained 5 FPS claim.
Adding compute 64-bit exports to this change was exact but slower on M3 and is
not included. Global compute code motion and narrow compute loads were also
neutral/slower and reverted. Every hardware run retained the watchdog and
thermal guards; desktop installations are unchanged. Complete logs, shaders,
and image evidence remain in the private local benchmark repository.
