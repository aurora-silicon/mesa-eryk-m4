#!/usr/bin/env python3
"""Observe an untouched Factorio menu inside a bounded host GPU experiment.

Use as the guest launcher, replacing guest-driver-observe.py. Set
FACTORIO_DRIVER_PROFILE=1 for an eight-second perf sample after initialization.
No screenshots or input are generated. The host experiment owns the VM lifetime.
"""
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import time

out = Path('/home/e/.local/share/factorio-driver/profile')
out.mkdir(exist_ok=True)
state = Path('/home/e/.local/share/factorio-standalone/demo-2.0.77')
(out/'environment.json').write_text(json.dumps({k:v for k,v in os.environ.items()
    if k.startswith(('VK_', 'HK_', 'MESA_', 'GALLIUM_', 'FEX_', 'AGX_', 'FACTORIO_', 'LIBGL_', '__GLX_', '__EGL_')) or k == 'LD_LIBRARY_PATH'}, indent=2))
wall = time.time()
perf = None
with (out/'stderr.log').open('w') as log, (out/'cpu-samples.jsonl').open('w') as samples, (out/'perf.log').open('w') as perf_log:
    trace = []
    if os.environ.get('FACTORIO_LOADER_TRACE') == '1':
        trace = ['/usr/bin/strace', '-f', '-qq', '-e', 'trace=openat,openat2,readlink,statx',
                 '-o', str(out/'loader.strace')]
        paths = ['/usr/share/fex-emu/ThunksDB.json',
                 '/run/fex-emu/rootfs/usr/lib/ld-linux-x86-64.so.2',
                 '/home/e/.local/share/factorio-driver/native-thunk-stage/libvulkan_asahi.so']
        (out/'loader-paths.json').write_text(json.dumps({p:Path(p).exists() for p in paths}))
    if os.environ.get('FACTORIO_NATIVE_GDB') == '1':
        commands = out/'crash.gdb'
        commands.write_text("""set pagination off
set debuginfod enabled off
handle SIGILL nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGBUS nostop noprint pass
catch signal SIGSEGV
commands
silent
if $_siginfo._sifields._sigfault.si_addr == (void*)($x19+4096) || $_siginfo._sifields._sigfault.si_addr == (void*)(($x28 & ~4095)+4096)
continue
end
bt
info registers
p $_siginfo
set $state = $x19+0x180
if $x28 > 0xffff00000000
set $state = $x28
end
x/24gx $state
x/20gx *(unsigned long*)($state+64)
info proc mappings
quit
end
run
""")
        trace = ['/usr/bin/gdb', '-q', '-batch', '-x', str(commands), '--args']
    p = subprocess.Popen(trace + [os.environ.get('FACTORIO_FEX_INTERPRETER', '/usr/bin/FEXInterpreter'), '/home/e/Games/factorio-demo-2.0.77/bin/x64/factorio',
                          '--config', str(state/'config.ini')], stdout=log, stderr=log)
    started = time.monotonic()
    done = set()
    perf_saved = False
    def stop(signum, frame):
        p.terminate()
    signal.signal(signal.SIGTERM, stop)
    try:
        while p.poll() is None:
            elapsed = time.monotonic() - started
            threads = []
            for task in Path(f'/proc/{p.pid}/task').glob('*'):
                try:
                    raw = (task/'stat').read_text()
                    end = raw.rindex(')')
                    fields = raw[end+2:].split()
                    threads.append(dict(tid=int(task.name), comm=raw[raw.index('(')+1:end],
                        state=fields[0], utime=int(fields[11]), stime=int(fields[12]),
                        starttime=int(fields[19]), wchan=(task/'wchan').read_text()))
                except (OSError, ValueError):
                    pass
            samples.write(json.dumps(dict(elapsed=elapsed, hz=os.sysconf('SC_CLK_TCK'), threads=threads))+'\n')
            samples.flush()
            for sec in (5, 15, 25, 35):
                if elapsed >= sec and sec not in done:
                    done.add(sec)
                    try:
                        (out/f'maps-{sec}-{p.pid}.txt').write_text(Path(f'/proc/{p.pid}/maps').read_text())
                    except OSError:
                        pass
            game_log = state/'factorio-current.log'
            if os.environ.get('FACTORIO_DRIVER_PROFILE') == '1' and perf is None and elapsed >= float(os.environ.get('FACTORIO_PROFILE_DELAY', '0')) and game_log.exists() and game_log.stat().st_mtime >= wall and (os.environ.get('FACTORIO_PROFILE_STARTUP') == '1' or 'Factorio initialised' in game_log.read_text(errors='replace')):
                env = dict(os.environ, LD_LIBRARY_PATH='/opt/m4-profiling/usr/lib', DEBUGINFOD_URLS='')
                perf = subprocess.Popen(['/opt/m4-profiling/usr/bin/perf', 'record', '-p', str(p.pid),
                    '-e', 'cpu-clock', '-F', '99', '-g', '-o', str(out/'menu.perf.data'), '--',
                    '/usr/bin/sleep', '8'], env=env, stdout=perf_log, stderr=perf_log)
            if perf is not None and perf.poll() is not None and not perf_saved:
                jit_map = Path(f'/tmp/perf-{p.pid}.map')
                if jit_map.exists():
                    shutil.copyfile(jit_map, out/jit_map.name)
                env = dict(os.environ, LD_LIBRARY_PATH='/opt/m4-profiling/usr/lib', DEBUGINFOD_URLS='')
                with (out/'menu.perf.script').open('w') as decoded:
                    subprocess.run(['/opt/m4-profiling/usr/bin/perf', 'script', '-f',
                        '-i', str(out/'menu.perf.data'), '-F', 'comm,tid,time,ip,sym,dso'],
                        env=env, stdout=decoded, stderr=perf_log, timeout=5)
                perf_saved = True
            time.sleep(.5)
    finally:
        if perf is not None and perf.poll() is None:
            perf.send_signal(signal.SIGINT)
            try:
                perf.wait(timeout=3)
            except subprocess.TimeoutExpired:
                perf.kill()
    raise SystemExit(p.returncode)
