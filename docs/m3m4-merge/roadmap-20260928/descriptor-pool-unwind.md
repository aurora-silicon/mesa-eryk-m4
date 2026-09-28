# Descriptor-pool allocation failure handling

The constructor used common destruction after a failed BO allocation before
initializing the VMA heap. The list walker would dereference an uninitialized
list. CPU mapping failure was also unchecked, allowing publication of a pool
without a usable CPU pointer. Initialize the empty heap before fallible work and
return OUT_OF_HOST_MEMORY after a failed mapping, releasing the backing object.

Compute maxSets padding in 64 bits and reject aggregate-size overflow rather
than allowing an undersized allocation. Host ASan/UBSan failure injection of the
actual constructor reproduces the old uninitialized cleanup and passes wrapper,
backing and CPU-map failures plus wide padding accounting with the fix. ARM full
Mesa build passes. Generic util_vma allocation failure remains outside this fix.

The hardware oracle now has an optional descriptor-pool failure mode followed by
same-device retry and 4096 checked fragment outputs. M3 passes injected mapping failure followed by same-device retry and all 4096
fragment outputs, then 178 rendering/memory CTS cases (six unsupported). The
first attempt failed in setup because the wrapper referenced a nonexistent
injector filename; the guard recovered by reboot, before GPU submission. Its
original receipt remains separate from the corrected pass. M4 passes the same injected failure and all 4096 fragment outputs, followed
by 178 rendering/memory CTS cases (six unsupported). Both guarded executions
finish healthy with the normal display managers active. This private binary has not
replaced that campaign candidate or either normal desktop.
