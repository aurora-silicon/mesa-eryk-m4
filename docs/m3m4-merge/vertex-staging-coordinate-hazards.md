# Vertex sample/fetch staging dependencies

Descriptor prefetch exposed a native G15S vertex scheduling error in the
retained Minecraft Vulkan gameplay replay. The world was unchanged, but the
player's hand became black in all three checked frames. Source bisection
isolated vertex shader
`21d0bb6c0d93a5741640191b7aeb19619c0b190e43cb07bbe08a3a07e642e873`.

An explicit register-LOD sample used coordinate bank 0 and completion slot 5.
A later integer fetch received completion slot 1, which selected coordinate
bank 0 too. Vertex fetches were omitted from the staging dependency graph
because their banks were selected after slot assignment. The fetch could
therefore overwrite the unfinished sample's coordinates. The ordinary
shader's extra descriptor reads changed the slot sequence: its later fetch
used bank 32 and did not expose this overlap. Descriptor contents themselves
were not the cause.

Choose vertex fetch banks from the initial slot assignment, add their actual
staging writes to the dependency graph, then assign completion dependencies
again. Keep those bank choices fixed through emission. This handles overlaps
in both instruction directions and retains parallel execution for disjoint
banks. Fragment scheduling, AGX3C and the descriptor-prefetch default remain
unchanged.

Validation on 2026-10-03:

- The new backend regression fails on the original packer and passes after
  the fix. It checks a sample in bank 0/slot 5 followed by a fetch in bank
  0/slot 1, and an independent fetch in bank 8 that needs no sample wait.
- All 322 compiler tests pass. Host and AArch64 Vulkan targets build.
- Guarded M3 replay checks with compact descriptor prefetch disabled and
  enabled both complete all 30 frames. Frames 1, 16 and 31 match the approved
  baseline exactly in RGB. Each run verifies a 60-second watchdog and retains
  all-channel thermal evidence.
- Full serialization and serialization before loads/ALU also repaired the
  original replay; serialization only at vertex writeout did not. Those are
  diagnostic controls, not the implemented fix.

Raw receipts, shader inputs, bisection and comparisons remain in the local
benchmark study `diagnostics/m3-macos-driver-gap-20261003/native-descriptor-prefetch`.
This is M3 replay validation, not a full conformance or M4 hardware claim.
