# Steam X11 and FEX bring-up checkpoint, 2026-09-18

Source fixes are retained in the workspace's reference repositories:

- muvm `8038805`: preserve server ordering for synthesized DRI3Open replies.
  The pure wire-protocol probe fails all 32 rounds on stock muvm and passes
  all 32 after the fix. Keep the Xlib assertions enabled.
- FEX `5c19579`: sparse Linux lookup caches commit writable L2 backing on use.
  The same 32-thread guest under 4 GiB RLIMIT_DATA crashes the previous FEX
  and passes the fixed version; excessive guest allocations still fail.
- muvm `f3002a4` retains the desktop/CDP/memory-navigation diagnostic helpers.

Installed Steam uses the private guest bridge under
`/usr/local/libexec/m4-steam/muvm-guest`. FEX is installed at
`/usr/bin/FEXInterpreter`, with the prior binary retained under
`/opt/m4-steam/fex/FEXInterpreter.before-lookup-cache`.

Twelve Library/Home transitions preserve SharedJSContext with accelerated CEF:
`20260918T000509740526Z-steam-fixed-menu-quiet-verified` (peak 67 C).
Archive traffic was paused for that run. A prior run timed out on a 5-second
CDP invocation without a renderer crash; do not count that as a crash or pass.
Factorio installation/application qualification is still in progress here.

`guarded-command.py` now retains a complete continuous kernel stream on short
runs as well as long ones. A slow optional journal query records its error and
partial output, while mandatory live/final kernel evidence and all watchdog,
thermal, fault, and long-run fresh-retirement conditions remain enforced.
This addresses a real journal timeout during concurrent Btrfs archive work
which previously latched watchdog recovery despite a successful workload.

The development archive helper seeds verified rsync copies with local evidence
hardlinks and removes only copied older target experiment files. It must never
use rsync --inplace, because that could modify original local evidence.
