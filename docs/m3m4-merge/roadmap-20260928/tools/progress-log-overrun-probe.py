#!/usr/bin/env python3
"""M4 guarded, bounded render validation across printk overrun and watchdog renewal.

The 65-second run exceeds a watchdog period. Rendering must keep advancing the
host-only progress counter; a deliberately slow kmsg reader must observe EPIPE.
No firmware registers, fault injection, reset or watchdog feeding in this probe.
"""
import argparse,ctypes,errno,hashlib,json,os,subprocess,time
from pathlib import Path
from completion_progress import CompletionProgress
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path)
a=p.parse_args();out=a.output
state=json.loads((out/'start.json').read_text())
assert state['watchdog']['armed'] and state['watchdog']['timeout_seconds']==60
assert state['progress_source']=='debugfs-host-verified-v1'
progress=CompletionProgress(out/'probe-progress.jsonl',Path('/sys/kernel/debug/asahi-m4/progress'))
base=Path('/root/roadmap-progress-oracle')
env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','MESA_','AGX_','HK_','ASAHI_','LD_','M4_ORACLE_'))}
settings=dict(VK_DRIVER_FILES='/opt/asahi-m3m4-cts-20260927/asahi.json',
    VK_ICD_FILENAMES='/opt/asahi-m3m4-cts-20260927/asahi.json',
    LD_LIBRARY_PATH='/opt/asahi-m3m4-cts-20260927/lib',
    ASAHI_M3_EXPERIMENTAL='1',ASAHI_M4_EXPERIMENTAL='1',ASAHI_MESA_DEBUG='nocompress',
    HK_DEBUG_LOG='0',AGX_MESA_DEBUG='',VK_LAYER_PATH='/var/empty',VK_IMPLICIT_LAYER_PATH='/var/empty')
env.update(settings);(out/'oracle-environment.json').write_text(json.dumps(settings,indent=2)+'\n')
files=['vk-buffer-oracle','probe.spv','input.bin','reference.bin']
(out/'oracle-input-sha256.json').write_text(json.dumps({f:hashlib.sha256((base/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n')
slow=os.open('/dev/kmsg',os.O_RDONLY|os.O_NONBLOCK);os.lseek(slow,0,os.SEEK_END)
ring=ctypes.CDLL(None).klogctl(10,None,0);assert 0<ring<=131072,ring
policy=Path('/proc/sys/kernel/printk_devkmsg');old=policy.read_text()
start=time.monotonic();iterations=0
try:
    policy.write_text('on\n')
    with open('/dev/kmsg','wb',buffering=0) as log:
        for i in range(4096):log.write((f'<6>asahi observer overrun validation {i} '+'.'*224+'\n').encode())
    policy.write_text(old)
    overrun=False
    try:os.read(slow,65536)
    except OSError as e:
        assert e.errno==errno.EPIPE,e
        overrun=True
    assert overrun,'Synthetic burst did not overrun slow reader'
    with (out/'oracle.log').open('w') as log:
        while time.monotonic()-start<65:
            assert iterations<1000
            subprocess.run([str(base/'vk-buffer-oracle'),str(base/'probe.spv'),str(base/'input.bin'),str(base/'reference.bin'),'1',str(out/'actual.bin')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
            progress.poll(time.monotonic());iterations+=1;time.sleep(.2)
    count,_=progress.poll(time.monotonic());assert count>=iterations>0
    report=dict(ring_bytes=ring,slow_reader_overrun=overrun,verified_completions=count,
                oracle_passes=iterations,elapsed_seconds=time.monotonic()-start,printk_policy_restored=policy.read_text()==old)
    (out/'overrun-proof.json').write_text(json.dumps(report,indent=2)+'\n')
    print('HOST_PROGRESS_OVERRUN_PASS '+json.dumps(report),flush=True)
finally:
    policy.write_text(old);os.close(slow);progress.close()
