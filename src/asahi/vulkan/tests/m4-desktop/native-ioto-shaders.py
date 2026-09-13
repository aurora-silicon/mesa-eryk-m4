#!/usr/bin/env python3
"""Find GPU0 Ioto LDSHDR candidates for a known shader in a retained native capture.

Usage: native-ioto-shaders.py CAPTURE/encoded --shader-va 0x10000000440
Offline only. This scans candidate records, not program control flow. It reports
adjacent SPECTPR allocation when present; anonymous LDSHDR f1 stays uninterpreted.
"""
import argparse, hashlib, json, struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path)
p.add_argument('--shader-va',type=lambda s:int(s,0),required=True)
a=p.parse_args()
if not 0<a.shader_va<1<<48 or a.shader_va%64:p.error('shader VA must be 48-bit and 64-byte aligned')
manifest=json.loads((a.directory/'manifest.json').read_text());seen=set();records=[]
for resource in manifest['namespaces']['resources']['objects']:
 name=resource.get('payload_file')
 if not name:continue
 path=a.directory/name
 if path.resolve().parent != a.directory.resolve():raise ValueError('payload escapes capture directory')
 data=path.read_bytes();sha=hashlib.sha256(data).hexdigest()
 if sha in seen:continue
 seen.add(sha)
 for off in range(0,len(data)-9,2):
  w=struct.unpack_from('<5H',data,off)
  if w[0]&0x7ff!=0x177:continue
  address=((w[1]>>7)<<6)|(w[2]<<15)|(w[3]<<31)|((w[4]&1)<<47)
  if address!=a.shader_va:continue
  r={'resource_id':resource['id'],'file':str(path.resolve()),'sha256':sha,
     'offset':off,'shader_va':hex(address),'ldshdr_bytes':data[off:off+10].hex(),
     'f1':(1,2,0,3)[(w[4]>>1)&3]}
  if off+20<=len(data):
   q=struct.unpack_from('<5H',data,off+10)
   if q[0]&0x7ff==0xf7:
    dwords=((q[1]>>15)&1)*2+(q[2]&1023)*4
    r.update(spectpr_bytes=data[off+10:off+20].hex(),
             staging_dwords=dwords,staging_bytes=dwords*4)
  records.append(r)
print(json.dumps({'shader_va':hex(a.shader_va),'candidates':records},indent=2))
raise SystemExit(0 if records else 1)
