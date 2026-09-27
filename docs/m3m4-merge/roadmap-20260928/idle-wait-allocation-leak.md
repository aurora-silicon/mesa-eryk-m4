# Idle-wait synchronization allocation leak

The expanded callback-failure probe creates a 16 KiB buffer, backing memory,
command pool/buffer and fence, records a fill plus host visibility barrier, then
submits and checks every word. Each successful path uses the real M4 GPU.

Baseline has 15 resource-path callback allocations. Failing allocation 15
leaked one allocation after all objects and the device were destroyed. In
vk_common_QueueWaitIdle, vk_sync_create succeeded, then allocation of the
signal-only submission failed; the error return omitted vk_sync_destroy.
The fixed code destroys that private sync on recoverable allocation failure.

The first harness iteration incorrectly assumed vkDeviceWaitIdle could not
return allocation failure. It now retains that result and retries the one-shot
failure before freeing GPU resources. The second run exposed the actual leak;
its failure evidence is preserved separately from the passing fixed run.

All 15 failure points now finish with zero callback-owned allocations. Two
failures are handled by successful internal fallback, which still validates
the fill/readback. Device-construction failure coverage remains eight points.

Scope: no kernel allocator or mmap failure is injected here. Deferred
DEVICE_LOST can retain a queued sync reference and is deliberately not folded
into the recoverable OOM cleanup change; that ownership path remains open.
M4 hardware validation is separate from M3, whose full catalog continues on
its previously reviewed Mesa until a guarded validation checkpoint.
