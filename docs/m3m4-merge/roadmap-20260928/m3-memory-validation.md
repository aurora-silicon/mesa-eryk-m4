# M3 lifetime, initialization and fault containment

Installed development kernel 0984ca081cd1, module SHA256
16a30679f5ba0b7bc85b5e017fea0c3202a3c6dfec41e47cd71729a9a8fee5da.
Published adaptation d027554bcafd is built separately.

GEM size boundaries pass; current user/kernel/coherent allocation extents return
exactly to baseline. Fourteen process exits and fourteen SIGKILLs after a
one-nanosecond Vulkan fence wait returned TIMEOUT pass, with fifteen intervening
checked readbacks. This establishes queued-or-executing ownership, not a precise
engine phase. Measured counts/bytes return to baseline after warmup.

The finite long-compute fault returns DEVICE_LOST from fence wait and subsequent
submission; device destruction returns. The guard's final health check catches
the unhealthy runtime despite child exit zero. Its verdict remains failure with
watchdog retained. The 478,843-byte first-fault snapshot has primary ETIMEDOUT.
After watchdog reboot the normal desktop, 140 logical CTS cases and 88 render
smoke cases pass. This does not qualify GPU-only reset or stopped DMA.

Both GPUs pass all eight observed custom Vulkan device allocation callbacks
failed in turn, returning OUT_OF_HOST_MEMORY and freeing all callback-owned
allocations. This covers device construction only, not kernel or submission
allocation failure. The probe reports the observed allocation count explicitly.

The first client-exit attempt selected M4 by default and failed before submitting
work; its evidence remains in build/driver-roadmap-20260928/m3-client-exit.
The helper now derives the chip selector from the accounting endpoint.

The catalog resumes at 2,757,077 outcomes with a new reviewed identity; original
rows and prior revisions remain intact. Both guards now check terminal GPU
health before disarming, closing a child-exit versus monitor-poll race.
