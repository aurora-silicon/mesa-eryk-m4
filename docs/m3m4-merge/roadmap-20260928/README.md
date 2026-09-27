# Roadmap failure-handling checkpoints

See `failure-ownership-audit.md` for ownership boundaries and remaining proof
gaps. Test summaries preserve hardware success separately from source/mock
checks. The M3 campaign now uses the explicitly reviewed P1/P2 kernel checkpoint;
see `m3-memory-validation.md`. Original outcomes and revision history remain.

Tools in this directory are versioned copies of workspace-root `tools/`.
Run the tests from that workspace root, with kernel checkouts in `linux/`:

```
python3 tools/test-m3-packet-completion.py
python3 tools/test-g16-scheduler-timeout.py
python3 tools/test-g16-fail-all.py
python3 tools/test-fault-dump-bounds.py
python3 tools/test-fault-cause.py
python3 tools/m3-gpu/test_fault_snapshot.py
```

The Rust-method extraction tests use observable mocks for fences, VM references,
allocators and readers. They check real method control flow, not kernel locking,
GPU retirement or live allocation pressure. Hardware experiments still require
the 60-second watchdog and the existing all-channel thermal guards.

`idle-display-session.py` runs inside the guard before a deployment/reboot. It
waits at most ten seconds for session processes to release DRM files after
systemd stop completes; it does not change the default boot target. The previous
immediate fuser check could report a spurious deployment failure while processes
were still exiting.

`guarded-command.py` collects any newly sealed firmware snapshot through the
bounded M4 parser; that parser verifies its format before copying it. The guard
also treats the first-runtime-error marker as a failure even if no active job
was present. Fault injection must retain that failure verdict, not relabel it
as a rendering pass.
