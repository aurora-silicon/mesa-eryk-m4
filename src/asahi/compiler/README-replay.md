# Offline AGX compiler failures

Set `AGX_COMPILER_CAPTURE_DIR` to an existing private writable directory during a
bounded experiment. On a returned compiler failure, the compiler saves its
**entry-point** NIR and shader key in `input.bin`, final backend IR when available,
and a versioned manifest. Successful compilations publish nothing. Normal
rendering leaves this option unset. Capturing does not change rejection behavior.

Build and replay on the development host (no Vulkan device needed):

```
ninja -C build/mesa-m4-asahi-host src/asahi/compiler/agx_shader_replay
build/mesa-m4-asahi-host/src/asahi/compiler/agx_shader_replay BUNDLE/input.bin
```

The workspace `tools/compiler-replay.py` wrapper saves compiler stdout/stderr,
input and executable SHA256 hashes, Git revision, dirty patch and hashes of
untracked source files. Preserve the capture build's source patch and installed
library checksum alongside the bundle; the serialized input alone is not a
build identity. Do not copy complete process environments or credentials.

The input uses Mesa's internal serialization and a pointer-free shader key. It
requires the same serialization ABI hash (derived from NIR/key/options sources),
standard Asahi NIR options, and compatible C layout/endian ABI. ARM64 Linux to
x86-64 Linux replay was verified. This is a trusted development format, not an
untrusted-file parser or promise of compatibility across Mesa updates. The tool
refuses mismatched ABI hashes/sizes. Capture declines nonstandard NIR options
rather than persisting callback addresses. Compiler debug overrides must be
recorded by the enclosing experiment; replay does not infer them.

Publication uses a directory lock, a private temporary directory and atomic
rename. The input BLAKE3 identifies/deduplicates the full serialized input, not a
normalized failure family. The directory admits at most 32 bundles, each with at
most 8 MiB of input and 8 MiB of published backend IR. Interrupted temporary
directories count toward that quota. Writing failure must not change compilation
success/failure. This captures returned failures, not an assertion/signal inside
the compiler or a later GPU execution fault.

## First real regression, 2026-09-12

J713 SDDM/Hyprland fragment input:
`dea256bf3f8da51bb336633d29fb6640ab33d33ff16f4a9384ea901b7b8144d1` (BLAKE3).
The workspace retains the original bundle under
`_provenance/compiler-capture-20260912/agx-failures/`, plus before/after replay
logs. The 9356-byte input first rejected a signed half-width widening IADD with
shift 4. Its old diagnostic misleadingly printed the generic immediate as zero.
After legalization, the same input exposed F32-to-I16 conversion and mixed-width
FCMPSEL with a selected immediate. All three were fixed and the full input now
compiles to 6026 bytes offline. Focused generated tests cover shift/order and
integer narrowing. This establishes compiler acceptance, not rendering
conformance: the subsequent normal desktop/Foot test hit a fragment-stage timeout
and remains under investigation.
