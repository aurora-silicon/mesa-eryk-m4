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

## Vertex exports across control flow

Point-size shaders can export position before a robust buffer load introduces
control flow, then export point size and layer after the merge. The terminal
batch optimizer cannot own these stores, but the packer incorrectly rejected
their valid scalar output indices. Use the existing scalar COPY/UVSW protocol,
retiring each write before further instructions can reuse GPRs, staging storage,
or scoreboard slots. Keep the terminal batch optimization where applicable.

All 13 original rasterization failures pass (1.223 runner seconds, peak 65.25 C),
evidence 20260920T110022289933Z-cts-fix-point-streamed-final. Host/ARM64 builds and
20 vertex compiler tests pass, including repeated/unordered indices, source/slot
reuse, sparse outputs and invalid operands. Updated obsolete tests that assumed
these general output shapes must be rejected or used the retired BAND wait.
An unrestricted packer unit run also exposed an existing diagnostic assertion
on intentionally malformed unaligned registers; this is recorded separately
and does not constitute a full compiler unit-suite pass.
Installed Vulkan SHA256: d39022a3e1f1327b6f0e2418590e751d5b6d6667d96059c585fd818c5903a7d8.

## Array gradients with register offsets

The encoder rejected ordinary colour array samples with gradients and dynamic
offsets despite already supporting their staging layout for depth comparisons.
An independent Metal compute probe, compiled offline by AIR-NT 32023.917 for
applegpu_g16g, confirms the colour instruction uses the same layer/offset bank
and separate gradient bank. Retain its source, native bytes and field decode.
Remove the obsolete exclusion; add a byte-exact native encoding check.

All 120 original texturegradoffset_pcoffset failures pass on hardware, 11.951
runner seconds, peak 68.25 C. Evidence 20260920T110253129201Z-cts-fix-array-gradient-offset.
Host/ARM64 builds pass; texture core passes 6417 checks. Installed Vulkan SHA256:
e66ca61c01866ade7378106a71b5573927a16598e9301f69e79bab64d1389532.

## Independent application memory mappings

Cached BOs may retain an internal mapping and may be up to twice the requested
size. Mapping VkDeviceMemory through bo->_map could ignore a placed address;
mapping or reserving bo->size could replace neighboring virtual memory. Give
DRM/virtio application allocations independent mappings of VkDeviceMemory's
size, preserve internal mappings/imported aliases, and release application
mappings on vkFreeMemory. Keep the Windows Lock2 backend on its native mapping
operations; that backend does not advertise placed maps. Reject a backend that
fails to honor the requested address instead of reporting a false success.

All 13 placed-mapping cases plus the two originally canceled memory tests pass
in one process (cache reuse retained), including all three original mapping
failures and the original unmap_reserve.size_1048576 SIGSEGV case. Evidence:
20260920T110746501362Z-cts-fix-memory-maps-final; 15 Pass, peak 68 C, 5.881 runner
seconds. Host and ARM64 builds pass. Installed Vulkan SHA256:
d6028797c6b2fca204d10079af501ca0c026d23959622c00897853d25fdbb596.
The original core and ledger remain retained. A subsequent independent UBO
capture hit a compiler IR-validation exit (exports before branches), recorded
at 20260920T110620505599Z-cts-fix-remaining-small; no GPU fault or thermal stop.

## Shader-part exports after late control flow

Late robust/sparse address lowering splits a formerly terminal fragment output
sequence into multiple blocks. The backend emitter sees only blocks created so
far, so agx_exit_block() cannot yet identify the final exit. Earlier exports
were left in intermediate blocks and omitted by register allocation's final
parallel copies. They produced wrong pixels or an IR-validation exit on an
uncached compilation. Normalize nonterminal exports through function variables
and SSA merges after late lowering, keeping conditional and repeated writes,
then export each final value from the actual exit.

All eight original UBO indexing failures pass with shader caching disabled,
including the four vertex cases already helped by the streamed-export fix.
Evidence 20260920T111202326946Z-cts-fix-ubo-exports, peak 66 C, 1.832 runner seconds.
Host/ARM64 builds and the new conditional/repeated-export compiler check pass.
Installed Vulkan SHA256: f6c9710940d64adb310d58daef222c9861d1c76fd53612493e03827a8e7a8a76.
