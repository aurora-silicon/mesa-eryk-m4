# Canonical fragment uniform-buffer addresses

Extend the native G15/G16 compute descriptor proof to fragment uniform-buffer
reads. These descriptors name canonical backing when sparse binding and
buffer descriptors are disabled, including buffers with a distinct replay BDA.
The existing proof removes redundant alias-table walks. Bounds checks, sparse
accesses, arbitrary BDA pointers and fragment SSBO translation remain intact.

Qualification initially found 2/9/4 changed pixels in the three retained replay
images, with a maximum channel difference of one. Bisection and before/after
compiler captures isolated legal floating-point reassociation in a motion-vector
shader: removing address control flow changed the association of multiplication
by a reciprocal and its original denominator. This was not evidence of a wrong
buffer address. To preserve established reference pixels, fragment shaders with
that expression retain runtime translation. The eligibility scan follows moves
and up to eight multiply nodes; it is an optimization policy, not an API limit.
No application identity or shader hash is used in production selection.

Host tests cover compute UBO/SSBO, fragment UBO/SSBO, disabled provenance,
reciprocal products and unrelated factors. Existing sparse-mask tests pass.
A CPU-checked Zink U32 UBO probe passes 32 changed-uniform rounds (32768 words).
Both devices complete the saved Satisfactory gameplay capture with exact RGB
images at frames 1, 16 and 31. Resolution and all replay adaptations are unchanged.
Three completed 30-frame timing repeats give:

| Machine | Previous median | New median | New replay FPS |
| --- | ---: | ---: | ---: |
| M3 Pro | 9.537297148 s | 7.870868793 s | 3.8115 |
| M4 | 13.808839765 s | 11.262342642 s | 2.6637 |

These are saved replay timings, not live-game FPS or conformance claims. All
hardware runs use watchdog and thermal guards. Private evidence and captures
remain in the local gpu-replay-bench repository; no game assets are included
here. Qualified library SHA256:
`5f89f5afb5a2e960d6fde394f0af39a1c767f5eb3e60d1b8c005d5c0ed68f873`.

M4 also passes all 80 pinned fragment UBO robustness2 cases, including dynamic
uniform buffers and null descriptors, under the guarded CTS runner. Selection
SHA256: `60afa523046f83bcdf9cc00b503669424f5f9a065c4838689af4ccf69af397a1`.

## Storage-buffer extension

The same proof also qualifies for fragment SSBOs. The reciprocal-product
eligibility check remains shared by both descriptor types. Three repeats with
exact images at frames 1/16/31 give 7.623448853 seconds (3.9352 FPS) on M3 Pro
and 10.075577957 seconds (2.9775 FPS) on M4. Library SHA256:
`5c97628cb80272aab4b9c9d5eee7fc6def480ccf088a1a54250a39e83e97a2a0`.

A separate vertex-stage extension changed pixels on both machines and is not
enabled. The fragment SSBO change does not depend on experimental preambles.

All 80 selected fragment SSBO robustness2 CTS cases pass on M4, including
read/write, dynamic bindings and null descriptors. Selection SHA256:
`ef8fc8537fdbc344f91586d1378f96f7039d25074c2470b38782fcf13bbdf1ea`.
