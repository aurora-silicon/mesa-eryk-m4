# September20 full CTS regression fixes

Baseline: frozen3,206,030case retest recorded176Fail29Crash6Timeout5QualityWarning.
Original ledger remains immutable under build/cts-full-confirm3-20260920.
Fix validations are separate overlays with original catalog indices.

## Conditional image fences

Compiler-generated fence_pbe_to_tex, fence_mem_to_tex and pixel image fences
were emitted as workgroup rendezvous barriers on G16G. These intrinsics are
invocation-local ordering operations and can occur under nonuniform conditionals.
The write-after-read memory-model shaders conditionally store an image; invocations
skipping the store never reach the inserted barrier. Use the existing qualified
non-rendezvous device/texture fence encodings on G16G. G17P behavior is unchanged.

Host and ARM64 Vulkan builds pass. The compiler workgroup test now also checks
all three fence intrinsics under a divergent branch and rejects rendezvous encodings.
Guarded22case original memory-model abnormal-result overlay:16Pass6Fail, no crashes,
timeouts, reboot or guard failure, peak80C. All16originalCrash cases nowPass;
the six original validation failures remain open and are not claimed fixed.
Evidence:20260920T105052985877Z-cts-fix-mm-conditional-fence-all. First isolated
formerhang passed in0.616runner seconds (20260920T105024488822Z-cts-fix-mm-conditional-fence-one).
Installed Vulkan SHA256:3f913bafcff9f9e75885aa4e124568ce85a4bd8b65612a8504dc5beaf9c6704d.
