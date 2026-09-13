#!/usr/bin/env python3
"""Run the pinned G16G PBE constructor offline on owned synthetic data.

No GPU execution. Arguments are the recovered internal ABI, not Metal enums.
Use build/display-venv/bin/python; retain JSON alongside native compiler probes.
"""
import argparse, hashlib, json, struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
import unicorn.arm64_const as r
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('umd',type=Path)
p.add_argument('--samples',type=int,choices=[1,2,4],default=1)
p.add_argument('--layout',type=int,choices=[1,2],default=1)
p.add_argument('--width',type=int,default=64)
p.add_argument('--height',type=int,default=64)
p.add_argument('--kind',type=int,choices=[2,3],default=2,help='Recovered native kind: 2=2D, 3=3D')
p.add_argument('--depth',type=int,default=1)
p.add_argument('--layer-stride',type=int,default=0)
a=p.parse_args();blob=a.umd.read_bytes();digest=hashlib.sha256(blob).hexdigest()
if digest!='edd46e01416e595b51734428ece82aa0104868b35d585f492b2efa3ceebbce3a':p.error('wrong UMD version')
u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0,(len(blob)+4095)&~4095);u.mem_write(0,blob)
base=0x4000000;u.mem_map(base,0x10000);sp=base+0xe000;stop=base+0xf000
# PAC return is irrelevant in this isolated call; retain all arithmetic.
u.mem_write(0x336858,struct.pack('<I',0xd65f03c0))
stack=bytearray(128)
struct.pack_into('<Q',stack,16,0x12340000) # buffer address
for off,val in [(40,256),(48,a.width),(52,a.height),(56,a.depth),(60,0),(64,0),(68,0),(72,1),(76,1)]:struct.pack_into('<I',stack,off,val)
struct.pack_into('<Q',stack,80,a.layer_stride)
u.mem_write(sp,bytes(stack))
# x4 is log2(samples); x6 holds tiled layout, 2D semantic and MS flag.
args=[base,0,0x28,0,a.samples.bit_length()-1,(2<<6)|(3<<9)|(4<<12)|(5<<15),((a.kind<<8)|a.layout)|(0x20000 if a.samples>1 else 0)]
for i,v in enumerate(args):u.reg_write(getattr(r,'UC_ARM64_REG_X'+str(i)),v)
u.reg_write(r.UC_ARM64_REG_SP,sp);u.reg_write(r.UC_ARM64_REG_LR,stop)
u.emu_start(0x33620c,stop,timeout=1000000,count=10000)
if u.reg_read(r.UC_ARM64_REG_PC)!=stop:raise RuntimeError('constructor did not return')
out=bytes(u.mem_read(base,32))
print(json.dumps(dict(umd_sha256=digest,function='0x33620c',samples=a.samples,layout=a.layout,width=a.width,height=a.height,kind=a.kind,depth=a.depth,layer_stride=a.layer_stride,register_args=args,stack=stack.hex(),descriptor=out.hex(),words=[hex(x)for x in struct.unpack('<4Q',out)],limitation='Offline ABI/descriptor witness, not GPU validation.'),indent=2))
