# Native signed integer lowering

Before lowering 8-bit arithmetic to 16 bits, fold sign extension of a packed
32-bit word's bytes to a left alignment and arithmetic right shift. This avoids
intermediate half-register copies and repeated extensions. Swizzles select the
byte before the rewrite; every lane retains its original signed value.

Also replace `sign(x) * (uint(abs(x)) >> k)` for constant 32-bit shifts with an
arithmetic shift after biasing negative inputs by `(1 << k) - 1`. This truncates
toward zero, including INT_MIN, and retains NIR's masked shift-count semantics.
The sign expression can be either isign or its min/max expansion.

Both transformations are limited to the native AGX3b compiler path. One retained
compute input shrinks from 19,747 instructions / 212,614 bytes to 18,692 /
201,388, without changing its 191-register allocation or introducing spills.
The full saved replay does not yet show a meaningful M3 FPS improvement; this
is a code-size improvement, not a claimed benchmark win.

Validation:

- All 296 host compiler tests pass.
- The independent Python oracle in
  `src/asahi/compiler/test/probes/native-signed-integer/generate.py` generates
  512 mixed/edge inputs, all 256 byte values, all four byte positions and shifts
  0..31 under three sign formulations. M3 and M4 both return exactly the expected
  51,712 words under watchdog and thermal guards.
- Both devices complete the saved 30-frame Satisfactory replay and preserve
  exact RGB at frames 1/16/31, with unchanged settings and adaptations.
- Qualified isolated library SHA256:
  `3e1934a3bead3d7247c73d1705509469665c2a42dc63a09b2bb7f7b3a318595f`.

Full shader inputs and replay evidence remain in the private local benchmark
repository. The generic numerical oracle contains no game assets.
