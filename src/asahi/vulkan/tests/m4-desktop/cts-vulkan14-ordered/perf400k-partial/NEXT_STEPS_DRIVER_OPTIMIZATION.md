# Handoff: M4 G16 driver throughput and completion visibility

Written 2026-09-15 by Claude for Codex. Read `/home/council/dev/dirtyroom_m4/AGENTS.md`
first; watchdog, thermal, evidence and push rules apply unchanged. All paths are
relative to `/home/council/dev/dirtyroom_m4` unless absolute.

## State at handoff

- Kernel `linux/linux-asahi`, local branch `m4-asahi-port`, pushed to
  `dirtyroom/eryk_m4_gpu` (push with `git push dirtyroom HEAD:refs/heads/eryk_m4_gpu`,
  never `git push dirtyroom HEAD`). New commits, newest first:
  - `f35a67b8883c` docs: M4_PORT.md section "Serialized submission throughput and
    completion visibility (2026-09-15)". Read it; it is the condensed record.
  - `9b93f16a6ffb` idle power-off delay 0 (Globals+0x9b8) and a 1 ms fence settle window.
  - `0e7b51c9aeb1` compute owner recycling per queue, consumed-page private pool
    clears, staged freelist/page-list writes, the settle mechanism, split
    retired/completion fence plumbing (retired fence unused by the scheduler yet).
- Installed target module (`root@omarchy-m4`,
  `/lib/modules/7.1.9-dirtyroom-asahi1+/kernel/drivers/gpu/drm/asahi/asahi.ko`)
  SHA256 `9b46d8104fa82babe11753df932d3b80f805915f063ebd23feb7d464c167fb59`, built from
  commit `9b93f16a6ffb` (the docs commit changes no code). The kernel working tree is
  clean at `f35a67b8883c`.
- Mesa `mesa/m4-asahi` (`m4-asahi-port`, pushed to `upstream/m4-asahi-port`): no driver
  change was needed for throughput. The corrected CTS executable on the target is
  `/root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit` (SHA256 `36bcc144…`), from the
  cumulative patch under
  `src/asahi/vulkan/tests/m4-desktop/cts-vulkan14-ordered/rest70k/cts-scaled-blit.patch`.
  All 56 former rest70k failures were CTS reference defects; see that README.
- Tools: `tools/cts-bulk.py --env KEY=VALUE` (repeatable, recorded) passes driver
  environment overrides to the remote runner. `tools/README-cts.md` documents it.
- Target: display manager active, CPU caps normal, watchdog broker idle/armed,
  no experiment running. The host heartbeat daemon (`ssh-watchdog.py heartbeat`) runs.

## Measured results (same frozen catalog segments, 20 workers, 60 s case bound)

| Segment | Old driver | Now |
| --- | --- | --- |
| 0 to 10k | 104 s | 91 s (stopped by the trim timeout, see below) |
| 10k to 20k | 453 s | 196 s |
| 20k to 30k | 749 s | 279 s |
| 30k to 40k | 141 s | 44 s |
| 40k to 50k | 157 s | 109 s |
| 50k to 60k | 396 s | 280 s |
| 60k to 70k | (not in a comparable earlier run) | 331 s |
| 70k to 80k | (not in a comparable earlier run) | 347 s |

Ordered slice [30000,32000) (`build/claude-perf/slice-30000-32000`): 59.2 s before,
18.1 s now, 2000/2000 both times. Per serialized GPU job: preparation 4.0 ms to
0.13-0.24 ms; retirement-to-next-publish is now bounded by the settle window below.

Partial 400k run on this kernel (`build/cts-vulkan14-ordered-perf/run2/0000000-0400000`,
segments 1-8, evidence `_provenance/experiments/20260915T02*-cts-perf400k-r2-s0[1-8]`),
stopped by the user after segment 8: 36,722 Pass, 41,275 NotSupported, 1 Fail,
1 Timeout, 1,930 Unrun and 71 Interrupted (all in segment 1, cancelled peers of the
timeout). The user wants the first 400k run completed and every error fixed; that is
your job after the driver work below, or in parallel if you judge the kernel change
cannot land quickly. Use `tools/cts-bulk-series.py --first-segment 9` on that batch
directory to continue; recover segment 1's Unrun/Interrupted cases separately as a
targeted list (never rerun completed outcomes).

