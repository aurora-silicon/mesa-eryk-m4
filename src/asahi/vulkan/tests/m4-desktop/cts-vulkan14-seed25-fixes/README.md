# Vulkan 1.4 seed 20260925 fixes

## Mutable descriptor crash

The original mutable combined-image-sampler/storage-buffer case aborted at
`hk_descriptor_stride_align_for_type` because rounding the 72-byte sampled
image payload to the buffer descriptor's 16-byte alignment produces an 80-byte
slot. HK_MAX_DESCRIPTOR_SIZE represented only the unpadded payload. Derive the
maximum slot size from the native payload plus alignment, and use that maximum
alignment for mutable pools with an omitted or empty type list.

The original unmodified CTS 1.4.6.2 case passes on M4 in **0.413 seconds**.
The mandatory guard passed, temperature peaked at 59.25 C, and no reboot or
GPU fault occurred. The new library was installed atomically; the existing
SDDM compositor retained its old mapping while the fresh CTS process tested
the updated library. Exact library and CTS hashes are retained in the evidence.
No broad regression campaign was run; the original 1,000-case report remains
unchanged. One of the two original crashes is now repaired.
