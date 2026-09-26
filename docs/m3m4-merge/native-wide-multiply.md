# Native 32x32 to 64-bit integer multiplication

AGX3B IMADD can produce the full product of two 32-bit operands. Apple Metal
witnesses for unsigned and signed products differ in source extension bits 28
and 43; destination width is field 10 = 3. Preserve each source's independent
extension, cache/discard hints and all six dependency waits. A four-half result
must fit inside the existing 192-half register limit.

Use this form for signed/unsigned mulHigh and 2x32-to-64 multiplication. Full
64x64 multiplication still uses the modulo-64 decomposition, now with the
native high product instead of sixteen-bit partial products. Addition remains
on its existing lowering. The changes apply only to AGX3B.

Validation:

- Two exact Apple byte fixtures plus invalid register, wait and lifetime cases;
  all 173 integer encoder checks and 297 backend tests pass.
- The public oracle generates 65,536 input tuples, including boundary cross
  products and seeded random values. Both M3 and M4 match all 655,360 product
  output words plus unchanged input/guard words against Python arithmetic.
  Retained AIR proves that the probe emits wide native IMADD instructions.
- The 26 pinned extended-multiply CTS cases give 24 Pass and 2 NotSupported on
  each GPU. The unsupported cases require longVector (ivec5/uvec5); they retain
  their original catalog indices and are not counted as passes. The initial
  runner stopped at the first unsupported case; the complete rerun records
  both outcomes without replacing cases.
- Final Satisfactory checks 20260926T021803884828Z (M3) and
  20260926T021806099371Z (M4) match reference RGB frames 1/16/31 exactly.
  Final library SHA256: 9585403376e08e44ea5de6411fd17fb1471d79dc815f78612e28f3ae8553c11c.

One initial M3 timing was 4.55640 replay FPS: no demonstrated overall speedup.
The unchanged capture, 30 completed frames, original resolutions, watchdog and
thermal rules remain in force. Normal desktop Mesa installations are unchanged.

A rejected follow-up ran generic algebraic optimization after backend-specific
lowering. It recreated b2i32, which instruction selection no longer admits at
that phase, and caused a compiler-side SIGSEGV before replay rendering. The
backtrace/core and failed result are retained privately. That cleanup is absent
from this change; M3 was recovered by a controlled reboot before final checks.
