# Native M4 multisample comparison

Versioned source mirror of workspace tools/macos-native. Reuses the existing
private g16state.py collector; no raw native images are distributed here.
Compile on macOS with `xcrun clang -arch arm64 -fobjc-arc -Wall -Wextra
-Werror -mmacosx-version-min=26.0 multisample-probe.m -framework Foundation
-framework Metal -o multisample-probe`.

## Multisample launch-state comparison

`run.py multisample --samples 2 --mask 1 --width 128 --capture
--firmware-trace` captures a constant-red triangle over a black/opaque clear.
Counts1/2/4 and an explicit mask are validated. Every output pixel is checked
immediately and100ms after completion, and both raw images are retained.
The expected red channel is round(255*popcount(mask)/samples), alpha255.

Add `--resolve-compute` for store-only MSAA followed by an explicit GPU compute
resolve. The default uses native StoreAndMultisampleResolve. Record which path
was used: these are behavioral comparison probes, not identical Vulkan streams.
Use `--render-active --repeat N` only when the submitted/complete snapshots
miss needed runtime state; repeats remain finite (1..256). No shader waits for
the collector. Output must not be called an active fragment capture without
matching command/firmware evidence. The existing collector output-allocation
and ready/release protocol is retained.

Stage multisample-probe, multisample.metal, native-common.h and the current
collector in the same remote directory used by run.py. The probe enforces
Apple M4 identity and queries native sample-count support. The runner checks
Mac16,12 and25G83. The Objective-C and Metal sources compile on the compiler
Mac with the documented warnings-as-errors command; this is not M4 execution
validation. The versioned runner mirror accepts `--workspace-root` pointing
to the development workspace so evidence is retained in its provenance tree.
