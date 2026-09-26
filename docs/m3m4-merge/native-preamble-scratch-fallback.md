# Native preamble scratch fallback

An optional native preamble can spill during register allocation. The HAL200
constant program has no scratch allocation, so a successfully compiled shader
previously failed at vkEndCommandBuffer with VK_ERROR_FEATURE_NOT_PRESENT.
Retain unmodified backend input for this opt-in path. If compilation reports
preamble scratch, discard that binary and recompile with preambles disabled.
The main shader still performs all of the original operations. Allocation and
compiler failures retain their normal error paths; no unsupported preamble is
submitted and no image settings or shader arithmetic are changed.

Validation on 2026-09-26 used an isolated experimental compiler that admits
64-bit preamble exports as pairs of 32-bit words. That experiment produces a
fragment preamble requiring 32 scratch bytes in the saved Satisfactory Vulkan
capture. Both G15S and G16G rejected it at command-buffer finalization before
the fallback. With the fallback, both log the recompilation and finish the
capture. Completed exact RGB comparisons subsequently rejected both builds.
The 64-bit experiment is not correctness-qualified and received no timing
approval. This establishes recovery from the submission refusal, not image
correctness. The initial note claiming exact matches was premature.

Before: M3 20260926T063500123739Z, M4 20260926T063530700865Z.
After: M3 20260926T063843033532Z, M4 20260926T063821702538Z.
Test library SHA256: 1a8e492ab9205e4e7565f23961a93bc38068f5fc5279a3b582897714bc3e8f90.
The 64-bit-export experiment is retained separately and is not enabled by this
fix. Evidence, source patches and exact binaries are retained in the private
local benchmark workspace. Watchdog and all-channel thermal guards were active;
the M3 failed-test guard triggered a recovery reboot before the passing test.
This validates the saved replay, not a live application or desktop replacement.
