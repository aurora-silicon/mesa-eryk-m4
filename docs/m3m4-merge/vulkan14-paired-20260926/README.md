# Paired full Vulkan 1.4 regression campaign

Started September26 at the user request, concurrently on M3 Pro and M4.
This is a regression run, not a Khronos conformance submission. It is in progress.

Each machine selects all3,206,030Linux cases from the frozen1.4.6.2catalog,
SHA25672c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
The executable is the existing corrected CTS used by the last completed full
M4campaign, SHA25636bcc144d4b3c0edc5b638f20331fbc4e1be388d89da652890af870b8e5697dc.
No CTS source or tolerance changes were made for this run.

Both use merged Mesa3c7ec4838f2, Vulkan library372e6351a85b681e7d42731f4dd1c988e0fc7a640a6859a82ebcfc07c791999f,
with default compiler settings and no Satisfactory opt-in flags. Exact driver
environment, kernel/module identities and every indexed outcome are retained
under `build/cts-paired-20260926/{m3-pte-fix,m4-full}` in the workspace.
Each has progress.json, failures.tsv, results.sqlite3, and immutable segments.

The retained tools mirror workspace-root tools. Run from that workspace:

```sh
python3 tools/cts-paired.py --machine m3 --output build/cts-paired-20260926/m3-pte-fix --run
python3 tools/cts-paired.py --machine m4 --output build/cts-paired-20260926/m4-full --run
```

Run these in separate host processes. One worker per GPU; no overlap on a
single device. M4 uses the existing bulk/experiment guard and SSH watchdog
broker. M3 uses its existing retirement/thermal guard and qualified deqp
backend. Both retain watchdog60seconds, all-channel0.5s monitoring, startup
below95C, regulation110C/emergency120C, and a60second CTS case bound.
No clock or thermal limits were raised. WSI uses the existing private headless
compositors; its environment remains distinct from panel qualification.

M3 begins each family with16cases and aims for15seconds of test work based on
preceding successful same-family duration, growing at most twofold to4096cases.
This avoids carrying a GPU-retirement lease into prolonged CPU-only API work.
It changes scheduling only; it cannot remove a case or overwrite an outcome.
M4 retains10000-case segments (100forWSI) bounded by its existing900s runner.
Fatal/incomplete segments stop for explicit evidence review. Only uncompleted
indices can resume; terminal failures are never automatically retried.
Creating STOP in a campaign directory stops its next scheduling boundary.

Preliminary original-CTS attempts are retained separately in m3/ and m4/.
They are not folded into corrected-build coverage. Those probes found the
known scaled-format reference failures and two M3 watchdog recoveries caused
by oversized CPU-query batches, not established GPU faults. The scheduling
correction retains the existing guard thresholds.

Offline selection regression and Python compilation checks passed. Hardware
qualification and status snapshots will be recorded as the campaign advances.

## First regression and current continuation

M3 sparse-buffer creation spent about 95% of a task-clock profile in uncached
PTE scans. Acquiring every PTE serialized host-only page-table inspection.
Kernel `efd38da3d7c7` (development commit `5bd273f6a511`) keeps the same ownership
and permission checks but uses Relaxed reads in three scans of exclusively
host-owned M3 DMA tables. Shared roots retain Acquire. The isolated 3,255-check
sparse-buffer case passes in 11.30 s; the original isolated diagnostic was
still running when interrupted at 26.1 s for investigation, so no exact
speedup is claimed. See the kernel's `tests/m3-sparse-pte-20260926/` evidence.

The earlier corrected `m3-full/` campaign remains stopped with its original
failed guard evidence; `m3-pte-fix/` starts the entire catalog again at zero.
M4's original corrected campaign continues unchanged. The sparse tests remain
selected and fully exercised; no limits or capabilities were reduced.

M3 grouping also consults prior per-case durations from the frozen September 20
full campaign with a 4x time allowance and 15 s aggregate target. A long case
is isolated, not skipped. Prior outcomes never affect selection or verdicts.
The controller stops before a new segment if host free space falls below 2 GiB.
Obsolete generated ELF object files were removed to reclaim space; captures,
all test evidence, source trees, libraries and executable artifacts remain.

The committed progress files are dated snapshots, not live or final results.
`build/cts-paired-20260926/active.json` identifies the live campaign directories.
The new kernel has passed the isolated check; broader CTS qualification is
ongoing. There is no claim of a complete passing suite yet.
