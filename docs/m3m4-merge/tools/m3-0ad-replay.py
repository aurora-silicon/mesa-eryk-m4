#!/usr/bin/env python3
"""Guarded replay of a retained 0 A.D. Vulkan trace (including Zink traces)."""
import hashlib, json, os, re, signal, subprocess, sys, time
from pathlib import Path

out, trace = map(Path, sys.argv[1:3])
assert 'guard.py' in Path('/proc',str(os.getppid()),'cmdline').read_text()
assert json.loads((out/'result.json').read_text())['phase']=='thermal-policy-ready'
shell=subprocess.check_output(['pgrep','-u','eryk','-x','plasmashell'],text=True).split()[0]
env=dict(v.decode().split('=',1) for v in Path('/proc',shell,'environ').read_bytes().split(b'\0') if b'=' in v)
for k in ('NOTIFY_SOCKET','INVOCATION_ID','JOURNAL_STREAM','SYSTEMD_EXEC_PID'):
    env.pop(k,None)
env.update(HK_DEBUG_LOG='0',HK_M4_NATIVE_PREAMBLE='true',HK_M4_PREFETCH_UBOS='true',
           HK_M4_PREFETCH_VARIANT='true',HK_M4_PLAIN_SAMPLERS='true',HK_M4_IOTO_TEMPLATES='true')
software='--software' in sys.argv
if software: env['VK_DRIVER_FILES']='/usr/share/vulkan/icd.d/lvp_icd.aarch64.json'
for arg in sys.argv[3:]:
    if arg.startswith('--environment='):
        candidate = json.loads(Path(arg.split('=',1)[1]).read_text())
        assert all(isinstance(k,str) and isinstance(v,str) and
                   k.startswith(('HK_', 'AGX_', 'MESA_', 'VK_', 'ASAHI_',
                                 'LD_LIBRARY_', 'LIBGL_', 'GALLIUM_', 'GBM_', '__EGL_'))
                   for k,v in candidate.items())
        env.update(candidate)
    if arg.startswith('--env='):
        k,v=arg[6:].split('=',1)
        assert k.startswith(('HK_','AGX_','MESA_','VK_'))
        env[k]=v
if 'AGX_COMPILER_CAPTURE_DIR' in env:
    capture=Path(env['AGX_COMPILER_CAPTURE_DIR'])
    assert capture.is_relative_to(out)
    capture.mkdir(parents=True,exist_ok=False)
cmd=['/opt/gfxreconstruct/bin/gfxrecon-replay','--wsi','wayland','--swapchain','offscreen',
     '--memory-translation','rebind','--omit-pipeline-cache-data',
     '--screenshots','3','--screenshot-dir',str(out),str(trace)]
for arg in sys.argv[3:]:
    if arg.startswith('--loop='):
        count=int(arg.split('=')[1]);assert 1<=count<=1000
        cmd[1:1]=['--loop-frame','2','--loop-count',str(count)]
icd=Path(env['VK_DRIVER_FILES'])
library=Path(json.loads(icd.read_text())['ICD']['library_path'])
if not library.is_absolute():
    library=Path('/opt/asahi-m3-wayland/lib')/library
assert library.is_file(), library
(out/'manifest.json').write_text(json.dumps({'command':cmd,'software':software,
    'adaptations':['offscreen swapchain','memory rebind','pipeline cache omitted'],
    'environment':{k:v for k,v in env.items() if k.startswith(('HK_','AGX_','VK_','LD_','MESA_'))},
    'sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
              (trace,Path(cmd[0]),icd,library)}},indent=2))
governors={};child=None;klog=None;result={};mapped=False
try:
    for p in Path('/sys/devices/system/cpu/cpufreq').glob('policy*/scaling_governor'):
        governors[p]=p.read_text().strip();p.write_text('performance')
    with (out/'kernel-live.log').open('w') as log:
        klog=subprocess.Popen(['dmesg','--follow-new','--color=never'],stdout=log,stderr=log)
    start=time.monotonic()
    with (out/'replay.log').open('w') as log:
        child=subprocess.Popen(cmd,env=env,stdout=log,stderr=log,start_new_session=True)
        while child.poll() is None:
            if not mapped:
                try: mappings=Path('/proc',str(child.pid),'maps').read_text()
                except FileNotFoundError: mappings=''
                if any(line.endswith(' '+str(library.resolve())) for line in mappings.splitlines()):
                    (out/'replay-maps.txt').write_text(mappings)
                    mapped=True
            if time.monotonic()-start>=32:
                os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=2)
                result={'returncode':child.returncode,'timed_out':True}
                break
            time.sleep(.05 if mapped else .01)
        else: result={'returncode':child.returncode,'timed_out':False}
    result['elapsed_seconds']=time.monotonic()-start
    result['selected_library_mapped']=mapped
    result['screenshot']=(out/'screenshot_frame_3.bmp').is_file()
    (out/'replay-result.json').write_text(json.dumps(result,indent=2))
finally:
    if child and child.poll() is None:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=2)
    if klog and klog.poll() is None:klog.terminate();klog.wait(timeout=2)
    for p,value in governors.items():p.write_text(value)
text=(out/'kernel-live.log').read_text()
assert not re.search(r'BUG:|Oops:|execution failed|M3 completion events|IOCTL: submit failed|GPU fault|faults=[1-9]',text,re.I)
print(json.dumps(result),flush=True)
assert result['returncode']==0 and not result['timed_out'],result
assert result['selected_library_mapped'], 'Selected Vulkan library was not observed in the replay process'
assert result['screenshot'] and (out/'screenshot_frame_3.bmp').stat().st_size>54,result
