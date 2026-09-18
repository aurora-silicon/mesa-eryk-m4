# Steam X11 and FEX bring-up checkpoint, 2026-09-18

Source fixes are retained in the workspace's reference repositories:

- muvm `8038805`: preserve server ordering for synthesized DRI3Open replies.
  The pure wire-protocol probe fails all 32 rounds on stock muvm and passes
  all 32 after the fix. Keep the Xlib assertions enabled.
- FEX `5c19579`: sparse Linux lookup caches commit writable L2 backing on use.
  The same 32-thread guest under 4 GiB RLIMIT_DATA crashes the previous FEX
  and passes the fixed version; excessive guest allocations still fail.
- muvm `30ce796`: fix dropped X11 bytes on short writes and wait for complete
  DRI3 headers before reading their fields. The forced short-write/socketpair
  test fails before the fix and passes after it; live fragmented QueryVersion
  passes in `20260918T002648277878Z-x11-final-fragment-probe`.
- muvm `f3002a4` retains the desktop/CDP/memory-navigation diagnostic helpers.

Installed Steam uses the private guest bridge under
`/usr/local/libexec/m4-steam/muvm-guest`. FEX is installed at
`/usr/bin/FEXInterpreter`, with the prior binary retained under
`/opt/m4-steam/fex/FEXInterpreter.before-lookup-cache`.

Twelve Library/Home transitions preserve SharedJSContext with accelerated CEF:
`20260918T000509740526Z-steam-fixed-menu-quiet-verified` (peak 67 C).
Archive traffic was paused for that run. A prior run timed out on a 5-second
CDP invocation without a renderer crash; do not count that as a crash or pass.
Factorio subsequently reached a playable Freeplay world through Steam; see
`../factorio-query-order-20260918/README.md` for final application qualification,
the additional GPU query-order fix, swap reserve and remaining visual defects.

`guarded-command.py` now retains a complete continuous kernel stream on short
runs as well as long ones. A slow optional journal query records its error and
partial output, while mandatory live/final kernel evidence and all watchdog,
thermal, fault, and long-run fresh-retirement conditions remain enforced.
This addresses a real journal timeout during concurrent Btrfs archive work
which previously latched watchdog recovery despite a successful workload.

The development archive helper seeds verified rsync copies with local evidence
hardlinks and removes only copied older target experiment files. It must never
use rsync --inplace, because that could modify original local evidence.

The final bridge is installed and active following
`20260918T002603515553Z-steam-final-transport-start`. This normal Steam restart
removes the temporary CEF remote-debugging flag. The shutdown-time D-Bus fatal
at 00:26:08 is from terminating the prior VM, not a new menu-navigation crash.

Cleanup moved historical driver libraries and completed experiment captures to
`build/m4-offload-20260918/` on the development workstation. All 1,653 library
archives passed SHA-256 verification (1,167 unique inodes, zero mismatches).
Transfers use receiver-verified rsync before removing source files; original
captures and restore paths remain available. The target changed from 96 GiB
used / 3.8 GiB available to 50 GiB used / 47 GiB available, including the newly
installed Factorio and Steam runtime. The Linux filesystem is approximately
100 GiB; cleanup does not resize partitions or remove user saves.
