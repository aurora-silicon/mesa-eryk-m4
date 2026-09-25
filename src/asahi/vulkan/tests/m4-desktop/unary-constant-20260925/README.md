# Register-only unary constant legalization

Satisfactory Vulkan startup exposed 18 serialized shader inputs failing at
`roundeven #0.000000` after image-format lowering and backend copy propagation.
Materialize minifloat sources for register-only AGX3B unary instructions before
register allocation, applying abs/neg to preserve the original floating value.
All 18 original inputs compile after this change; proprietary inputs stay in the
local benchmark repository rather than this source repository.

The standalone shader writes RGBA8 UNORM pixels `(i / 255, 0, 0.5, 1)` then
reads them through a coherent typed view. Expected packed output is
`i | 0xff800000`, for i=0..255. Compile with `glslangValidator -V`.
Use the current workspace `tools/vk-buffer-oracle.c`, with two 4096-byte buffer
bindings, all-zero initial bytes and the expected words at indices 1024..1279.
Set M4_ORACLE_BINDING_COUNT=2, M4_ORACLE_IMAGE_FORMAT=37,
M4_ORACLE_STORAGE_IMAGE=1 and M4_ORACLE_IMAGE_FORMATLESS=1. Dispatch four groups.
Hardware execution requires the mandatory watchdog and thermal guard.

This small shader also passes on the previous driver, so it is an image-store
smoke test, not a reproducer for the late-folding compiler failure. The 18 real
compiler inputs establish the before/after regression.

M4 fixed build: exact readback, 0/2048 mismatching words; lavapipe same result.
Evidence: 20260925T063449594740Z-unary-constant-rgba8-current-oracle.
Existing half-input unary compiler regression passes. The independent funop-core
unit reports its existing register-out-of-range expectation failure (58/59);
that executable does not link the changed compiler pass. No blanket suite pass
or M3 hardware validation is claimed.
