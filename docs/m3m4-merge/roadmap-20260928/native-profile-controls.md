# Separate the native replay performance profile from correctness

Final publication candidate b656c92f was compared on the same current M3 boot,
capture, packet policy, and guarded runner. The earlier native-read-early binary
still gives 4.66 FPS in an unscored screenshot check; current ordinary preambles
give about 3.09. Restoring all six experimental options gives 4.79, but fails the
current corrected reference (346/377/435 changed pixels, maximum 3/3/2).
These screenshot measurements are diagnostic, not qualified timing results.

Starting with the same two preamble flags, descriptor prefetch plus compact
prefetch alone matches every checked RGB pixel. Scaled-format branching alone
also matches. Direct buffers alone exactly reproduces the full-profile pixel
differences and gives 4.64 check FPS. Thus the direct-buffer control is the main
performance/correctness tradeoff in this test; the new publication sequence did
not cause the slower ordinary result. It remains disabled by default. This does
not qualify scaled-format branching globally: the prior Lethal Company failure
still needs rechecking. No reference or tolerance was changed.

M4 DX12 ordinary packet32 stops at the existing 1 GiB available-memory floor,
first with its desktop and again with a private software headless compositor.
Both failures retain guards, logs and memory telemetry; neither is a GPU fault
or a successful replay. The desktop is restored and the activity ABI is healthy.
The prior successful DX12 run used packet8, so comparison requires retaining
that difference rather than blaming the compiler or relaxing the memory guard.

## Matched Lethal Company follow-up

On the current M3 compiler, enabling scaled-format branching changes 259 pixels
in each checkpoint by at most one RGB level versus the matched preamble control.
Both current outputs differ substantially from the old reference. Reversing only
the BACKFACING helper correction reproduces every old reference RGB value at
all three frames. Thus the large reference shift is separate from the small
branch-control difference. The prior exact-only regression remains retained;
no global branch default or reference tolerance was changed.

## Completed M4 memory control

The same M4 compiler and DX12 capture complete with packet8 and the private
software headless compositor, with and without memory preambles. All three RGB
images are bit-exact between both variants and the current M3 output. The M4
checks retain their diagnostic-only classification; they do not qualify a new
performance score. The watchdog, thermal checks and 1 GiB memory floor are
unchanged. Packet32 boot files and the exact original initramfs were restored;
normal-boot verification follows the planned restart. No compiler source was
changed to make the memory control pass. Dynamic memory-pressure handling and
bounded per-packet allocation remain open driver work.

## Split the direct-buffer control

Two temporary source controls were built and restored in a `finally` block;
the normal source and desktop builds are unchanged. Keeping branched fragment
alias lookup while enabling direct descriptors reproduces all the same
346/377/435 differing pixels, max3/3/2. Enabling only branchless alias lookup
without direct descriptors changes 2/12/5 pixels by at most one RGB level.
The original exact comparison failures are retained; neither result changes the
Satisfactory policy or enables an optimization by default. The split narrows
the larger difference to direct descriptor lowering, rather than branchless
alias lookup. Frostpunk qualification of the latter control is ongoing.

M4 normal boot is verified restored: boot775cfd3f, packet32, same kernel module,
active display manager and healthy idle native activity. Both smaller-packet
DX12 controls completed without a reboot or GPU fault during replay.

## Final bounded qualification checkpoint

The full 204-case frozen fragment `robustness.buffer_access` subgroup passes
on both M3 and M4 with each control: branchless alias lookup and the original
direct-buffer option, with memory preambles enabled. No cases were filtered by
outcome; global catalog indices and the catalog SHA256 are retained. The actual
host sparse-lowering test also passes for the branchless variant and after
restoring normal source: 52 coalesced boundaries, 810 bounds-oracle cases, six
352-case sparse byte/vector/atomic groups, and the other retained rejection,
carry and immutable-window checks. These successes do not waive image failures.

M3 Frostpunk branchless-only passes its unchanged reviewed policy: 8/21/13
pixels differ, maximum1/2/1. M4 native Satisfactory reproduces M3's 2/12/5-pixel
max1 difference exactly. Desktop Mesa and driver defaults remain unchanged.
The user requested wrapping up these harder optimization experiments at this
checkpoint; broader roadmap work is still open. No stress workload is left on
either machine, and M4's normal packet32 boot/display manager were restored.
