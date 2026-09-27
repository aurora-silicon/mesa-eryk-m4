# Vulkan memory allocation rollback

After creating/importing a BO, hk_AllocateMemory also installs a read-only GPU
alias. A failed alias mapping previously returned UNKNOWN without releasing the
memory object or BO. It now returns OUT_OF_DEVICE_MEMORY and unwinds both.
Imported fd ownership transfers only after heap-accounting admission succeeds;
a later heap-limit rejection must leave the caller's fd open. Page rounding
also rejects uint64 overflow before allocating anything.

Production-function extraction tests cover memory-object/BO creation failures,
alias failure, heap-accounting rollback, imported-fd ownership, normal free and
all 16,383 overflowing allocation sizes. These use observable mocks, not forced
kernel memory exhaustion.

The LD_PRELOAD test interposer is explicitly armed after device creation and
rejects exactly one read-only VM bind before it reaches the kernel. The M4
baseline returns UNKNOWN; the fix returns OUT_OF_DEVICE_MEMORY, then the same
device performs a checked 16 KiB GPU fill/readback. Final M3 and M4 both pass,
with zero callback-owned allocations after destruction. `tested=0` in this
probe mode describes the callback-failure loop count; one ioctl-boundary failure
is recorded separately by BIND_FAILURE. It is not kernel fault injection.

Initial interposer attempts aborted before GPU work because RTLD_NEXT cannot
see libdrm inside the Vulkan driver's local loader scope. The corrected helper
resolves libdrm through its own explicit handle with pthread_once. Those
preflight failures remain in the experiment archive.

Final library f3141473a77b726bf41fb3b9c58979908bd58daed640bbe2d66f8032cd87e53b:
both GPUs complete 184 frozen external-memory/render regression cases, with
178 Pass and six NotSupported each. Normal desktop prefixes are unchanged.
