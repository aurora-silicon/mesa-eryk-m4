#!/usr/bin/env python3
"""Bounded Steam startup evidence. Invoke host mode only through gpu-experiment.py.

Copies the running VM's argv, closes Steam (refuses if a game is running), and
starts the same command with temporary FEX perf naming. No installed wrapper is
changed. The normal Steam session remains open after profiling. Guest perf uses
software samples; CPU times, faults and waits are also retained independently.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time


def processes():
    rows = []
    for p in Path('/proc').glob('[0-9]*'):
        try:
            raw = (p / 'stat').read_text()
            stat = raw[raw.rfind(')') + 2:].split()
            rows.append(dict(pid=int(p.name), comm=raw[raw.find('(')+1:raw.rfind(')')],
                state=stat[0], ppid=int(stat[1]), utime=int(stat[11]), stime=int(stat[12]),
                minflt=int(stat[7]), majflt=int(stat[9]), rss=int(stat[21]),
                wchan=(p/'wchan').read_text().strip(),
                argv=(p/'cmdline').read_bytes().decode(errors='replace').strip('\0').split('\0')))
        except (OSError, ValueError, IndexError):
            continue
    return rows


def guest(output, command, seconds):
    output.mkdir(exist_ok=True)
    env = dict(os.environ, FEX_GLOBALJITNAMING='1', FEX_LIBRARYJITNAMING='1')
    # Attach before dbus-run-session forks. Attaching after Popen races its
    # first fork and can silently produce a profile of an idle Python parent.
    child = subprocess.Popen(['/usr/bin/python3', '-c',
        'import os,signal,sys; os.kill(os.getpid(),signal.SIGSTOP); os.execvpe(sys.argv[1],sys.argv[1:],os.environ)',
        *command], env=env)
    os.waitpid(child.pid, os.WUNTRACED)
    perf_env = dict(os.environ, LD_LIBRARY_PATH='/opt/m4-profiling/usr/lib')
    with (output/'perf.log').open('w') as log:
        perf = subprocess.Popen(['/opt/m4-profiling/usr/bin/perf', 'record',
            '-e', 'cpu-clock:u', '-F', '99', '-g', '-p', str(child.pid),
            '-o', str(output/'perf.data')], env=perf_env, stdout=log, stderr=log)
        time.sleep(.5)
        assert perf.poll() is None, 'perf failed before workload start'
        child.send_signal(signal.SIGCONT)
        start = time.monotonic()
        try:
            with (output/'guest-processes.jsonl').open('w') as f:
                while time.monotonic() - start < seconds and child.poll() is None:
                    f.write(json.dumps(dict(elapsed=time.monotonic()-start,
                                          processes=processes()))+'\n')
                    f.flush()
                    for p in Path('/tmp').glob('perf-*.map'):
                        try:
                            shutil.copyfile(p, output/p.name)
                        except OSError:
                            pass
                    time.sleep(.5)
        finally:
            if perf.poll() is None:
                perf.send_signal(signal.SIGINT)
            perf.wait(timeout=5)
    print('GUEST_PROFILE_COMPLETE', flush=True)
    # Leave the ordinary desktop application alive, no stress workload.
    return child.wait()


def host(output, seconds, full_clocks, normal_launch):
    rows = processes()
    vms = [p for p in rows if p['argv'][0] == '/usr/bin/muvm' and '-f' in p['argv']]
    assert len(vms) == 1, 'need exactly one existing Steam VM'
    vm = vms[0]
    assert any('mesa-multilib' in arg for arg in vm['argv']), 'not the Steam VM'
    desktop = ['python3', '/root/steam-desktop-command.py']
    guest_cmd = desktop + ['/usr/bin/muvm', '--privileged', '-i', '--']
    # Only terminate Steam, and refuse if a launched game is present.
    stop = """import os, pathlib, signal
ps=[]
for p in pathlib.Path('/proc').glob('[0-9]*'):
 try: ps.append((int(p.name),(p/'comm').read_text().strip(),(p/'environ').read_bytes().split(b'\\0')))
 except OSError: pass
