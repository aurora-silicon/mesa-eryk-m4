# Steam startup diagnosis, 2026-09-18

Steam startup is substantially CPU-bound in FEX's compiler, with additional
web-helper reinitialization and CPU thermal throttling. This is a diagnosis,
not a startup-performance fix or evidence that FEX-generated code is generally
slow. No FEX/Mesa/kernel implementation or installed launcher was changed.

## Measured findings

The useful profile is
`_provenance/experiments/20260918T092054127184Z-steam-startup-jit-profile/`.
Guest native perf sampled the Steam descendant tree at 99 Hz using
`cpu-clock:u`, starting before its first fork. 7,033 samples, no lost samples:

- 4,896 samples (69.61%) landed in the native FEX executable.
- 3,852 samples (54.77% of all samples) map directly to compiler functions:
  IR passes/emission, x86 decoding, ARM64 code generation and GenerateIR.
  This excludes supporting allocator/container functions, so it is a
  conservative classification of compilation-related CPU work.
- Register allocation Run: 679 samples (9.65%); AssignReg: 115 (1.64%).
- ARM64 CompileCode: 258 samples (3.67%); dead-flag ProcessBlock: 240 (3.41%).
- The other samples include translated application code, runtime support,
  native libraries and small native launcher overheads. These are sampled
  user CPU shares, not percentages of wall time or GPU utilization.

`profile-summary.json` retains exact counts and resolved function names.
FEX is Release/-O3 and creates an Arm64 JIT backend unconditionally in
ContextImpl::InitializeCompiler. The name FEXInterpreter denotes its Linux
executable entry point, not instruction-by-instruction x86 interpretation.
Default persistent code caching is disabled and named EnableCodeCachingWIP;
it was not enabled for this investigation. In-process translated-code reuse
still exists. No claim is made that all compilation is redundant.

Follow-up clarification: this is the older FEX-2604 code-map/offline-compiler
cache, not the newer automatic JIT disk cache. The local code identifies
executables by path in CodeCache::ComputeCodeMapId and leaves CodeCacheConfigId
at zero with a TODO to distinguish active configurations. These are concrete
unfinished cache-identity details; they do not establish a cache-related bug in
our uncached launch.

Upstream's September release introduces a separate DiskCache option enabled
with FEX_DISKCACHE=1. Its release notes explicitly describe unbounded cache
growth for JIT applications, missing size limits/stale-entry cleanup, and
possible invalidation conflicts after file contents change. These limitations
belong to that release, not a measurement of our old build. Our FEX-2604 tree
does not contain the DiskCache option. Before testing the new implementation,
update FEX while preserving/reassessing our allocator and lookup-cache fixes;
merely enabling EnableCodeCachingWIP is not the same experiment.
Source: https://fex-emu.com/FEX-2609/ (published September 7, 2026).

The initial user launch logged 33.15 seconds of Steam system startup after
verification. Verification itself took about four seconds, update checking
about one second; authentication connected within about two seconds of LogOn.
The instrumented launch logged 35.99 seconds, and the final ordinary launch
42.34 seconds. These are Steam's own internal startup intervals, NOT
click-to-interactive measurements. Thermal state and instrumentation differ;
they must not be presented as an optimization A/B result.

## Thermal contribution

An initial effective 1.8 GHz reading was insufficient to identify the cause
of the cap. Explicitly restoring cpuinfo_max_freq did not deliver sustained
4.104 GHz: the kernel thermal cooling devices continued to constrain it.
The first run observed performance-core ceilings from 912 MHz to 2.352 GHz.
The profiled run observed 912 MHz to 2.352 GHz with aggregate readings
60.75--67.75 C. The installed J713 policy starts cooling at 70 C, releases
below 60 C, and polls every 25 ms. Its hysteresis permits continued cooling
below 70 C after an earlier threshold crossing. Both cooling-device states
were observed near their maximum cooling levels.

This can amplify startup latency. Neither the fraction of wall time attributable
to throttling nor an improved safe policy is established here. Keep sensor
failure protection and CPU thermal control enabled; do not treat the 95 C
experiment cutoff as a replacement CPU policy. No trip, hysteresis, polling
interval, governor, or sensor setting was changed. Normal requested CPU maxima
were restored; thermal QoS can still lower the effective ceilings.

## Reinitialization remains unresolved

steamui_html.txt explicitly recorded Restart webhelper process followed by
Shutting down webhelper process at 09:10:11 in the user launch, and at
09:21:54 and 09:22:27 in the profiled launch. Browser recreation retries follow.
The restart trigger has NOT been established. Do not relabel these as proven
FEX crashes, renderer crashes, shader failures or normal login behavior.
The warnings about crash-reporting keys alone are not crash evidence.

Next useful work: determine the explicit restart trigger, distinguish necessary
compilation from invalidation/recompilation, then test a narrowly scoped
compilation optimization. Persistent caching is experimental; do not blindly
enable it or disable Chromium's sandbox as a performance fix. Any smaller-block
experiment must assess steady-state cost and avoid changing game behavior.

## Reproducibility and cleanup

Source identities: kernel d9c4a7c37eb7, Mesa 49a8f9b26b0, FEX 5c19579;
Steam's guest graphics overlay remains mesa-multilib-r10.erofs. Installed FEX
build ID 7835672270fec4f01e34a7f34f42702c46b95844. Relinking the exact objects
without --strip-all generated offline symbols with a different build ID;
its .text was compared byte-for-byte with the installed-build ELF. Both
.text SHA256 values are
103ff37e7c630f8b2944cbb44d804295a93923af805e35643019779647306a3d.
For unresolved perf DSO offsets, add 0x10000 to convert this ELF's executable
segment file offset to symbol VA, then resolve with nm -n -S -C. Do not use
the older FEX-symbols ELF, whose code/build ID differs. The symbolized ELF
was NOT installed and the build-ID cache was not falsified.

The initial 091824 full-profile experiment missed descendants because perf
attached after the first fork. Its clock/timing evidence is useful, but its
nearly empty perf file is NOT a valid CPU profile. The helper now holds the
launcher at SIGSTOP until perf is attached, then resumes it.

The final ordinary launcher was restored in
`_provenance/experiments/20260918T092557212519Z-steam-startup-normal-restore/`.
It uses /usr/local/bin/steam and the unmodified private muvm wrapper with no
profiling environment. All three bounded guard runs retained watchdog and
six-channel thermal evidence; peaks were 66.5, 67.75 and 66.25 C. A guard
success means successful evidence collection, not a Steam speed/stability pass.
Raw application/process logs stay in private provenance because they contain
account information. No games were running or terminated.

The reusable root tools/steam-startup-profile.py is mirrored here. Copy it to
/home/e/steam-startup-profile.py on the target and invoke host mode only under
tools/gpu-experiment.py with a duration allowing shutdown, VM startup and
collection. --normal-launch returns to the installed launcher after profiling.
Successful runs leave the ordinary application open; guard interruption stops
the detached experiment VM. The final interruption-cleanup hardening was
reviewed/compiled but not fault-injected on the user's desktop.