Open outcomes:
1. `dEQP-VK.api.command_buffers.trim_command_pool` Timeout in segment 1. It submits
   6,000 sequential GPU jobs from one process. Isolated it takes 13.3 s now (43.8 s
   before) but under 19 competitors on a serialized GPU at ~2.2 ms per job it cannot
   finish in 60 s. The runner cancels peers on a timeout, which is why segment 1 is
   incomplete. The fix is throughput (section below), not a longer bound.
2. `dEQP-VK.api.copy_and_blit.copy_commands2.image_to_image.dimensions.src16384x6_dst16384x6.r4g4b4a4_unorm_pack16.r4g4b4a4_unorm_pack16.general_optimal`
   Fail in segment 4 (passed on the old driver). Not yet investigated. First rerun it
   isolated with `--log-images`; if it passes isolated, it is most likely the
   visibility window below at its margin for a large copy, which strengthens the case
   for the real flush fix.

## The limitation and what has been ruled out

Results of a retired command reach CPU-visible memory only after the GPU idles and
powers off. At the moment the firmware publishes every completion marker we read
(command stamps at +0xee0/+0xee4, queue and pipe consumer indices, the event ring
record, the endpoint 0x20 message 0x0042…), the data is still in the GPU caches.
Twelve bisect cycles established this; all evidence directories are
`_provenance/experiments/20260915T0*-claude-perf-kernel*`. Findings:

- Every buffer-reuse variant "failed" only because it removed the old driver's
  accidental ~1.2 ms per-job teardown before the fence. Reuse itself is sound.
- Holding the fence 1.5 ms with the GPU idle: 2000/2000. Holding only the next
  publication 1.5 ms (fence immediate): fails. Publishing the next job at retirement
  (split fence, draft `build/claude-perf/drafts/split-fence.patch`): fails and twice
  preceded a type 0x16 firmware recovery snapshot.
- Idle-off delay 50 ms: every readback stale. Idle-off delay 0 plus 1 ms hold:
  2000/2000; 0.3 ms hold: 2 failures in 2,000 (`claude-perf-kernel27b`). So the
  write-back follows power-off by a few hundred microseconds.
- ASID TLB invalidate before the fence: no effect. Snapshotting Activity254/25c/264,
  PowerPerformance, Runtime, Globals and the queue info across the window: nothing
  changes. The firmware statistics records (types 0x1b/0x19/0x1c) that follow are
  power events, 1.7-3.5 ms later and absent for 40% of jobs. PowerPerformance+0x38
  never read 0 during runs, so it is not usable as an "off" signal.
- Setting the job metadata `flush_stamps` word (compute command +0x81c, render
  metadata +0x1c; the reference driver sets it on the last render and last compute
  command of each submission, `linux/aurora-gpu/.../queue/mod.rs`) on both engines
  produced a type 0x16 recovery snapshot 0.6 ms after the next compute publication
  (`claude-perf-kernel18`). The lab's macOS reconstruction hard-coded this word to 0
  (`g16_lab/g16_lab.c` around line 11431) because its captures never needed prompt
  CPU readback. The port notes' "cold Vulkan fill still has stale first-read data"
  was this bug.

Consequently the current design keeps the DRM scheduler credit until the settled
fence signals, so the GPU idles ~1 ms after every job. Throughput is capped near
2 ms per job. The fast variants ran the trim case in 6.2 s and the slice in about
9 s, so roughly 3x more is available once completion is coherent.

## Next steps, in order

### 1. Qualify the firmware's per-command flush (the real fix)

