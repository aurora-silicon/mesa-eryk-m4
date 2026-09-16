#!/usr/bin/env python3
"""Inspect a saved G16G instruction through Mesa's generated encoding table.

Usage: native-form-decode.py TEXT.bin MNEMONIC OFFSET
Reports candidate anonymous fields, not semantic names or hardware validation.
The final candidate must reproduce every byte with the existing composer.
"""
import argparse, ast, ctypes, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path);p.add_argument('mnemonic');p.add_argument('offset',type=lambda x:int(x,0))
p.add_argument('--scan',action='store_true',help='Scan every two-byte offset from OFFSET; candidates are not instruction boundaries')
p.add_argument('--walk',action='store_true',help='Decode consecutive instructions, stopping at unmatched or ambiguous lengths; use mnemonic * to consider every form')
a=p.parse_args();root=Path(__file__).resolve().parents[1]
src=root/'mesa/m4-asahi/src/asahi/compiler/agx3_compose'
s=(src/'agx3b_tables.c').read_text()
def array(name):
 body=re.search(r'\b'+name+r'\[\]\s*=\s*\{(.*?)\n\};',s,re.S).group(1)
 body=re.sub(r'(?<=[0-9a-fA-F])ull\b','',body)
 return ast.literal_eval('['+body.replace('{','[').replace('}',']')+']')
forms=array('forms');nodes=array('nodes');fixed=array('fixeds');preds=array('preds');cmds=array('cmds');groups=array('groups');alts=array('altpool');lut=array('lutpool');sets=array('setpool')
raw=a.binary.read_bytes();data=raw[a.offset:];words=[int.from_bytes(data[i:i+2],'little') for i in range(0,len(data),2)]
def merge(st,field,mask,value):
 v,m=st.get(field,(0,0))
 if (v^value)&m&mask:return None
 t=st.copy();t[field]=(v|value,m|mask);return t
def pred(st,pr):
 field,kind,shift,width,x,y=pr
 if kind==3:
  t=merge(st,field,((1<<width)-1)<<shift,x<<shift)
  return [] if t is None else [t]
 vals=[x] if kind==0 else sets[x:x+y] if kind==1 else ([st[field][0]] if field in st and x<=st[field][0]<=y else range(x,min(y,x+1023)+1))
 out=[]
 for v in vals:
  t=merge(st,field,(1<<64)-1,v)
  if t is not None:out.append(t)
 return out
def node(idx,st):
 fo,fn,po,pn,co,cn,go,gn=nodes[idx]
 for w,mask,val in fixed[fo:fo+fn]:
  if w>=len(words) or words[w]&mask!=val:return []
 states=[st]
 for field,w,dshift,width,sshift,lo,ln in cmds[co:co+cn]:
  if w>=len(words):return []
  v=(words[w]>>dshift)&((1<<width)-1)
  vals=[v] if lo<0 else [i for i,x in enumerate(lut[lo:lo+ln]) if (x>>sshift)&((1<<width)-1)==v]
  nxt=[]
  for t in states:
   for x in vals:
    u=merge(t,field,((1<<width)-1)<<sshift if lo<0 else (1<<64)-1,x<<sshift if lo<0 else x)
    if u is not None:nxt.append(u)
  states=nxt
 for ao,an in groups[go:go+gn]:
  states=[u for t in states for alt in alts[ao:ao+an] for u in node(alt,t)]
  if len(states)>10000:raise RuntimeError('Candidate expansion exceeds diagnostic bound')
 for pr in preds[po:po+pn]: states=[u for t in states for u in pred(t,pr)]
 return states
lib=root/'build/native-compose-decode.so'
inputs=[src/'agx3_compose.c',src/'agx3b_tables.c',src/'agx3c_tables.c']
if not lib.exists() or lib.stat().st_mtime<max(x.stat().st_mtime for x in inputs):
 subprocess.run(['cc','-O2','-shared','-fPIC',*[str(x) for x in inputs],'-o',str(lib)],check=True)
c=ctypes.CDLL(str(lib)); table=(ctypes.c_byte*1).in_dll(c,'agx3b_table')
c.agx3_compose.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint64),ctypes.c_void_p];c.agx3_compose.restype=ctypes.c_bool
if a.walk and a.scan: p.error('--walk and --scan are mutually exclusive')
offsets=range(a.offset,len(raw),2) if a.scan or a.walk else [a.offset]
next_offset=a.offset
for offset in offsets:
 if a.walk and offset!=next_offset:continue
 candidates=[]
 data=raw[offset:];words=[int.from_bytes(data[i:i+2],'little') for i in range(0,len(data),2)]
 for i,(name,ep,size,idx) in enumerate(forms):
  if (a.mnemonic!='*' and name!=a.mnemonic) or len(data)<size:continue
  for st in node(idx,{}):
   f=(ctypes.c_uint64*160)(*[st.get(j,(0,0))[0] for j in range(160)]);out=(ctypes.c_byte*32)()
   if c.agx3_compose(ctypes.addressof(table),i,f,out) and bytes(out)[:size]==data[:size]:
    candidates.append((name,ep,size,{j:f[j] for j in range(160) if f[j]},data[:size].hex()))
 for candidate in candidates:
  if a.scan or a.walk:print(f'offset={offset:#x}',end=' ')
  print(*candidate)
 if a.walk:
  lengths={c[2] for c in candidates}
  if len(lengths)!=1:
   print(f'STOP offset={offset:#x}: '+('unmatched' if not lengths else f'ambiguous lengths {sorted(lengths)}'))
   break
  next_offset=offset+lengths.pop()
