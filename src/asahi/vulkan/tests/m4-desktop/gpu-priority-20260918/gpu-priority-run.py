#!/usr/bin/env python3
"""Run bounded competing Vulkan clients inside gpu-experiment.py's guard."""
import argparse,json,os,pathlib,subprocess,time
p=argparse.ArgumentParser();p.add_argument('output',type=pathlib.Path);p.add_argument('--foreground',type=int,default=1);p.add_argument('--background',type=int,default=0);p.add_argument('--iterations',type=int,default=50000);p.add_argument('--batch',type=int,default=32);p.add_argument('--rounds',type=int,default=4);a=p.parse_args();a.output.mkdir(exist_ok=True,parents=True)
env=os.environ.copy();env.update(VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json',ASAHI_M4_EXPERIMENTAL='1',ASAHI_MESA_DEBUG='nocompress')
children=[];logs=[]
def start(name,priority,iterations,batch,rounds):
    log=open(a.output/(name+'.log'),'w');logs.append(log)
    ready=a.output/(name+'.ready');go=a.output/(name+'.go');out=a.output/(name+'.jsonl')
    child=subprocess.Popen(['/root/vk-priority-probe','/root/load.spv',str(priority),str(iterations),str(batch),str(rounds),str(ready),str(go),str(out)],env=env,stdout=log,stderr=subprocess.STDOUT)
    children.append(child);return child,ready,go,out
try:
    heavy=start('heavy',a.background,a.iterations,a.batch,a.rounds)
    foreground=start('foreground',a.foreground,64,1,40)
    deadline=time.monotonic()+25
    while not all(x[1].exists() for x in (heavy,foreground)):
        if any(c.poll() is not None for c in children) or time.monotonic()>deadline:raise RuntimeError('probe startup failed')
        time.sleep(.01)
    heavy[2].touch();time.sleep(.015);foreground[2].touch()
    for c in children:c.wait(timeout=max(1,deadline-time.monotonic()))
    if any(c.returncode for c in children):raise RuntimeError('probe failed')
    summary={}
    for name,entry in [('heavy',heavy),('foreground',foreground)]:
        rows=[json.loads(l) for l in entry[3].read_text().splitlines()];v=sorted(r['latency_ms'] for r in rows)
        summary[name]=dict(samples=len(v),min_ms=v[0],median_ms=v[len(v)//2],p95_ms=v[min(len(v)-1,int(len(v)*.95))],max_ms=v[-1],first_start_ns=rows[0]['start_ns'],last_end_ns=rows[-1]['end_ns'])
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2),flush=True);print('PASS: competing priority clients and all readbacks',flush=True)
finally:
    for c in children:
        if c.poll() is None:c.kill()
    for c in children:c.wait()
    for f in logs:f.close()
