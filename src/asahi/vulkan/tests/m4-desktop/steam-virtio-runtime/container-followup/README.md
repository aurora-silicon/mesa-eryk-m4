# Steam container qualification and remaining emulator blocker

2026-09-16, 22:26 UTC. Apple M4 J713; Mesa e2558ae72bb remains the driver
revision. No later driver changes. The objective is correct GPU rendering.
Steam login and games have **not** passed.

## Matching Mesa inside pressure-vessel

Installed `/opt/m4-steam/mesa-multilib-r7.erofs` SHA256:
`6c0e343944a5cc1a6976899b81650a963219fbdce0abf633801a1a167ba6ef90`.
It contains matching x86_64 and i686 Honeykrisp, Zink, GLX, EGL, GBM and the
separate `gbm/dri_gbm.so` backend. Library files are at the rootfs's standard
`/usr/lib` and `/usr/lib32` locations. ICD and EGL manifests are inside the
image. Private `/opt` copies remain for direct probes, but production uses
the standard paths so pressure-vessel can capture them into its container.
The image uses 4096-byte EROFS blocks, matching the guest page size.

r27 runs the GLX64 oracle through Steam's actual pressure-vessel entry point:
Apple M4 renderer, all 256 red pixels correct, no device loss. The explicit
x86_64 dynamic loader also resolves steamwebhelper's GBM dependency.
Previous private-path overlays produced dangling container library links.

## FEX socket visibility

FEX-Emu 2604-1 uses an abstract UNIX socket and a pathname fallback under
`/run/user/1000`. Chromium's network namespace hides the abstract socket;
pressure-vessel's private runtime hides the pathname fallback. r29 fails to
start an FEX child inside nested user/network namespaces with this default.
r30 passes the identical child and the container GLX pixel check with the
existing `FEX_SERVERSOCKETPATH` option pointing into a private shared-home
directory. This does not change Chromium's sandbox settings.

The guest-session wrapper provides that per-VM socket and a native D-Bus
session. Native ARM services receive native Mesa paths; only the FEX child
receives x86 driver paths. The distribution Steam launcher is unchanged.
Source evidence: FEX tag FEX-2604, commit
9681559d56eee7eff5cd9232ca3b49ba419cb62b,
`Source/Common/FEXServerClient.cpp`, socket naming and connection fallback.

## Full-client boundary

r31 survived 75 seconds but only showed the launcher splash. Steam itself
disabled its sandbox because an earlier crash left `.cef-initialize-sentinel`.
That marker and logs were preserved under `/root/asahi-evidence/steam-r31`;
the marker was removed to restore normal sandbox initialization.

r32 and r33 keep the sandbox enabled, each observed for 120 seconds. CEF's
GPU process loads the patched Honeykrisp and Zink libraries; GPU jobs retire,
but the storage subprocess cannot create threads. The native syscall trace
in r33 identifies this sequence:

1. Sandboxed child sets `RLIMIT_DATA` to 4 GiB successfully.
2. Its next 8 MiB thread-stack `mmap` fails with `ENOMEM`.
3. FEX's own subsequent allocations fail as well.

The parent sandbox zygote already reports roughly 8–9 GiB of virtual data
mappings while using only about 62 MiB of resident memory. This is an FEX
resource-accounting/Chromium compatibility boundary, not evidence of exhausted
physical RAM or a new GPU fault. FEX currently passes prlimit64 through to
the host kernel. No limit was bypassed and no sandbox-disable option added.
The strace run perturbs process timing and is diagnostic, not a performance
or client-success result. Full process snapshots and trace remain under
workspace `build/steam-device-lost/`; compact evidence is retained here.

The harness's observation PASS only means survival. `review.json` records
that r31/r32/r33 are **not Steam login passes**. No software renderer was
selected. No additional hardware capability is claimed from these runs.

All experiments used the 60-second hardware watchdog, six PMP channels
sampled every 0.5 seconds, 95 C cutoff, and bounded process cleanup. Peaks
for r31/r32/r33 were 65.5/65.75/65.5 C. After cleanup only the normal greeter
compositor remained; display manager active, panel failed=0, CPU maxima
4104000/2892000 kHz. The separate intermittent DCP D114 idle fault remains
unfixed. User desktop login and real games remain unqualified.

The compressed command logs preserve the original PTY bytes, including CRLF.
Tool copies are evidence snapshots. Maintain reusable versions in workspace
`tools/`, whose README describes installation and guarded use. The read-only
guest snapshot and bounded native syscall tracer join an already guarded VM;
they must not create an unattended second VM.
