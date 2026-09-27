# First failure, fence completion and retained DMA ownership

Scope: native M3 G15S and M4 G16G runtime/submit/RTKit/DRM frontends, common
VM job guard and Mesa VM-status consumption. This is an incremental source
review, not proof of safe hardware reset or an exhaustive allocation audit.

## Publication and ownership

| Boundary | M3 | M4 | Failure contract |
| --- | --- | --- | --- |
| Decode/admit | Packet holds VM, commands, timestamp mappings, attachments and VM job guard | Packet also retains notifier, predecessor and cache references; runtime allocates a disjoint bank | Before publication, local objects unwind; report the error without reusing an active hardware bank |
| Publish | Runtime retains packet and native jobs; sets `gpu_pending` before the first queue write | Runtime reserves in-flight capacity first; sets pending before queue writes and retains every prepared Active even if the last mailbox send fails | Any fallible operation after publication may leave DMA active; latch unhealthy and keep the ownership graph |
| Retire | Requires stamps, events, all queue consumers, engine and pipe idle | Requires stamps, queues, pipe state and relevant engine/dependency retirement; preserves previous slot owners where firmware may cache descriptors | Only demonstrated retirement permits successful fence completion and release of VM active-job ownership |
| Error fence | One atomic completion claimant records VM error and signals fence | Same; global failure visits every in-flight and prepared packet | An error fence wakes userspace but does not prove DMA retirement |
| Cancel | Packet result is immutable once claimed | Pending-only cancellation can drop unpublished owners; in-flight cancellation loses the global runtime and completes all admitted packets | Keep mappings and VM guards for unretired work |
| Scheduler deadline | Runtime executes bounded batches; a healthy/retired packet returns NO_HANG | Checks the current packet identity/deadline, allowing a retired packet or fresh successor | Scheduler wall time is not by itself proof of a hardware hang; removed runtime returns ENODEV and completes its packet |
| Runtime removal | Registration is removed, health fails, shared runtime is taken under its mutex | Same; explicitly error-complete admitted packets before retaining runtime | Existing files retain a disconnected backend, not freed platform state |
| Unproven DMA/ASC stop | ManuallyDrop retains the runtime and pins module code | Same | Stopping ASC alone does not establish stopped GPU DMA; recovery still needs reboot |

`T8140VmJobGuard` ties queued work to a VM's active-job count. Successful packet
completion can release it early; error completion retains it because command
buffers and firmware may still reference that VM. The runtime graph, rather
than the userspace process's lifetime, owns the unretired commands. M4's pending
batches are unpublished and may be freed after error completion; a surviving
scheduler packet can keep its guard until the packet itself is dropped.

Mesa's status path reads VM errors/global health and reports
VK_ERROR_DEVICE_LOST, including when checking an already-signaled fence. A late
scheduler callback therefore must not turn a successful immutable fence into a
new VM error. Host tests extract and exercise the production completion methods
with observable fences and ownership drops.

## Error paths addressed in this checkpoint

- M3 atomic completion claimant: no duplicate fence signaling or late mutation.
- M3 healthy scheduler deadline: reinsert instead of marking the runtime lost
  after waiting for a valid long packet's mutex. The per-batch deadline remains.
- M4 global error: finish every admitted packet, retain all published Active
  objects, discard only never-published pending owners.
- Removed runtime: both timeout callbacks complete the packet with ENODEV.
- Submission-publication failure: retain the original error and first snapshot;
  allocation/diagnostic errors must not overwrite that error.
- Runtime/firmware/thermal/boot errors: request one first-failure snapshot per
  runtime, with a versioned host errno record. Capture allocation failure is
  logged, not repeatedly retried on every cancellation.
- RTKit crash: wake and queue the existing completion worker; no mutex or
  snapshot allocation in the callback. A crash without a final completion
  notification must still reach idle-runtime error handling.
- Snapshot storage is bounded at 2 MiB for M3 and 16 MiB for M4. Failed reads
  roll back the partial record; arithmetic/counter overflow is rejected before
  calling its reader. Allocation failure remains a normal diagnostic failure.

## Remaining proof gaps

- No qualified stop-DMA/reset/reinitialize sequence; no claim of post-hang
  rendering without a reboot. Recovery state and context generations need work.
- Fault-only allocations still use GFP_KERNEL and may enter reclaim. Size caps
  are not a reserve/mempool and do not prove bounded allocation latency under
  severe memory pressure.
- M3 snapshots still lack active shader/command storage and job IDs. M4 records
  owned command buffers but does not dump application BO contents.
- Startup failures before a Runtime/Config exists lack these owned-memory
  snapshots; RTKit's own crash log is a separate source.
- M3 publishing source retains older opt-in recovery diagnostics and unlocked
  waiting. It is built separately; its build is not deployment evidence.
- Destruction with active DMA, per-client isolation, queue fairness and memory
  reclamation need guarded hardware campaigns beyond source/mock tests.
- DRM wedged notifications and a common first-fault/progress ABI are not yet
  integrated. The private debugfs progress file is not a stable userspace ABI.

## Validation at this checkpoint

M4 `05cc062c9266`: 88 rendering smoke + 140 logical CTS Pass after reboot;
normal display manager active. M3 `52b06070669f` / publishing `6fb5196678f7`:
separate module builds and 200 rounds of concurrent completion/cancellation,
late timeout, first-error and retained-owner host tests pass; not yet deployed
because the full M3 catalog continues on its reviewed kernel identity.

First-fault hardware injection and final revisions/results are recorded in a
separate receipt; compilation alone does not validate failure behavior.
