# Native M3 vertex constant-program uniform FMA witness

The retained own Minecraft Vulkan replay on J514S/T6030, macOS 26A428,
produces pipeline archive 109. Vertex text is 1348 bytes; the Mach-O symbols
put `_agc.main.constant_program` at zero and `_agc.main` at 576.
The complete table walk reproduces all 103 instructions, including 26 NOPs.
Extracted text SHA-256: `51e42ab247b67aef483e55c8b218921f89e8cbd21c39c315f3ae96ebc2a096a6`.

Fifteen short `f32fma.ep3` instructions write uniform halves 24 through 52,
with f9=3 and the destination in f3. All three inputs remain registers.
Their independent source cache/discard bits and the one slot-3 wait are
retained in `test_native_fma_uniform_destination`; every complete eight-byte
instruction is matched. A sixteenth write to half 54 uses ep5 and additional
f106/f107 fields; their release semantics are not yet qualified and that
form is excluded. Uniform sources, modifiers, high source-register bits,
other wait slots and non-AGX3b encodings also remain outside this API.

`agx3_alu_fma_to_uniform` is an inactive encoder foundation. It changes no
compiler policy, disk-cache key, shader output or hardware behavior. A future
store fusion still needs a proof of physical last use and uniform ordering,
scoreboard publication, GPU image checks and paired timings. This artifact
is not evidence of a performance gain or M4 hardware qualification.

Raw archive, exact native field walk and the earlier losing vertex-preamble
controls remain outside Git in gpu-replay-bench diagnostics under
`m3-macos-driver-gap-20261003/vertex-alu-preamble`.
