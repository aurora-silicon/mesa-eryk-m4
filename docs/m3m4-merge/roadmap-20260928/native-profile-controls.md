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
