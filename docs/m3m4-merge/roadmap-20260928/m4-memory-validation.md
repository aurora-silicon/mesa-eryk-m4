# M4 GEM boundary and queued-client lifetime checks

Kernel `ceb7e4cb0d1c` includes the checked-size fix `2631756846be` and shared
allocation counters. The ordinary desktop is restored after each probe.

The boundary probe submits zero and four overflowing near-u64-max sizes to the
GEM_CREATE ioctl, expecting EINVAL/EOVERFLOW with no returned handle. It then
creates six small valid sizes around 16 KiB/64 KiB boundaries, maps the rounded
extent, verifies zero fill, writes/reads both ends, unmaps and closes each
handle. Live counters return exactly to the stopped-session baseline:
11 user GEM objects / 53,739,520 bytes and 650 kernel GEM objects /
981,663,744 bytes. These include retained firmware/previous-slot owners, not
just handles still open in userspace.

The first probe attempt completed the requests but its final accounting call
hit a Python variable-shadowing error. Its process_failure evidence is retained
at `20260927T215251672773Z-roadmap-m4-gem-size-boundaries`; it is not counted as
a pass. The corrected repeat `20260927T215327451305Z-roadmap-m4-gem-size-boundaries-r2`
passes, with no driver fault.

The lifetime probe first checks a finite one-million-step integer compute
against an independent CPU uint32 result. Each exit/SIGKILL then requires a
one-nanosecond Vulkan fence wait to return VK_TIMEOUT before ending the process.
This establishes queued or executing work, not a particular hardware phase.
A normal checked dispatch follows every pair to verify further rendering.

Two warmup pairs and twelve measured pairs complete: 14 exits, 14 SIGKILLs and
15 successful full readbacks. The measured interval starts after warmup.
Current memory remains exactly 1 user GEM object / 17,039,360 bytes and 569
kernel GEM objects / 798,949,376 bytes before/after. The module's high-water
counters also remain unchanged over the measured interval. Retained memory is
not zero because firmware caches previous slot owners until displaced safely.
Evidence: `20260927T215741750334Z-roadmap-m4-client-exit-lifetime`.

All 88 rendering smoke cases pass on this kernel. These tests do not establish
full pressure handling, sparse/dma-buf ownership, construction-allocation failure
unwind, or safe reclamation after a hardware fault. M3 validation follows a
separate reviewed deployment rather than borrowing these M4 results.
