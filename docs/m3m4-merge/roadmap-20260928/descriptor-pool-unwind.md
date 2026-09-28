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
same-device retry and 4096 checked fragment outputs. Hardware validation pending;
this binary has not replaced the running broader preamble replay candidate.
