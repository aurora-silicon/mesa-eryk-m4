# Propagate eager shader upload errors

Shader compilation and deserialization previously returned success even when
executable/preamble allocation, CPU mapping or upload failed. An incomplete
compute shader then reached dispatch, which reported FEATURE_NOT_PRESENT; it
could also remain in the pipeline cache.

The linker now exposes a checked result distinguishing invalid input, host/device
OOM and upload failure. The existing bool interface remains for other consumers.
Honeykrisp propagates eager link/upload errors through compile/deserialization,
letting the existing shader-object cleanup unwind ownership. Lazy graphics
variant callers still use the bool interface and remain a separate audit item.

M4's first armed executable mmap rejection reproduced FEATURE_NOT_PRESENT.
After the fix it returns OUT_OF_HOST_MEMORY; a second GPU fill/readback in the
same device succeeds and all callback-owned allocations are freed at destruction.
This passes with normal disk cache behavior and separately with disk cache
explicitly disabled. The 184-case external-memory/render selection finishes
178 Pass, six NotSupported. Both paths use the normal watchdog/thermal guard.
M3 and final rebuilt binary qualification follow in a separate checkpoint.
