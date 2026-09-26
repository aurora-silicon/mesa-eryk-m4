# Native 64-bit subtraction and addition

Apple G16G witnesses encode full-width subtraction with destination width 3 and
independent four-half register operands. The zero-minus-register form supplies
negation. Use the pair `a - (0 - b)` for addition modulo 64 bits, replacing the
low-word/carry/high-word sequence; no signed-overflow assumption is introduced.
Keep the AGX3B register limit and each source's cache/discard and dependency waits.

Validation: 181 integer encoder checks include exact subtract/negate witness
bytes and invalid operands; all 297 host backend tests pass. Both M3 and M4
match all 1,048,576 oracle words, covering random values and overflow boundaries.
Eight pinned compute/graphics 64-bit subgroup-add CTS cases pass on each GPU.
The public oracle generator and indexed CTS selection are in validation/.

Those CTS checks also found an existing M4 compiler refusal in precompiled
geometry helper 78. The old division-by-three shortcut accepted only dependency
wait mask 1, but was selected independently of the eventual wait allocation.
The unchanged control library reproduces the failure. Stop selecting that fixed
shortcut: the newly qualified general wide multiply handles the same constant
with arbitrary supported dependency waits. The complete eight-case rerun passes.

Satisfactory's three comparison images remain RGB-exact on both GPUs. The
initial addition-only M3 timing median is 4.56572 replay FPS (three samples);
this is approximately unchanged and does not meet the five-FPS target. Final
combined library SHA256:
c35861121484f47b278467893ac5a74d09cc03816f879c44271de5d9fdfe45a1.
The 30-frame measurement and original capture/resolutions remain unchanged.
Private traces, Apple archives and detailed experiment evidence stay local.
