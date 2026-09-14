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

## Scaled-format swizzle CTS crash

The second original crash dereferenced ProgramBinary address 0xf inside CTS
`vk::createShaderModule`, before calling the driver. SwizzleTests prepared a
UINT program for USCALED (and INT_BIAS for SSCALED), while `getSamplerType`
correctly chose FLOAT. The binary collection lookup used the missing float
program name. Correct the 17 scaled-format declarations to FLOAT/FLOAT_BIAS,
matching the existing runtime selection and scaled-format sampling semantics.

A separately named `deqp-vk-scaled-swizzle` containing this test correction
passes the original swizzle case in **0.311 seconds**, peak 59.25 C, guard pass.
It uses the same Mesa library as the mutable-descriptor retest. No Mesa change
was needed for this crash. The original CTS binary and source were restored
and checksum-verified by cts-patched-build.py; original results remain Crash.
The core backtrace warns about a subsequently changed Mesa shared library, but
the faulting code/registers are in the exact unchanged CTS executable.

Both original crash cases now have passing targeted results: one driver fix
and one separately identified CTS correction. The 91 original Fail outcomes
and eight mandatory-feature skip reasons remain outstanding at this point.
