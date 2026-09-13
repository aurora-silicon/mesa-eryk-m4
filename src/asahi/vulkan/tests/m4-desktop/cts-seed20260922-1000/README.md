# Fresh random CTS sample: seed 20260922

1,000 unique cases drawn uniformly from the same CTS 1.0.2.6 must-pass list,
using `random.Random(20260922).sample`. The recorded Linux-only selection
excludes Win32 and Android cases before drawing. No results-based exclusions,
redraws, software changes or retries were made.

| Outcome | Count | Rate |
| --- | ---: | ---: |
| Pass | 914 | 91.4% |
| NotSupported | 76 | 7.6% |
| Fail | 9 | 0.9% |
| Crash | 1 | 0.1% |

Pass rate among supported attempted cases: 98.918%. This random sample is not
a Vulkan conformance claim. `failures.tsv` contains all ten failing/crashing
cases, including four scaled-to-float linear blits, two tessellation cases,
one render-pass allocation case, one cube gather, one texel-view write and
one depth-attachment assertion.

Twenty workers; 30-second per-case timeout; each batch used the 60-second
hardware watchdog lease and mandatory six-channel 0.5-second thermal guard.
Peak measured temperature: 62 C. Batch execution took 59.459 seconds; guarded
campaign segments including transport/evidence overhead took 78.212 seconds.
First launch through final completion took 101.554 seconds, including the
pause to inspect the process crash. Exact timestamps and definitions are in
`summary.json`.

Case 697 aborted in Mesa's `hk_CmdBeginRendering`, hk_cmd_draw.c:1118: the depth
format assertion does not accept X8_D24. The guard completed normally, all
already-running tests drained, and the batch stopped after case 711. The GPU
and display remained healthy (display failed counter zero, unchanged boot ID,
watchdog idle/armed). After inspecting the saved assertion and guard evidence,
we resumed at case 712. The crash remains in the original result; there was no
reboot or replay. See `crash-697.log` and `recovery-697.json`.

Mesa source checkpoint: `cdac6e02b8a`, including native 2x/4x storage images.
The installed library is the identical qualified pre-commit candidate from
`cts-msaa-20260914`, whose executable hash is retained in this report. CTS uses
the previously documented `deqp-vk-scaled-border-reference` build. Exact Mesa,
CTS, guard and kernel identities and all original evidence paths are recorded
in `summary.json`; the historic test/reference corrections are unchanged.

Reproduce from the workspace root by copying `cases.txt` to the target and
using `tools/cts-campaign.py --jobs 20 --case-timeout 30 --count 1000` with
`/root/cts-1.0.2.6/deqp-vk-scaled-border-reference`. Keep the mandatory watchdog,
thermal guards and explicit inspection on any crash. Original QPA logs and
compiler captures are retained in the experiment directories named in the
report. Fixes and retests should be reported separately from this frozen run.
