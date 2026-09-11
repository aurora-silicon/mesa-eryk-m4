#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Supervise the Asahi desktop; renew only after real GPU and compositor checks."""
import argparse, json, os, re, shutil, signal, subprocess, sys, time
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
    # 0 A.D. renames its main thread to "main". Match its executable argv,
    # restricted to the desktop UID, instead of relying on the comm name.
    subprocess.run(['pkill','-TERM','-u','1000','-f',
                    '^/opt/m4-0ad/usr/games/pyrogenesis( |$)'],timeout=3)
    for name in ('supertux2','supertuxkart','sauer_client','quickshell','Hyprland'):
        subprocess.run(['pkill','-TERM','-u','1000','-x',name],timeout=3)
    if proc is not None and proc.poll() is None:
        os.killpg(proc.pid,signal.SIGTERM)
# The normal-clock comparison still uses the same thermal guard. This ceiling
# only lowers existing cpufreq limits; a previous lower cap survives until reboot.
os.environ.setdefault('M4_TEST_CPU_MAX_KHZ', '1860000')
t=ExperimentThermals(a.output,on_trip=stop)
try: t.start()
except BaseException:
    w.disarm()
    raise
r['passed']=False
shell_started=time.time()
try:
    env={**os.environ,'M4_DESKTOP_EVIDENCE':str(a.output), 'M4_HYPR_INTERACTIVE_SECONDS':str(a.seconds)}
    with (a.output/'command.log').open('w') as log, (a.output/'health.jsonl').open('w') as health:
        proc=subprocess.Popen(['python3','/root/asahi-desktop/desktop-launch.py'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        source=(a.output/'command.log').open()
        last=-1
        pending=''
        began=time.monotonic()
        deadline=began+a.seconds+180
        kernel_source=None
        kernel_pending=''
        last_kernel_feed=0
        last_kernel_report=0
        last_kernel_job=-1
        while proc.poll() is None:
            t.check()
            if time.monotonic()>deadline:
                raise RuntimeError('desktop exceeded bounded duration')
            kernel_path=a.output/'kernel-live.log'
            if kernel_source is None and kernel_path.exists():
                kernel_source=kernel_path.open()
            if kernel_source is not None:
                kernel_pending+=kernel_source.read()
                kernel_lines=kernel_pending.split('\n')
                kernel_pending=kernel_lines.pop()
                for kline in kernel_lines:
                    if any(x in kline for x in ('GPU retirement unproven','firmware crashed','Internal error:','BUG:')):
                        raise RuntimeError('kernel GPU failure: '+kline)
                    # Asset upload may keep a freshly submitted probe behind
                    # thousands of valid render jobs. During this bounded run,
                    # independently verified firmware job retirements count as
                    # forward progress throughout the bounded experiment. Never feed from an old buffered line.
                    match=re.search(r'^\[\s*([0-9.]+)\].*userspace GPU job (\d+) retired',kline)
                    now=time.monotonic()
                    exact=re.search(r'\bmonotonic_ns=(\d+)',kline)
                    retired_at=int(exact[1])/1e9 if exact else (float(match[1]) if match else 0)
                    if match and now-last_kernel_report >= 5:
                        print(json.dumps({'event':'kernel-progress-age','job':int(match[2]),'age':now-retired_at,'startup_elapsed':now-began}),flush=True)
                        last_kernel_report=now
                    if match and int(match[2])>last_kernel_job and 0 <= now-retired_at < 3:
                        last_kernel_job=int(match[2])
                    else:
                        continue
                    if now-last_kernel_feed >= 5:
                        t.check()
                        health.write(json.dumps({'event':'startup-gpu-retirement','timestamp':retired_at,'job':last_kernel_job})+'\n')
                        health.flush(); os.fsync(health.fileno())
                        w.integer(KEEPALIVE)
                        last_kernel_feed=now
                        print(json.dumps({'event':'startup-gpu-retirement','job':int(match[2])}),flush=True)
            pending+=source.read()
            lines=pending.split('\n')
            pending=lines.pop()
            for line in lines:
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
    print(json.dumps({'event':'failure','error':repr(error)}),flush=True)
    game_log=a.output/('supertux.log' if os.getenv('M4_TEST_SUPERTUX')=='1' else
                       'sauerbraten.log' if os.getenv('M4_TEST_SAUERBRATEN')=='1' else
                       '0ad.log' if os.getenv('M4_TEST_0AD')=='1' else 'supertuxkart.log')
    if game_log.exists():
        with game_log.open('rb') as diagnostics:
            diagnostics.seek(max(0,game_log.stat().st_size-8192))
            print(diagnostics.read().decode(errors='replace'),flush=True)
    stop()
    # Preserve the sealed kernel snapshot before reboot destroys devcoredump.
    # Never renew the watchdog after failure; collection has its own short
    # bound and cannot prevent recovery or replace the original failure.
    try:
        with (a.output/'fault-collection.log').open('w') as diagnostics:
            subprocess.run(['python3', '/root/g16-fault-dump.py', 'collect',
                            '--guard-output', str(a.output), '--allow-missing'],
                           stdout=diagnostics, stderr=subprocess.STDOUT, timeout=5)
    except Exception as collection_error:
        r['fault_collection_error'] = repr(collection_error)
    raise
finally:
    if os.environ.get('M4_TEST_0AD') == '1':
        engine_logs=Path('/home/e/.local/state/m4-asahi-0ad/0ad/log')
        if engine_logs.exists():
            shutil.copytree(engine_logs,a.output/'0ad-engine-logs',dirs_exist_ok=True)
    with (a.output/'quickshell-journal.log').open('w') as shell_log:
        subprocess.run(['journalctl','-b','-t','omarchy-shell','--since',
                        '@'+str(int(shell_started)), '--no-pager','-o','short-monotonic'],
                       stdout=shell_log,stderr=subprocess.STDOUT,timeout=3)
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