assert not any(c.lower().startswith(('factorio','wine','proton')) or any(x.startswith(b'SteamGameId=') and x!=b'SteamGameId=0' for x in e) for _,c,e in ps), 'game is running'
for pid,c,_ in ps:
 if c=='steam': os.kill(pid,signal.SIGTERM)
"""
    subprocess.run(guest_cmd + ['/usr/bin/python3', '-c', stop], check=True, timeout=10)
    for _ in range(30):
        if not Path(f'/proc/{vm["pid"]}').exists():
            break
        time.sleep(.2)
    else:
        os.kill(vm['pid'], signal.SIGTERM)
        time.sleep(2)
    assert not Path(f'/proc/{vm["pid"]}').exists(), 'old VM did not exit'
    clocks = {}
    for p in Path('/sys/devices/system/cpu/cpufreq').glob('policy*'):
        clocks[p.name] = (p/'scaling_max_freq').read_text().strip()
        if full_clocks:
            (p/'scaling_max_freq').write_text((p/'cpuinfo_max_freq').read_text())
    (output/'cpu-limits-before.json').write_text(json.dumps(clocks))
    shared = Path('/home/e/.cache/m4-steam') / output.name
    shared.mkdir(mode=0o700)
    os.chown(shared, 1000, 1000)
    logdir = Path('/home/e/.local/share/Steam/logs')
    logs = ['bootstrap_log.txt','console_log.txt','console-linux.txt','webhelper.txt',
            'webhelper-linux.txt','cef_log.txt','steamui_system.txt','steamui_login.txt',
            'steamui_html.txt']
    offsets = {name: (logdir/name).stat().st_size for name in logs}
    argv = vm['argv']
    sep = argv.index('--')
    guest_start = argv.index('/usr/local/libexec/m4-steam/guest-session.py') - 1
    command = argv[:sep+1] + ['/usr/bin/python3', '/home/e/steam-startup-profile.py',
        'guest', '--output', str(shared), '--seconds', str(seconds-5), '--'] + argv[guest_start:]
    if normal_launch:
        command = ['/usr/local/bin/steam']
    (output/'launch.json').write_text(json.dumps(dict(argv=command, wall=time.time()), indent=2))
    with (shared/'launch.log').open('w') as log:
        launch = subprocess.Popen(desktop+command, stdout=log, stderr=log, start_new_session=True)
    def interrupted(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        start = time.monotonic()
        with (output/'host-processes.jsonl').open('w') as f:
            while time.monotonic()-start < seconds and launch.poll() is None:
                f.write(json.dumps(dict(elapsed=time.monotonic()-start,
                                      processes=processes()))+'\n')
                f.flush()
                time.sleep(.5)
        for name, offset in offsets.items():
            with (logdir/name).open('rb') as f:
                f.seek(offset)
                (output/name).write_bytes(f.read())
        shutil.copytree(shared, output/'guest', dirs_exist_ok=True)
        assert launch.poll() is None, 'Steam launcher exited'
    except BaseException:
        # The successful run deliberately leaves the ordinary application open.
        # A guard stop must also stop its detached VM/launcher, however.
        for proc in processes():
            if (proc['argv'][0] == '/usr/bin/muvm' and '-f' in proc['argv'] and
                    any('mesa-multilib' in arg for arg in proc['argv'])):
                try:
                    os.kill(proc['pid'], signal.SIGTERM)
                except ProcessLookupError:
                    pass
        if launch.poll() is None:
            os.killpg(launch.pid, signal.SIGTERM)
        raise
    print('STARTUP_PROFILE_COMPLETE', flush=True)
    return 0


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['host','guest'])
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--seconds', type=int, default=65)
    p.add_argument('--full-clocks', action='store_true')
    p.add_argument('--normal-launch', action='store_true',
                   help='restore the installed launcher without FEX perf naming')
    # Separate the guest command before parsing the profiler options.
    import sys
    args = sys.argv[1:]
    command = []
    if '--' in args:
        i = args.index('--')
        command, args = args[i+1:], args[:i]
    a = p.parse_args(args)
    assert 10 <= a.seconds <= 180
    raise SystemExit(guest(a.output, command, a.seconds) if a.mode == 'guest'
                     else host(a.output, a.seconds, a.full_clocks, a.normal_launch))
