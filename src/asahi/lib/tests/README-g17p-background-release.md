# G17P background Ioto release

The bounded internal clear/reload path uses `07 02 54 00 02 00`, not the
ordinary fragment Ioto release `07 02 54 f3 02 00`. Both are six-byte PIXREL
records; their descriptors are respectively `0x200` and `0x2f3`.

## Native witness and producer

The valid `20260904-194053Z-j700` first-doorbell before snapshot has its 3D
descriptor at `0x420c00b0000`. Its BG and partial-BG pointers at descriptor
`+0x1e80` and `+0x1eb0` are `0x10001990240` and `0x10001990000`. Both programs
contain `07 02 54 00 02 00` at program `+0x1de`. The captured Ioto page SHA-256
is `86bdd93717d8ed322b14aaed56250958a6c979eee039316c94ddae87c22518a7`.
This is a release-protocol witness, not a claim that the native shader body
or resource graph is identical to Mesa's small clear/reload shaders.

In the extracted AGXMetalG17P arm64e image, the producer chain is:

- `AGCDeserializedReply`'s name table at `0x878880` identifies section index
  6 as `__GPU_METADATA_2`. Reply `+0x18 + 6*0x28 = +0x108` is its data span;
  `+0x338` is the fallback span.
- BackgroundObject's ESL constructor passes program kind 9 at `0x3c2890`.
  Instructions `0x3c49f8..0x3c4a78` read that section's root FlatBuffer
  field 1 and store its low 16 bits at encoder `+0xac`.
- `ESLStateLoadEncoderGen2::finish`, `0x32cbd0..0x32ccc8`, emits a PIXREL
  for kind 1 or a nonzero descriptor. It serializes the descriptor into
  the six-byte record. Thus the release mask comes from compiler metadata,
  not the physical GPR count or a universal fragment constant.

The ordinary fragment compiler objects in the fresh G17P D-matrix probe
have `__GPU_METADATA_2` root field 1 equal to `0x2f3`, independently matching
the ordinary FS capture. Native BG supplies `0x200` instead.

## Scope and verification

Only `hk_agx3_g17p_rt0_bg_program_generate` selects the new background
composer. It is used for the internal clear/reload shaders compiled with
`ignore_tib_dependencies`; those shader bodies do not own ordinary pixel
wait/release operations. API fragment programs retain their existing
composer and `0x2f3`; EOT retains its separate composer without PIXREL.
Arbitrary shaders need their own compiler-derived release metadata.

The core test checks complete ordinary-FS bytes, a BG composition differing
only in the descriptor byte, the unchanged complete EOT fixture, and
transactional rejection of malformed records, invalid addresses, null
spans/state and insufficient output capacity. The Honeykrisp test checks
BG clear, reload and relocated reload termination independently.

This fixes a source-backed discrepancy. Hardware completion or rendered
pixels are separate validation requirements; unit tests do not prove them.
