#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Supervise the Asahi desktop; renew only after real GPU and compositor checks."""
import argparse, json, os, signal, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, '/root/g16-submit-test')
from experiment_watchdog import ExperimentWatchdog, KEEPALIVE
from experiment_thermals import ExperimentThermals
p=argparse.ArgumentParser()
p.add_argument('--seconds',type=int,default=1800)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
assert 1 <= a.seconds <= 3600
a.output.mkdir(parents=True,exist_ok=False)
w=ExperimentWatchdog()
r={'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'watchdog':w.arm(),'seconds':a.seconds}
(a.output/'start.json').write_text(json.dumps(r,indent=2)+'\n'); os.sync()
proc=None
stopping=False
def stop():
    # The seat/session children deliberately own process groups. Stop their
    # actual GPU clients too, so thermal trips don't leave rendering running.
    for name in ('supertux2','Hyprland'):
        subprocess.run(['pkill','-TERM','-u','1000','-x',name],timeout=3)
    if proc is not None and proc.poll() is None:
        os.killpg(proc.pid,signal.SIGTERM)
os.environ['M4_TEST_CPU_MAX_KHZ']='1860000'
t=ExperimentThermals(a.output,on_trip=stop)
try: t.start()
except BaseException:
    w.disarm()
    raise
r['passed']=False
try:
    env={**os.environ,'M4_DESKTOP_EVIDENCE':str(a.output), 'M4_HYPR_INTERACTIVE_SECONDS':str(a.seconds)}
    with (a.output/'command.log').open('w') as log, (a.output/'health.jsonl').open('w') as health:
        proc=subprocess.Popen(['python3','/root/asahi-desktop/desktop-launch.py'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        source=(a.output/'command.log').open()
        last=-1
        pending=''
        deadline=time.monotonic()+a.seconds+45
        while proc.poll() is None:
            t.check()
            if time.monotonic()>deadline:
                raise RuntimeError('desktop exceeded bounded duration')
            pending+=source.read()
            while '\n' in pending:
                line,pending=pending.split('\n',1)
                if not line.startswith('{'): continue
                try: event=json.loads(line)
                except json.JSONDecodeError: continue
                if event.get('event')=='ready':
                    print(line,flush=True)
                if event.get('event')!='gpu-progress': continue
                c=event['check']; kms=c.get('native_kms_status',{})
                assert c.get('passed') is True and c.get('vulkan') is True
                assert c.get('values_verified')==32 and c.get('device_id')==0x8132
                assert c.get('compositor_responsive') is True
                assert c.get('sequence')==last+1 and 0 <= time.monotonic()-c['checked_at'] < 3
                assert kms.get('failed')==0 and kms.get('imported')==1 and kms.get('completed',0)>0
                t.check()  # A stale/stopped thermal monitor vetoes renewal.
                health.write(line+'\n'); health.flush(); os.fsync(health.fileno())
                w.integer(KEEPALIVE)
                last=c['sequence']
                (a.output/'live-thermal.json').write_text(json.dumps(t.summary(),indent=2)+'\n')
                print(json.dumps({'event':'verified-progress','sequence':last}),flush=True)
            time.sleep(0.2)
        source.close()
        log.flush(); os.fsync(log.fileno())
        assert proc.returncode==0, proc.returncode
        t.check()
        r.update(passed=True,health_checks=last+1,returncode=proc.returncode)
except BaseException as error:
    r['error']=repr(error)
    stop()
    raise
finally:
    r['thermal']=t.summary()
    (a.output/'result.json').write_text(json.dumps(r,indent=2)+'\n')
    subprocess.run(['dmesg','--color=never'],stdout=(a.output/'dmesg.log').open('w'),timeout=3)
    t.close(); os.sync()
    if not r['passed']:
        # Failed health must not disarm recovery. Allow an ordinary reboot
        # first; the still-armed watchdog covers a stuck shutdown.
        subprocess.run(['systemctl','reboot'],timeout=5)
w.disarm()
print(json.dumps(r),flush=True)
