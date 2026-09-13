#!/usr/bin/env python3
"""Replay retained compiler bundles offline, retaining each run and a short index.

Uses compiler-replay.py for hashes, source patches and bounded diagnostics.
Original inputs are untouched. No GPU execution or SSH.
"""
import argparse, json, subprocess, sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
helper=Path(__file__).with_name('compiler-replay.py')
records=[]
for input_file in sorted(a.directory.glob('*/input.bin')):
 bundle=input_file.parent;out=a.output/bundle.name
 result=subprocess.run([sys.executable,str(helper),str(bundle),'--output',str(out)],capture_output=True,text=True,timeout=45)
 (a.output/(bundle.name+'.runner.log')).write_text(result.stdout+result.stderr)
 diagnostic=(out/'compiler.stderr').read_text(errors='replace') if (out/'compiler.stderr').exists() else result.stderr
 lines=diagnostic.splitlines()
 failures=[]
 for i,line in enumerate(lines):
  if 'AGX3 preflight:' in line:
   failures.append(line)
  elif 'no grounded AGX3 encoding' in line:
   failures.append('\n'.join(lines[i:i+3]))
 record={'bundle':str(bundle.resolve()),'returncode':result.returncode,'failures':failures,'diagnostics':str(out.resolve())}
 records.append(record)
 print(json.dumps(record),flush=True)
 (a.output/'summary.json').write_text(json.dumps(records,indent=2)+'\n')
if not records:raise SystemExit('No bundles found')
raise SystemExit(any(r['returncode'] for r in records))
