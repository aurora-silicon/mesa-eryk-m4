#!/usr/bin/env python3
"""Bounded native Apple OpenGL comparison for the audited Factorio trace.

GPU elapsed queries on this Apple GL driver return zero. Timing mode measures
completed-frame wall time with the explicit apitrace completion patch; it does
not report GPU-only time or game FPS. Setup, presentation and logging excluded.
"""
import argparse, hashlib, json, os, platform, re, statistics, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base',type=Path,default=Path('/private/var/tmp/factorio-comparison-20260919'))
p.add_argument('--output',required=True,type=Path)
p.add_argument('--snapshot',type=int)
p.add_argument('--predecode-range')
p.add_argument('--runs',type=int,default=3,choices=range(1,6))
a=p.parse_args();assert platform.system()=='Darwin'
a.output.mkdir(parents=True,exist_ok=False)
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
trace=a.base/'menu-portable.trace';exe=a.base/'apitrace-build/glretrace';ignore=(a.base/'menu-portable.ignore.txt').read_text().strip()
assert re.fullmatch(r'\d+(,\d+)*',ignore)
env={k:v for k,v in os.environ.items() if not k.startswith(('APITRACE_','DYLD_','VK_','MESA_'))}
env['APITRACE_FACTORIO_COPY_COMPAT']='1'
cmd=[str(exe),'--headless','--core','--ignore-calls='+ignore]
if a.snapshot is not None:cmd+=['-s',str(a.output/'frame-'),'-S',str(a.snapshot)]
else:cmd+=['-b'];env['APITRACE_FRAME_COMPLETION_TIMING']='1'
if a.predecode_range:
 assert re.fullmatch(r'\d+-\d+',a.predecode_range)
 env['APITRACE_PREDECODE_RANGE']=a.predecode_range
cmd+=[str(trace)]
manifest=dict(command=cmd,trace_sha256=sha(trace),binary_sha256=sha(exe),adaptations={k:v for k,v in env.items() if k.startswith('APITRACE_')},mode='image validation' if a.snapshot is not None else 'completed-frame CPU+GPU wall time, excludes swap',runs=[])
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for i in range(a.runs):
 start=time.monotonic()
 with (a.output/('run-%d.log'%i)).open('w') as log:r=subprocess.run(cmd,cwd=a.output,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60)
 text=(a.output/('run-%d.log'%i)).read_text();r.check_returncode()
 assert not re.search(r'GL_INVALID|error:|compile failed|link failed',text,re.I)
 samples=[dict(call=int(c),ms=int(ns)/1e6) for c,ns in re.findall(r'COMPLETED_FRAME call=(\d+) ns=(\d+)',text)]
 if a.snapshot is None:
  assert [s['call'] for s in samples]==[719447,720631,721815,722999,724183]
  assert all(0<s['ms']<1000 for s in samples)
 row=dict(run=i,process_seconds=time.monotonic()-start,completed_frames=samples)
 manifest['runs'].append(row);(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(row),flush=True)
