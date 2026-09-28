# Command buffer CPU mapping rollback

Ordinary command BO creation now rejects a NULL CPU map, releases its backing
reference and callback-owned wrapper, and returns OUT_OF_HOST_MEMORY. Oversized
command allocations similarly release backing and record the error before adding
anything to the command buffer ownership list.

The source-extraction test covers both chip policies, main/USC pools, wrapper,
backing and mapping failures, oversized allocations, and publication ownership.
These are mocked boundaries, not kernel memory-pressure tests.

M4 hardware validation uses the explicitly armed mmap interposer with
ASAHI_TEST_MAP_SIZE=131072 to target an ordinary command BO. The baseline silently
recovers on a later mapping attempt and returns success. The fix records the
first allocation failure; EndCommandBuffer returns OUT_OF_HOST_MEMORY, a second
fill/readback on the same device succeeds, and callback allocation count returns
to zero at destruction. All 88 rendering smoke cases pass. M3 hardware validation
is pending its ongoing full-catalog checkpoint; oversized mapping failure has
source-test coverage only.

Earlier unrestricted mmap injection reached shader linking first and found a
separate error-propagation defect (FEATURE_NOT_PRESENT after failed linking).
Those experiments remain archived; they do not validate this command BO path.

Run the offline check with `python3 tools/test-command-map-unwind.py`. Build the
interposer with aarch64-linux-gnu-gcc -shared -fPIC -O2 -Wall -Wextra, linking
-ldl -pthread. Use it only inside the normal bounded watchdog/thermal guard.
