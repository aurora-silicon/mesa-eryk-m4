# First-fault storage reservation

M4 63703468d95d, M3 development 1ace524c28f0, published e9268a4002d9.
Reserve the bounded byte buffer during native initialization, before publishing
jobs: 16 MiB on M4, 2 MiB on M3. A failed optional reservation is logged and
leaves rendering available; later faults never retry that large allocation.
Capture timestamps and job IDs are refreshed at the actual fault, not boot.

Bounds ensure record appends fit the reservation; they use NOWAIT defensively.
The small owning box and devcoredump entry also use NOWAIT. This is not a fully
allocation-free publication: generic sysfs/device registration still allocates,
and M4 owned-BO readers may need a cached kernel mapping. Neither direct-reclaim
latency nor successful capture under arbitrary memory exhaustion is qualified.

All three source variants build. Production serializer extraction tests append
records successfully with allocator failure armed after construction, and cover
size/count overflow and read rollback. M4 module f7fbf4c085000da47cfa84424c72600de7dc979fdb32f24c2a00b4d73c130b48
boots normally and retains a 1,970,952-byte snapshot after deliberate timeout.
The final health guard rejects child exit zero, preserves DEVICE_LOST evidence,
and watchdog recovery restores the normal desktop. 88 render smoke cases pass.
M3 reservation is source-built only while its full catalog uses the previous
reviewed kernel. Its earlier non-reserved snapshot/lifetime tests remain valid
for that installed revision, not for this new reservation change.
