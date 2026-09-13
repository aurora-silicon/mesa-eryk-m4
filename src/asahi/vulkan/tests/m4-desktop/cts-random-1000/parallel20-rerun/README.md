# Frozen 1,000-case rerun, twenty concurrent CTS processes

**849 Pass, 151 NotSupported, zero Fail, Crash, Timeout or HarnessError**.
Every case and outcome matches the preceding four-process run. The frozen
baseline is unchanged; unsupported cases are not counted as passes.

| Concurrent CTS processes | Total elapsed | Batch execution wall time |
| --- | ---: | ---: |
| 4 | 129.124 s | 90.447 s |
| 20 | 86.531 s | 58.332 s |

This run was **1.492x faster overall**, taking **33.0% less wall time**.
The 86.531-second total includes guard setup, SSH and evidence collection;
58.332 seconds were spent executing bounded test batches. Setup/development
before campaign start is excluded. Per-case elapsed times overlap and must not
be summed as wall time. All six batches reached 20 concurrent processes.

The 60-second watchdog and six-channel thermal guard were active throughout.
Peak **62.75 C**, no guard failure or reboot. Runtime binary/guard/CTS hashes
and boot identity are identical to the four-process run; no driver changes were
made. Normal shader-cache policy was retained and caches were not reset, so the
observed speedup is not a controlled concurrency-only benchmark.

`comparison.json` records both timings and the identity/result checks.
`campaign.json`, `summary.json`, and `results.json` preserve timing, complete
CTS outcomes and links to all six guarded evidence directories. The CTS binary
retains the previously documented historical input/oracle corrections in
`../fixes/`. This random sample is not a full Vulkan conformance result.
