#!/usr/bin/env python3
"""Bounded J514S experiments: 60s watchdog, M3 PMP samples, CPU thermal policy."""
import argparse,json,mmap,os,signal,struct,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
from experiment_watchdog import ExperimentWatchdog, KEEPALIVE
from draw_progress import validate_progress
from native_progress import validate_native_progress
from completion_progress import CompletionProgress as RetirementProgress
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--seconds',type=int,default=30,help='Bound for ordinary commands (1..45 seconds, no watchdog renewal)');p.add_argument('--repeat-draws',type=int,default=0);p.add_argument('--native-draws',type=int,default=0);p.add_argument('--keep-cpu-clocks',action='store_true',help='Preserve existing CPU frequency caps; thermal policy and monitoring remain mandatory');p.add_argument('output',type=Path);p.add_argument('command',nargs=argparse.REMAINDER);p.add_argument('--gpu-progress-seconds',type=int,default=0,help='Bounded replay with kernel retirement and thermal-gated renewal (46..180 seconds)');a=p.parse_args()
assert 1<=a.seconds<=45
assert not (a.repeat_draws and a.native_draws)
assert a.gpu_progress_seconds==0 or 46<=a.gpu_progress_seconds<=180
assert not (a.gpu_progress_seconds and (a.repeat_draws or a.native_draws))
assert 0<=a.native_draws<=1000
progress_validator=validate_native_progress if a.native_draws else validate_progress
a.repeat_draws=a.native_draws or a.repeat_draws
assert a.repeat_draws==0 or 1<=a.repeat_draws<=1000
base=Path(__file__).resolve().parent.parent
if a.command[:1]==['--']:a.command=a.command[1:]
a.output.mkdir(mode=0o700,parents=True,exist_ok=False)
r={'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'scope':'bounded M3 experiment','command':a.command,'phase':'prepared','keep_cpu_clocks':a.keep_cpu_clocks}
limit=a.gpu_progress_seconds or (120 if a.repeat_draws else a.seconds)
r['progress_gate']={'requested_draws':a.repeat_draws,'total_limit_seconds':limit,'stall_limit_seconds':5,'renew_interval_seconds':10,'kernel_retirement':bool(a.gpu_progress_seconds),'retirement_source':'debugfs-host-verified-v1' if a.gpu_progress_seconds else None,'startup_seconds':45 if a.gpu_progress_seconds else 20}
import hashlib
r['tool_sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [*Path(__file__).resolve().parent.glob('*.py'),*Path(__file__).resolve().parent.glob('*-incomplete.bin'),*base.glob('m3_*.ko'),*base.glob('asahi.ko')]}
def save():
 (a.output/'result.json').write_text(json.dumps(r,indent=2)+'\n');os.sync();print(json.dumps(r),flush=True)
save();w=ExperimentWatchdog();r['watchdog']=w.arm();r['phase']='armed';save()
child=None;fd=None;mem=None;caps=[];retirements=None
native_progress_path=Path('/sys/kernel/debug/asahi-m3/progress')
native_present_at_start=native_progress_path.exists()
started=time.monotonic()
try:
 for name in ('m3_pmp_overlay','m3_pmp_thermal','m3_gpu_bridge'):
  if not Path('/sys/module',name).exists():
   if name=='m3_pmp_thermal':time.sleep(1)
   subprocess.run(['insmod',str(base/(name+'.ko'))],check=True,timeout=8)
 zones=[p for p in Path('/sys/class/thermal').glob('thermal_zone*') if (p/'type').read_text().strip()=='m3-pmp-die']
 assert len(zones)==1 and (zones[0]/'mode').read_text().strip()=='enabled'
 fd=os.open('/dev/mem',os.O_RDONLY|os.O_SYNC);mem=mmap.mmap(fd,0xc0000,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ,offset=0x350500000)
 records=[('ta000m',0x69190),('te000m',0x69234),('te001m',0x692d8),('tp000m',0x6937c),('tp001m',0x69420),('tp002m',0x694c4)]
 def sample():
  assert (zones[0]/'mode').read_text().strip()=='enabled'
  temps={}
  for name,off in records:
   # Copy an aligned span from Device memory before parsing in normal RAM.
   first=off&~15;end=(off+0xa4+15)&~15
   for attempt in range(20):
    raw=mem[first:end];again=mem[first:end]
    if raw==again:break
   else:raise RuntimeError(f'unstable PMP record: {name}')
   data=raw[off-first:off-first+0xa4]
   assert data[0x94:0x9b]==name.encode()+b'\0'
   valid=struct.unpack_from('<I',data,0x48)[0]
   if name=='ta000m' and valid!=1:continue  # Same optional channel as the kernel.
   assert valid==1,(name,valid)
   q,f=struct.unpack_from('<if',data,0x3c)
   assert -2560<=q<=9600 and abs(q/64-f)<.1,(name,q,f)
   temps[name]=q*1000/64
  record={'time':time.time(),'monotonic':time.monotonic(),'temperatures_mc':temps,'zone_mc':int((zones[0]/'temp').read_text())}
  with (a.output/'temperatures.jsonl').open('a') as log:log.write(json.dumps(record)+'\n');log.flush()
  assert max(temps.values())<120000 and record['zone_mc']<120000,record
  return temps
 assert (zones[0]/'trip_point_2_temp').read_text().strip()=='120000', 'Kernel and monitor thermal limits must match'
 r['thermal_policy']={'startup_max_mc':95000,'stop_mc':120000,'sample_interval_seconds':.5,'kernel_regulation_target_mc':110000}
 r['initial_temperatures']=sample()
 assert max(r['initial_temperatures'].values())<95000 and int((zones[0]/'temp').read_text())<95000, '95 C startup gate'
 for pol,limit in [('policy0',1884000),('policy1',1476000)]:
  if a.keep_cpu_clocks:continue
  path=Path('/sys/devices/system/cpu/cpufreq',pol,'scaling_max_freq');old=int(path.read_text());caps.append((path,old));path.write_text(str(min(old,limit)))
 r['phase']='thermal-policy-ready';save()
 retirements=RetirementProgress(a.output/'kernel-retirements.log') if a.gpu_progress_seconds else None
 started=time.monotonic()
 renewed_count=0
 verified=0;last_progress=started;last_renew=started
 with (a.output/'workload.log').open('w') as log:
  if a.command:child=subprocess.Popen(a.command,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  while True:
   sample()
   now=time.monotonic()
   if retirements:
    count,progress_time=retirements.poll(now)
    if progress_time is not None:last_progress=progress_time
    verified=count
    # Pipeline compilation may follow an initial GPU upload. Preserve the
    # startup allowance even if the upload has already retired; no renewal
    # occurs during compilation without fresh GPU evidence.
    if now-started>45 and now-last_progress>5:raise TimeoutError('verified GPU retirement stopped')
    if now-last_renew>=10 and verified>renewed_count:
     w.integer(KEEPALIVE);last_renew=now;renewed_count=verified
     with (a.output/'watchdog-progress.jsonl').open('a') as log:log.write(json.dumps({'monotonic':now,'verified_kernel_records':verified,'last_retirement':last_progress})+'\n');log.flush()
   if a.repeat_draws:
    count,progress_time=progress_validator(a.output/'draws.jsonl',verified,now,a.repeat_draws)
    if progress_time is not None:last_progress=progress_time
    advanced=count>verified;verified=count
    if now-last_progress>(5 if verified else 20):raise TimeoutError('verified draw progress stopped')
    if now-last_renew>=10 and advanced:
     # The immediately preceding sample checked all sensors and enabled CPU
     # thermal policy. Every new record independently passes the M3 gates.
     w.integer(KEEPALIVE);last_renew=now
     with (a.output/'watchdog-progress.jsonl').open('a') as log:log.write(json.dumps({'monotonic':now,'verified_draws':verified,'last_draw':last_progress})+'\n');log.flush()
   if child is not None and child.poll() is not None:break
   if child is None and time.monotonic()-started>2:break
   if time.monotonic()-started>limit:raise TimeoutError('bounded experiment limit')
   time.sleep(.5)
 if child is not None and child.returncode:raise RuntimeError(f'command exit {child.returncode}')
 # A child can return success just before the next monitor poll (for
 # example a deliberate Vulkan DEVICE_LOST probe). Never disarm on that race.
 if retirements:
  retirements.poll(time.monotonic())
 elif native_progress_path.exists():
  terminal=RetirementProgress(a.output/'terminal-progress.jsonl',native_progress_path)
  terminal.close()
 elif native_present_at_start:
  raise RuntimeError('GPU progress endpoint disappeared during experiment')
 if a.repeat_draws:
  assert verified==a.repeat_draws,(verified,a.repeat_draws)
  r['verified_draws']=verified
 if retirements:
  # Early userspace rejection may submit no GPU work; it still fits the
  # ordinary bound and is reported as a failed replay by the child.
  assert verified>0 or time.monotonic()-started<=45, 'No verified GPU execution'
  r['verified_kernel_records']=verified
 r['elapsed_seconds']=time.monotonic()-started
 r['final_temperatures']=sample()
 log=subprocess.check_output(['dmesg','--color=never'],text=True);(a.output/'dmesg.log').write_text(log)
 if child is not None:print((a.output/'workload.log').read_text(),flush=True)
 r['phase']='complete';save()
 for path,old in caps:path.write_text(str(old))
 caps=[];w.disarm()
except BaseException as exc:
 if child is not None and child.poll() is None:
  os.killpg(child.pid,signal.SIGKILL)
  try:child.wait(timeout=2)
  except subprocess.TimeoutExpired:pass
 r['phase']='failed-watchdog-retained';r['error']=repr(exc)
 # Keep the original failure even if evidence collection fails. These sealed
 # kernel snapshots require no MMIO, acknowledgements or watchdog renewal.
 try:
  from fault_snapshot import collect
  # Health latches before the kernel seals the dump. Allow a bounded grace
  # interval, without watchdog renewal, for its publication.
  deadline=time.monotonic()+1
  while True:
   snapshots=collect(a.output,int(started*1e9))
   if snapshots or time.monotonic()>=deadline:break
   time.sleep(.05)
  r['firmware_fault_snapshots']=snapshots
 except Exception as capture_error:
  r['firmware_fault_snapshot_error']=repr(capture_error)
 try:
  (a.output/'dmesg.log').write_bytes(subprocess.check_output(['dmesg','--color=never'],timeout=2))
  if (a.output/'workload.log').exists():print((a.output/'workload.log').read_text(),flush=True)
 finally:save()
 raise
finally:
 if retirements is not None:retirements.close()
 if mem is not None:mem.close()
 if fd is not None:os.close(fd)
