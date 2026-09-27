# Four submitted packet fences on native failure

M4 kernel 63703468d95d and M3 development 1ace524c28f0 accept four submissions
of a finite overlong compute command. The command buffer permits simultaneous
use and begins with a cross-submission memory dependency for its shared buffer.
Each pending Vulkan fence is exported to a separate Linux sync_file before loss.

After the hardware timeout, poll and SYNC_IOC_FILE_INFO show all four kernel
fences signaled with errors: M4 [-110,-110,-5,-5], M3 [-110,-5,-5,-5]. This is
independent of Vulkan's cached device-lost flag. The primary retained snapshot
is ETIMEDOUT; secondary canceled jobs are not new root causes. QueueWaitIdle
then reports DEVICE_LOST, the next empty submission returns DEVICE_LOST, and
device destruction returns. Guards keep the fault verdict and watchdog armed.

First iterations already showed four error fences, but incorrectly expected an
empty no-fence Vulkan submit to discover loss after direct OS polling. That
operation can be a CPU no-op until Vulkan has observed backend health; the
corrected test uses queue completion as the first API observation. Original
attempts remain under build/driver-roadmap-20260928 and the experiment archive.

M3's preallocated snapshot is now also hardware validated: its new module
147b67a9dbfc08b215ba75469c5e0bedfeb1cb8bce977063ffbce465f268d5e2
retains the complete 478,843-byte dump. After the first recovery, the normal
desktop and 184-case memory/render regression recover (178 Pass, six unsupported).

These are qualified timeout/error-fence/retained-ownership checks, not proof of
stopped DMA, GPU-only reset, every firmware fault, or new-context recovery
without reboot. Terminal guard-progress errors now prohibit a Pass verdict
even when kernel logs are unavailable or overwritten; offline verdict tests
cover that evidence boundary.
