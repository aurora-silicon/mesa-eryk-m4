# Second retained GPU incident: segment 143

GPU job2822139 stalled in Tiling, context35, zero render timestamps. A sealed
1,681,584-byte snapshot and original logs are retained. Peak experiment99.25C;
no thermal guard failure. Four workers active: negative vertex-shader compilation
checks, not sufficient to assign the stalled render job to any one CTS case.
The preceding isolated LOD-bias case (global194699, session38, EGLconfig12) failed
at its first bias pair: magenta instead of red. Its guard passed; original Fail stays.

Watchdog recovery boot28e5b38a-2164-479a-8bbd-24e88008f8d4. Desktop/autologin restored,
watchdog60 armed. Unchanged pinned library/CTS hashes. Four interrupted cases Pass
serially; preceding LOD-bias case Pass in39s; exact53-case four-worker repeat Pass53.
Review resumes only41 unfinished original cases serially before remaining plan.
Both this incident and segment22 remain unresolved; no driver fix claimed.

Both dumps show PPP_SCREEN register0x10129 value0x03ff03ff and TE_SCREEN0x101b9
value0x1f01f: 1024x1024 render bounds, matching the headless output size. This is a
clue toward compositor/context attribution, not proof of the originating process.
queue/render.rs encodes PPP_SCREEN as x_max | y_max<<16; original logs retained.
Both show gate2/free-engine-maskffff and parameter hardware descriptors different
from the stalled manager address. Snapshot differences alone do not prove corruption
or lifetime errors because other contexts may have used the hardware slots.
