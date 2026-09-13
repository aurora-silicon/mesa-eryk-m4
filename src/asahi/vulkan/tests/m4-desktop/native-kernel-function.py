#!/usr/bin/env python3
"""Extract and disassemble one named ARM64 Mach-O function, retaining provenance.

Offline only. Example: native-kernel-function.py kernelcache.macho SYMBOL
--output NEW_DIRECTORY. Uses exact symbol and segment addresses, never guessing
that a kernel virtual address is a file offset. No source/capture is modified.
"""
import argparse, hashlib, json, struct, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('image',type=Path);p.add_argument('symbol')
p.add_argument('--output',required=True,type=Path)
a=p.parse_args();data=a.image.read_bytes()
if len(data)<32 or struct.unpack_from('<II',data)[0:2]!=(0xfeedfacf,0x100000c):p.error('requires little-endian ARM64 Mach-O')
raw=subprocess.check_output(['llvm-nm','--defined-only',str(a.image)],text=True)
symbols=[]
for line in raw.splitlines():
 fields=line.split()
 if len(fields)==3 and fields[1].lower()=='t':symbols.append((int(fields[0],16),fields[2]))
matches=[addr for addr,name in symbols if name==a.symbol]
if len(matches)!=1:p.error('symbol must match exactly one text symbol')
start=matches[0];ends=[addr for addr,name in symbols if addr>start]
if not ends:p.error('no following text symbol to bound function')
end=min(ends);length=end-start
if not 0<length<=256*1024:p.error('function extent exceeds bounded disassembly size')
off=32;source=None
for _ in range(struct.unpack_from('<I',data,16)[0]):
 if off+8>len(data):p.error('truncated load command')
 cmd,size=struct.unpack_from('<II',data,off)
 if size<8 or off+size>len(data):p.error('invalid load command extent')
 if cmd==0x19:
  if size<72:p.error('truncated segment')
  va,vs,fo,fs=struct.unpack_from('<4Q',data,off+24)
  if va<=start<end<=va+min(vs,fs):source=fo+start-va
 off+=size
if source is None or source+length>len(data):p.error('function does not fit a file-backed segment')
a.output.mkdir(parents=True,exist_ok=False)
body=a.output/'function.bin';body.write_bytes(data[source:source+length])
cmd=['aarch64-linux-gnu-objdump','-D','-b','binary','-m','aarch64',f'--adjust-vma={start}',str(body)]
with (a.output/'disassembly.txt').open('w') as f:subprocess.run(cmd,stdout=f,check=True)
(a.output/'manifest.json').write_text(json.dumps(dict(image=str(a.image.resolve()),image_sha256=hashlib.sha256(data).hexdigest(),symbol=a.symbol,virtual_address=hex(start),file_offset=hex(source),size=length,function_sha256=hashlib.sha256(body.read_bytes()).hexdigest(),command=cmd),indent=2)+'\n')
print(a.output)
