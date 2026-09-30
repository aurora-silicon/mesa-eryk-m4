#!/usr/bin/env python3
"""Summarize opt-in GL interval diagnostics; never produces a benchmark score.

Input: a retained replay.log from the completed/predecoded interval profiler.
CPU call times include waits, worker threads are excluded. Global native
retirement spans include other clients and overlapping engines. Shader/dispatch
record frequencies do not measure coverage or execution cost.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path)
p.add_argument('--output',type=Path)
a=p.parse_args()
s=a.log.read_text()
bounds=re.findall(r'PROFILE_INTERVAL (begin|end) monotonic_ns=(\d+)',s)
assert [x[0] for x in bounds]==['begin','end'], bounds
interval=s.split('PROFILE_INTERVAL begin',1)[1].split('PROFILE_INTERVAL end',1)[0]
marker=re.findall(r'BENCH_RANGE first=(\d+) last=(\d+) frames=(\d+) ns=(\d+)',s)
assert len(marker)==1
first,last,frames,ns=map(int,marker[0])
calls=[dict(name=n,count=int(c),ns=int(t),maximum_ns=int(m)) for n,c,t,m in
       re.findall(r'CALL_COST name=(\w+) count=(\d+) ns=(\d+) max_ns=(\d+)',s)]
completion=[dict(call=int(c),ns=int(t),replay_ns=int(r),wait_ns=int(w)) for c,t,r,w in
            re.findall(r'COMPLETED_FRAME call=(\d+) ns=(\d+) replay_ns=(\d+) finish_wait_ns=(\d+)',s)
            if first<int(c)<=last]
if completion:assert len(completion)==frames
counters={}
for edge,engine,fields in re.findall(r'PROFILE_COUNTER (begin|end) (render|compute) ([^\n]+)',s):
 counters.setdefault(engine,{})[edge]={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',fields)}
deltas={k:{n:v-rows['begin'][n] for n,v in rows['end'].items()} for k,rows in counters.items()}
assert all(v>=0 for d in deltas.values() for v in d.values()), 'Counter reset'
shader=Counter(re.sub(r' cs=[\da-f]+','',l) for l in interval.splitlines() if l.startswith('HK_SHADER_PROFILE'))
compute=Counter(l.split('size=',1)[1] for l in interval.splitlines() if l.startswith('G17P compute-record:'))
cpu_rows={edge:dict(user_us=int(user),system_us=int(system)) for edge,user,system in
          re.findall(r'PROFILE_CPU (begin|end) user_us=(\d+) system_us=(\d+)',s)}
cpu_delta={k:cpu_rows['end'][k]-cpu_rows['begin'][k] for k in cpu_rows.get('begin',{})}
r=dict(diagnostic_only=True,log=str(a.log),first=first,last=last,frames=frames,
       interval_ns=ns,monotonic_ns=[int(x[1]) for x in bounds],
       process_cpu_delta_us=cpu_delta,
       cpu_calls=sorted(calls,key=lambda r:-r['ns']),completed_frames=completion,
       global_retirement_deltas=deltas,shader_selections=shader.most_common(),
       compute_dispatches=compute.most_common(),
       descriptor_rejections=sorted(set(l for l in s.splitlines() if l.startswith('HK_DIRECT_REJECT'))),
       limits=__doc__)
text=json.dumps(r,indent=2)+'\n'
if a.output:a.output.write_text(text)
else:print(text,end='')