Hypothesis: the flush path validates the user stamp pointer the way the Timestamp
microsequence op validates user pointers (against the 64 MiB window published in
HwDataB+0x28, `g16_memory::TIMESTAMP_RANGE`), and our stamps live inside the command
page, so the flush faults. A ready experiment moves the compute user stamp into that
window and sets `flush_stamps` for compute only:
- Patch: `build/claude-perf/drafts/flushstamps-windowed.patch` (applies to `f35a67b8883c`).
- Built module: `build/claude-perf/drafts/asahi-flushstamps-windowed.ko`
  (SHA256 `ad7df10fcdee8cd7a628d3fc6a50dfc2a0843e5ae874842f6b8da202b3d4932f`).
- Install with `tools/install-asahi-module.py … --label codex-flush1` after stopping
  the display manager (the installer refuses active DRM clients), then a guarded
  reboot (`/root/guarded-reboot.py --output /root/asahi-evidence/<unique>` on the
  target, wait 90 s). Run the slice:
  `python3 tools/cts-bulk.py --cases build/claude-perf/slice-30000-32000/cases.txt --runner reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner --binary /root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit --output build/<unique> --name <unique> --jobs 20 --tests-per-group 50 --runner-seconds 600 --case-timeout 60`
  It fails within seconds if visibility regresses and the kernel log shows any
  0x16 record ("G16G recovery statistics").
- If it does not fault, first keep the 1 ms settle to prove stability, then set the
  settle window to 0 (or gate the fence on the stamp alone) and rerun the slice, then
  the trim case (`build/claude-perf/trim.txt`, `--jobs 1`). If readbacks are correct
  with an immediate fence, restore the split-fence draft so the scheduler credit
  frees at retirement, and consider the staged-prepare draft
  (`build/claude-perf/drafts/stage2-pipelining.py`) afterwards.
- If it still faults, do not iterate blindly on unknown fields. Capture from macOS.

### 2. macOS capture (if step 1 faults)

Needed: a Metal program that dispatches a compute kernel, calls `waitUntilCompleted`,
and reads the result on the CPU, in two variants (one dispatch per command buffer;
two dispatches per command buffer), with the RunCompute page (0x1000 bytes), the
notifier (0x200 bytes) and the finalize record captured at completion. Compare with
`g16_compute.rs::encode_dispatch`: job metadata +0x800..+0x828 (stamp pointers, slot,
event control index, flush word, uuid, sequence), where the user stamp pointer
points, the finalize record +0xb58..+0xbd4, and the notifier's `flush_stamps` /
`notifier_flush` words after completion (`g16_lab.c` structs
`g16_fw_compute_job_meta` and `g16_fw_compute_notifier`). The last-versus-non-last
command difference is the key observation. Do the same for a render pass whose
attachment is read back (render metadata at `g16_render_command.rs` `meta` offsets).
The M4 does not need to boot macOS if the earlier lab capture arrangement is still
available; ask the user which host to use.

### 3. Regardless of the flush outcome

- Do not raise the settle window above 1 ms as a "fix" for the segment-4 failure;
  measure it first. If the flush lands, the window goes away.
- Keep the recovery record handling as is (0x16 marks the runtime failed and the
  guard reboots). Continuing after a recovery is unqualified.
- The consumed-page pool clear (`consumed_pages` from UMA descriptor +0x20) is only
  validated on CTS. Before desktop or game use, run the existing SuperTux/0 A.D.
  guarded checks; the port notes record a firmware fault when pool clears were
  removed entirely in a desktop session.
- Firmware trace ids seen right before both 0x16 snapshots: thread 1 ids 0x112
  (args [4,0x30003]), 0x101, 0x105, 0x106, 0x100, then thread 4 id 0x113 args [1,2].
  If you decode them, record the meaning in M4_PORT.md.

## Do not

- Do not reuse `build/claude-perf/drafts/split-fence.patch` or the 50 ms idle-off
  delay without the flush fix; both were measured to return stale results.
- Do not treat the 400k partial results as a benchmark of a finished driver; the
  per-segment numbers above are the benchmark for this kernel.
- Do not install or run experiments while another guard is active, and do not kill
  the host heartbeat daemon.
