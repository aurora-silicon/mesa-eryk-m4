# M5 macOS trim-command-pool comparison

Apple M5 MacBook Air Mac17,3,8 GPU cores,16GiB, macOS26.6.2 build25G83,
`root@mac-m5`. Temporary source/build/SDK/results remain under
`/private/var/tmp/cts-comparison-20260915`. No M4 experiment ran in this task.

Same unmodified CTS1.4.6.2 source as the Radeon comparison, commit
`f6a29701220f34dd1407513bfe80d74ca7b392ce`. Release `-O3 -DNDEBUG`, Apple
clang21.0.0, native arm64, DEQP_TARGET=osx; unrelated video tests disabled
because the platform is unsupported. Full deqp-vk target built successfully.
Command-buffer test source SHA256 matches the local source exactly:
`e5aebe80729a938e352668436c5c3460e66c97caf9a00f06fecf90db9f15d74d`.

One isolated first attempt per driver, same CTS binary and Vulkan loader,
explicit driver ICD, ordinary driver defaults, hard60s process bound:

| Driver | CTS case time | Whole process | Result |
| --- | --- | --- | --- |
| MoltenVK1.4.2 | 0.008591s | 1.230704s | Pass |
| KosmicKrisp SDK1.4.357.1, git92ec601a34 | 0.558004s | 1.341660s | Pass |

Both are from the official LunarG Vulkan SDK1.4.357.1. The pinned installer
URL is https://sdk.lunarg.com/sdk/download/1.4.357.1/mac/vulkansdk-macos-1.4.357.1.zip .
Only core and KosmicKrisp components were selected; no system-global component.
The wrapper records device/driver identity, ICD, loader/driver/binary hashes,
exact command and raw QPA. These are case durations, excluding process startup.

The previous Radeon780M/RADV result is0.225327s. M4 measured isolated
5.132689s; M4's41.027435s contended outcome is not comparable to these isolated
runs. Different hardware/CPU policies and Vulkan implementations prevent a
raw GPU-speed conclusion. This test submits330 batches containing6000 event-
setting command buffers and trims300 times; it is not an application shader
throughput benchmark. MoltenVK's much shorter time merits inspecting event
implementation before attributing it to GPU speed. No repeat distribution
or full CTS conformance claim is made from these single attempts.

Reproduce with workspace `tools/cts-host-case.py --library SDK/macOS/lib/libvulkan.dylib
--icd SDK/macOS/share/vulkan/icd.d/DRIVER.json --vulkaninfo SDK/macOS/bin/vulkaninfo`,
plus the explicit CTS binary, case and new output directory.
