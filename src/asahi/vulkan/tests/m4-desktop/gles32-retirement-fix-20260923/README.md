# G16 ES 3.2 retirement repair validation

Kernel source commit `aa0c3dcad374` corrects signed-wrap notifier retirement,
requires completed private UMA counters before owner reuse, and rolls back
unpublished packet reservations. The ordinary module SHA256 is
`afc871fb4273720d8825c644cf7823a10457ed9eb16a9e9a3f47303978357956`.
The full regression is paused at 102,857/326,602 pairs after segment 41,
with no Fail, Timeout, or Crash so far. See `handoff-102857/` for the exact
indexed checkpoint and clean restart point. This is partial validation.

The temporary boundary-test module SHA256
`a6011f771a03f38c786eac21e232d1f4b360f48d8a5520dd80b4d3697da687f5`
has the same repair with its initial render stamp set to `0x7ffff000`; the
ordinary module uses zero. It was activated by a guarded reboot, ran five
guarded segments, and was replaced by the ordinary module via another guarded
reboot. The segment inputs retain the original full catalog's indices and
case identities. The high-stamp results are 4,778 Pass, 36 NotSupported,
zero Fail/Timeout/Crash across 4,814 pairs:

| Segment | Original issue | High-stamp guarded result |
| --- | --- | --- |
| 22 | Seven image failures, tiling timeout | 2,466 Pass / 34 NS, four workers |
| 142 | LOD-bias failure | 1 Pass, one worker |
| 143 | Tiling timeout | 53 Pass, four workers |
| 105 | Point-mode case timeout | 1,129 Pass / 1 NS, one worker |
| 278 | Firmware stamp-order fault | 1,129 Pass / 1 NS, one worker |

These passing trials exercise the signed half-range quickly, but intermittent
faults need the full sustained run for stronger validation. Original failures
and fault dumps remain in `../gles32-regression-20260922/`.
