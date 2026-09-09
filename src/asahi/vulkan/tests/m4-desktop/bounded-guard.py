#!/usr/bin/env python3
import argparse, json, os, signal, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, '/root/g16-submit-test')
from experiment_watchdog import ExperimentWatchdog
from experiment_thermals import ExperimentThermals
p=argparse.ArgumentParser()
p.add_argument('--output', required=True, type=Path)
p.add_argument('command', nargs=argparse.REMAINDER)
a=p.parse_args()
command=a.command[1:] if a.command[:1]==['--'] else a.command
assert command
a.output.mkdir(parents=True, exist_ok=False)
w=ExperimentWatchdog()
r={'command':command, 'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip(), 'watchdog':w.arm()}
(a.output/'start.json').write_text(json.dumps(r,indent=2)+'\n')
os.sync()
initial_taint=int(Path("/proc/sys/kernel/tainted").read_text())
initial_dmesg=subprocess.check_output(["dmesg","--color=never"],text=True)
proc=None
def stop():
    if proc is not None and proc.poll() is None:
        os.killpg(proc.pid, signal.SIGTERM)
os.environ['M4_TEST_CPU_MAX_KHZ']='1860000'
t=ExperimentThermals(a.output,on_trip=stop)
try:t.start()
except BaseException:
    w.disarm()
    raise
with (a.output/'command.log').open('w') as log:
    os.fsync(log.fileno())
    os.sync()
    proc=subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    deadline=time.monotonic()+40
    streamed=0
    def relay():
        global streamed
        with (a.output/'command.log').open('rb') as source:
            source.seek(streamed)
            chunk=source.read()
        if chunk:
            streamed += len(chunk)
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
    while proc.poll() is None:
        relay()
        t.check()
        if time.monotonic()>deadline:
            stop()
            raise RuntimeError('bounded command exceeded 40 seconds; watchdog remains armed')
        time.sleep(0.2)
    relay()
    log.flush();os.fsync(log.fileno())
t.check()
r.update(returncode=proc.returncode, thermal=t.summary())
(a.output/'result.json').write_text(json.dumps(r,indent=2)+'\n')
subprocess.run(['dmesg','--color=never'],stdout=(a.output/'dmesg.log').open('w'),check=True,timeout=3)
os.sync()
final_taint=int(Path('/proc/sys/kernel/tainted').read_text())
final_dmesg=(a.output/'dmesg.log').read_text()
new_dmesg=final_dmesg[len(initial_dmesg):] if final_dmesg.startswith(initial_dmesg) else final_dmesg
# The ring can wrap during verbose GPU bring-up. Retain the launcher's
# continuous stream as well, so an earlier fault cannot vanish at teardown.
live=a.output/'kernel-live.log'
if live.exists():
    new_dmesg += live.read_text()
faults=['Internal error:', 'BUG:', 'Oops:', 'Kernel panic', 'soft lockup', 'hard LOCKUP', 'ASC failed to stop', 'GPU retirement unproven', 'firmware crashed']
if (final_taint & ~initial_taint & ((1 << 7) | (1 << 9) | (1 << 14))) or any(f in new_dmesg for f in faults):
    print('Kernel fault detected; requesting reboot with watchdog still armed',flush=True)
    subprocess.run(['systemctl','reboot'],check=True,timeout=5)
    raise RuntimeError('kernel fault during guarded command; watchdog remains armed')
t.close()
w.disarm()
print(json.dumps(r,indent=2),flush=True)
sys.exit(proc.returncode)
