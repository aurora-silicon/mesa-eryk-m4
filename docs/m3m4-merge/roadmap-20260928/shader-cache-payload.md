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
pass. ARM Vulkan build passes. Hardware cache and binary-shader tests are pending;
no desktop installation or complete hardware qualification is claimed here.
