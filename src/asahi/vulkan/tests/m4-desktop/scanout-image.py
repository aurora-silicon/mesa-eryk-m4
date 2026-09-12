#!/usr/bin/env python3
"""Convert a saved read-only M4 native scanout snapshot to PNG (host only).
Usage: scanout-image.py INPUT.m4fb OUTPUT.png
"""
import argparse, json, struct
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('output',type=Path)
p.add_argument('--allow-kms-allocation',action='store_true',help='Decode display-allocated scanout too; allocation origin is not rendering provenance')
a=p.parse_args();raw=a.source.read_bytes()
magic,width,height,stride,fmt,fb,imported,size=struct.unpack_from('<8I',raw)
assert magic==0x4246344d and len(raw)==size+32 and imported in (0,1)
assert imported==1 or a.allow_kms_allocation, 'Display-allocated buffer: explicitly select --allow-kms-allocation'
assert 0<width<=8192 and 0<height<=8192 and stride>=width*4 and size>=stride*height
assert fmt in (int.from_bytes(b'XR24','little'),int.from_bytes(b'AR24','little'))
Image.frombytes('RGB',(width,height),raw[32:],'raw','BGRX',stride,1).save(a.output)
print(json.dumps(dict(width=width,height=height,stride=stride,framebuffer_id=fb,imported=bool(imported),output=str(a.output))))
