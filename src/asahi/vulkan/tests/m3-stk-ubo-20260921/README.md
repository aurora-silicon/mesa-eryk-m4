# M3 opt-in UBO prefetch and ordinary descriptor variants

Expose the existing bounded M4 UBO prefetch and executable variants on the
shared G15/G16 shader ABI. Default remains off. Existing cache identity bits
cover all switches. Dynamic/update-after-bind/partially-bound descriptors stay
ineligible; alias/sparse fallback and per-draw variant selection are retained.

J514S native replay: UBO prefetch without the direct variant is pixel-identical.
Integer-only preamble + UBO prefetch + direct variant differs at52 of1946984
pixels, maximum one 8-bit channel level; adding plain sampler specialization
produces exactly that same image. Full-resolution unchanged3024x1964 racing
sample with integer-only preamble and all three UBO/variant/sampler switches:
32.697FPS over10seconds, peak70.25C. Both race screenshots inspected.
VSync is disabled and the isolated profile cap raised to1000; quality unchanged.
PMP memory/fabric floor4/2 is held only for each guarded workload and restored.
This is application swap cadence, not a full benchmark or scanout measurement.

Known unsupported combination: AGX_NATIVE_FLOAT_PREAMBLE=true with the direct
UBO variant changes the kart significantly (108367pixels,max125channel error).
That combination is not selected. The earlier no-variant float-preamble probe
was insufficient to qualify this composition. Evidence retained, no blanket
float-hoisting correctness claim. Broader CTS validation follows separately.
