# M3 CTS repair 01

Exact M4 CTS SHA256 `36bcc144d4b3c0edc5b638f20331fbc4e1be388d89da652890af870b8e5697dc`, unchanged tests. Selected 30 failures from the seed-2026092002 10,000-case baseline. 27 Pass, three remaining Fail, no losses/crashes; watchdog and thermal guard completed, peak 50.5 C. Original sample and catalog indices retained in cases.meta.json. Full QPAs and guard evidence are in build/m3-cts-repair-01 on the development host and /root/m3-cts-repair-01 on fedora-m3.

Keep explicit f16-to-f32 widening before AGX3b transcendental operations. M3 RG32 signed/unsigned end-of-tile exports use full-width I32 conversion, as M4 does; the G17-specific expanding conversions corrupted output. M3 also shares M4's ordinary signed RGBA16 export, including MRTs.

The three remaining failures are texture instruction forms (catalog indices 787042, 784897, 784847). This subset is not a full regression or conformance claim.
