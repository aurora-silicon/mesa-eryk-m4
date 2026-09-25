# M3 complete meta stream ledgers (2026-09-25)

Allow M3 to use the complete destination ledgers already recorded for pure
meta fills and copies. Once all commands are recorded, a complete ledger
allows adjacent compute streams to merge within the attachment limit instead
of submitting each small operation separately. Mixed/unclassified streams keep
the conservative ledger. Query copies and disjoint clears remain unchanged.

Also record write-bearing pipeline barriers on M3. Unlike the qualified M4
wide-visibility operation, M3 still needs firmware completion to publish writes
to aliased addresses. Merging must preserve those completion boundaries. The
change does not enable M4 visibility opcodes or pooled publication on M3.

## Validation

Isolated AArch64 build SHA256:
`ffd973c9457239463b06ac849306c48d5df535ed8febac5bdb8654006da70671`.
M3 kernel is the previously qualified bulk-reset implementation, module
`1fd461f37de903cafadb472beab2c7ef87f2d097489da9210eb0b650880a8de6`.

- 40 sparse and 3 alias buffer-oracle variants passed on M3.
- Host DRM attachment/resource and sparse specialization tests passed.
- Saved Satisfactory, 0 A.D. and SuperTuxKart Vulkan replays matched all
  three reference RGB images exactly on M3.
- Satisfactory also matched all three reference images exactly on M4.
- Three M3 30-frame timing runs: 9.961426341, 9.859081300, 9.903089772 s.
  Median 9.903089772 s = 3.02936 replay FPS, up 4.34% from 2.90322.
  These are saved-workload replay rates, not live gameplay frame rates.

Satisfactory output remains 3840x2160, internal 2880x1620 with its captured
settings and previously documented feature fallbacks. Images and full logs stay
in the private local gpu-replay-bench repository. Every hardware run retains
its 60-second watchdog and thermal guard. This is regression evidence, not
independent Vulkan conformance or blanket application qualification.

Separate compute+fragment and compute+vertex descriptor specialization
experiments both changed RGB values at zero tolerance, so neither is enabled.

## Atomic-loop compiler validation follow-up

A retained compute input exposed native retry-loop backoff being inserted after
an existing logical BREAK. Its ordinary SSA immediate must precede the logical
terminator, while the execution jump/pop remain afterwards. The debug compiler
rejected the previous order at IR translation; moving the insertion cursor makes
that same input compile with validation enabled (60,538 bytes). All 296 host
compiler tests pass. The private input hash begins `10fe357e9ecdc845`.

An isolated build containing this fix and compute GCM without value numbering
matched Satisfactory's three RGB references exactly on M3. That motion experiment
had no measured benefit and is not enabled. A separate compute constant-program
experiment changed later images and was rejected. Neither supplies a new score.
