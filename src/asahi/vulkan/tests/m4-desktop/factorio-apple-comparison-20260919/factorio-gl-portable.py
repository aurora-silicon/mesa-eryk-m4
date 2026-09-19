#!/usr/bin/env python3
"""Audit a trusted apitrace and prepare explicit cross-device diagnostics.

Renumber only the unused high scratch texture unit (159 -> 31), preserving
all draws/shaders/uploads. Omit app profiling queries uniformly; replay timing
uses its own GL_TIME_ELAPSED queries. Refuse a unit-31 collision. Output includes
original call numbers and reasons for every profiling call omitted.
"""
import argparse,collections,hashlib,json,os,pickle,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('output',type=Path);p.add_argument('--apitrace',type=Path,required=True);a=p.parse_args()
sys.path.insert(0,str(a.apitrace.resolve().parents[1]/'lib/apitrace/scripts'))
proc=subprocess.Popen([str(a.apitrace),'pickle',str(a.trace)],stdout=subprocess.PIPE)
units=collections.Counter();skip=[];frames=[];copies=[]
while True:
 try:no,thread,fn,pairs,ret,flags=pickle.load(proc.stdout)
 except EOFError:break
 args=dict(pairs)
 if fn=='glActiveTexture':units[int(args['texture'])-0x84c0]+=1
 if fn in ('glBeginQuery','glEndQuery'):
  assert int(args['target'])==0x82f4,(no,args)
  skip.append(dict(call=no,function=fn,reason='Mac has no fragment-invocation statistics'))
 if fn=='glQueryCounter':
  assert int(args['target'])==0x8e28
  skip.append(dict(call=no,function=fn,reason='Mac rejects GL_TIMESTAMP; separate elapsed query measures frames'))
 if fn=='glXSwapBuffers':frames.append(no)
 if fn=='glCopyImageSubData':copies.append(no)
assert proc.wait()==0
assert 159 in units and 31 not in units and all(x<31 or x==159 for x in units),units
assert len(frames)==6
assert max(copies)<frames[0], 'CPU copy adapter must stay outside the five measured frames'
subprocess.run([str(a.apitrace),'sed','-e','s/GL_EMBOSS_MAP_NV/GL_TEXTURE31/','-o',str(a.output),str(a.trace)],check=True)
a.output.with_suffix('.ignore.txt').write_text(','.join(str(x['call']) for x in skip)+'\n')
a.output.with_suffix('.adaptation.json').write_text(json.dumps(dict(source=str(a.trace),source_sha256=hashlib.file_digest(a.trace.open('rb'),'sha256').hexdigest(),output=str(a.output),output_sha256=hashlib.file_digest(a.output.open('rb'),'sha256').hexdigest(),texture_units=units,remap={'159':31},omitted_queries=skip,frame_end_calls=frames,setup_copy_calls=copies,limitations=['Diagnostic comparison; application statistics and timestamp queries omitted on both drivers.','Mac uses explicit raw-byte startup texture-copy adapter.','No shaders/draws/visual settings changed. Validate output before timing.']),indent=2)+'\n')
print('PASS: audited portable GL trace',dict(frames=frames,units=units,omitted_queries=len(skip)))
