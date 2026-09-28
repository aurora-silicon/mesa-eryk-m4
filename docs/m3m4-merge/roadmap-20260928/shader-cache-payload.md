# Shader cache payload validation

Reject truncated code/data blobs before allocating from their serialized lengths.
Read the two bounded spans separately, without a combined length addition that
could overflow a 32-bit size_t. Validate logical/backend stage enums and executable
main, preamble and constant-data ranges against the retained code span. Reject a
binary-size disagreement rather than overwriting it. Preserve zero-sized unused
variants and avoid malloc(0) for absent constant data. Remove duplicate mutex
initialization: hk_init_link_ht already initializes it.

The serialized format is unchanged. This is bounded cache-input validation, not
a complete audit of every shader metadata field or an instruction validator.

Host test extracts the actual deserializer; bounded reader and allocation/upload
mocks cover truncations, UINT32_MAX sizes, invalid stages and executable ranges,
empty data, successful copies, and both payload allocation failures. ASan/UBSan
pass. ARM Vulkan build passes. Hardware cache and binary-shader validation is
recorded below; no normal desktop installation or complete hardware qualification
is claimed here.

M3 hardware candidate 24d5215d8886bc11ce37aa53f41b8d5bc0e66a3b1ba09dd55bface7d3fd9c4b8
passes the complete frozen pipeline-cache families plus compute binary-shader
basic family: 5,503 Pass, 18 NotSupported (5,521 selected cases). Each 128-case
batch retains the normal guards, original catalog indices and selected binary.
The main catalog was checkpointed at 3,146,672 results, then resumed on its
unchanged allocation-20260928 binary.

M4 completed the same selection: 5,503 Pass, 18 NotSupported, zero failures.
Both per-case indexed result sets and exact install receipts are retained in
shader-cache-validation.tsv/json. M4 packet32 and normal display manager were
restored before this test; the existing guard retained all temperature limits.
