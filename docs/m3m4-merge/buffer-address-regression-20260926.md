# Restore the qualified fragment buffer path

The full game regression suite found a Frostpunk DX11 gameplay image change on
both G15S and G16G: about 17,330 pixels per saved frame, with a maximum RGB
channel delta of 27. The original acceptance policy is unchanged (2/255 and
24 edge pixels). This is not a conformance claim or a newly approved baseline.

Bisection identifies one application fragment source, BLAKE3
`1e6eb9e054aca0ac56af230d3a751028d52393b57e3f81c02bd5ec5c28a8cd6f`.
Both ordinary-buffer canonical descriptors and the later branchless alias leaf
lookup reproduce the discrepancy. Restoring the previous buffer addresses and
fragment leaf control flow removes it. Disabling scheduling, register-cache
hints or floating move folding for the affected shader does not remove it.
The lower-level compiler cause is still unresolved; fast-math permission alone
is insufficient evidence to replace the reference images.

Disable canonical descriptor shortcuts by default, including static provenance
and their tag-based address-lowering path. Retain them under the explicitly
diagnostic `HK_DIAGNOSTIC_DIRECT_BUFFERS` option, separated in pipeline/disk
cache keys. Restore branched alias leaf lookup for fragment shaders; other
stages retain the independently useful branchless lookup. Sparse residency,
bounds checks, BDA translation and advertised API features are preserved.
The default policy does not select applications or individual shader hashes.

Host validation covers 306 compiler tests, descriptor lowering and sparse
address tests. Sparse tests now exercise compute and fragment fallback paths,
with and without immutable-window specialization, in addition to the retained
tagged-address diagnostic tests.

A full M3 replay at frames 1/16/31 has 9/15/15 changed pixels, maximum1/2/1,
under the original policy. Vulkan library SHA256:
`7eef6f1e4902cbea003bc68860c5e4b30a83d6d809c77ee59faede7d5b82df19`.
Hardware experiments retain 60-second watchdog and all-channel thermal guards.
Private captures, compiler bundles and images remain in gpu-replay-bench.

M4 full replay171153886064 also passes:10/10/17 changed pixels,max1/2/1.
Its guarded replay completes in65.29seconds including setup and readbacks;
this is a correctness check, not a scored timing.
