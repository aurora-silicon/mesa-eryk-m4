#!/usr/bin/env python3
import argparse, json, os, re, signal, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, '/root/g16-submit-test')
from experiment_watchdog import ExperimentWatchdog, KEEPALIVE
from experiment_thermals import ExperimentThermals
p=argparse.ArgumentParser()
p.add_argument('--output', required=True, type=Path)
p.add_argument('--seconds', type=int, default=40)
p.add_argument('command', nargs=argparse.REMAINDER)
a=p.parse_args()
assert 1 <= a.seconds <= 1800
command=a.command[1:] if a.command[:1]==['--'] else a.command
assert command
a.output.mkdir(parents=True, exist_ok=False)
w=ExperimentWatchdog()
r={'command':command, 'seconds':a.seconds, 'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip(), 'watchdog':w.arm()}
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
kernel_proc=None
kernel_read=None
kernel_write=None
kernel_pending=''
last_feed=time.monotonic()
last_job=-1
if a.seconds>40:
    kernel_write=(a.output/'kernel-live.log').open('w')
    kernel_proc=subprocess.Popen(['dmesg','--follow-new','--color=never'],
                                 stdout=kernel_write,stderr=subprocess.STDOUT)
    kernel_read=(a.output/'kernel-live.log').open()
with (a.output/'command.log').open('w') as log:
    os.fsync(log.fileno())
    os.sync()
    proc=subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    deadline=time.monotonic()+a.seconds
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
        if kernel_read is not None:
            if kernel_proc.poll() is not None:
                stop()
                raise RuntimeError('kernel progress monitor stopped; watchdog remains armed')
            kernel_pending += kernel_read.read()
            lines=kernel_pending.split('\n')
            kernel_pending=lines.pop()
            for line in lines:
                if any(f in line for f in ('GPU retirement unproven','firmware crashed','Internal error:','BUG:')):
                    stop()
                match=re.search(r'^\[\s*([0-9.]+)\].*userspace GPU job (\d+) retired',line)
                now=time.monotonic()
                if match and int(match[2])>last_job and 0<=now-float(match[1])<3:
                    last_job=int(match[2])
                    if now-last_feed>=5:
                        t.check()
                        kernel_write.flush(); os.fsync(kernel_write.fileno())
                        w.integer(KEEPALIVE)
                        last_feed=now
                        print(json.dumps({'event':'gpu-retirement-renewal','job':last_job}),flush=True)
        if time.monotonic()>deadline:
            stop()
            raise RuntimeError('bounded command exceeded deadline; watchdog remains armed')
        time.sleep(0.2)
    relay()
    log.flush();os.fsync(log.fileno())
if kernel_proc is not None:
    kernel_proc.terminate(); kernel_proc.wait(timeout=3)
    kernel_read.close()
    kernel_write.flush(); os.fsync(kernel_write.fileno()); kernel_write.close()
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
