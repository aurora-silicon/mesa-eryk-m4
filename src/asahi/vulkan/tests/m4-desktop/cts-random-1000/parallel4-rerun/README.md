# Frozen 1,000-case rerun, four concurrent CTS processes

All 1,000 cases completed: **849 Pass, 151 NotSupported, zero Fail, Crash,
Timeout or HarnessError**. All original 800 passes remain passes; all 47
original failures and two process crashes now pass. Unsupported cases remain
unsupported. The original case list and baseline results are untouched.

Elapsed wall time was **129.124 seconds (2m 9.124s)** from campaign start to
final evidence collection. Sum of batch execution wall times: **90.447 seconds**.
The remaining **38.677 seconds** cover guard setup, SSH/evidence transfers and
host orchestration. This excludes runner development/setup before the campaign.
Each of nine batches retained a maximum of four overlapping CTS processes;
per-case start/finish timestamps and PIDs provide overlap evidence. Individual
case durations overlap and must not be summed as elapsed wall time.

Every batch used the 60-second watchdog and six PMP temperature channels at
0.5-second intervals. Peak **62.0 C**, no guard failures or reboot; one boot and
unchanged runtime/CTS/guard identities throughout. The default display manager
remained active. Runs retained the normal shader-cache policy; this is a mixed
warm/cold-cache campaign, not a controlled compiler-speed comparison.

The installed Mesa implementation is `360e3b04daa`, library SHA-256
`b3f9a96807a5e882e3ddb84f362b8aa0de43bcb5e376ebb509bd42315cd35591`.
No driver change was made for this rerun. The separately identified CTS binary
is `deqp-vk-input-aspects-tess-chroma`, including the previously documented old
CTS subpass-input, zero-render-area and midpoint-oracle corrections. See
`../fixes/` for patches and `summary.json` for full runtime identities.

`REPORT.md` summarizes results/timing, `results.json` retains all original CTS
outcomes, `campaign.json` records nine guarded batches and timing, and
`baseline-transitions.json` gives the baseline comparison. Timing support and
parallel execution are reusable through `tools/cts-campaign.py --jobs 4`.
This random sample is not a full Vulkan conformance result.
