#!/usr/bin/env python3
"""Offline native G16G SPECLM encoder oracle. No GPU or network access.

Run with build/display-venv/bin/python and the retained 25G83 arm64 UMD slice.
Executes the exact imageblock arithmetic and instruction encoder with synthetic
owned structures. Validates stride encoding, not GPU execution or geometry.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_SP, UC_ARM64_REG_LR, UC_ARM64_REG_PC

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('umd', type=Path)
p.add_argument('--stride', type=int, action='append')
a = p.parse_args()
blob = a.umd.read_bytes()
digest = hashlib.sha256(blob).hexdigest()
if digest != 'edd46e01416e595b51734428ece82aa0104868b35d585f492b2efa3ceebbce3a':
    p.error('requires the retained G16G 25G83 arm64 UMD slice')
u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
u.mem_map(0, (len(blob)+4095)&~4095); u.mem_write(0, blob)
base = 0x4000000
u.mem_map(base, 0x10000)
stop = base+0xf000
u.reg_write(UC_ARM64_REG_SP, base+0xe000)

def call(pc, args):
    for reg, value in zip((UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3), args):
        u.reg_write(reg, value)
    u.reg_write(UC_ARM64_REG_LR, stop)
    u.emu_start(pc, stop, timeout=1000000, count=10000)
    if u.reg_read(UC_ARM64_REG_PC) != stop:
        raise RuntimeError('native encoder did not return within bound')
    return u.reg_read(UC_ARM64_REG_X0), u.reg_read(UC_ARM64_REG_X1)

rows = []
for stride in a.stride or [8, 16, 24, 248]:
    if stride < 8 or stride > 248 or stride % 8:
        p.error('stride must be a multiple of eight between 8 and 248')
    lo, hi = call(0x315428, [stride//4, 1, 32, 32])
    # SpecLM semantic object: active H, imageblock low and high pairs,
    # then default flags/resource fields. No native pointers escape this call.
    data = struct.pack('<QQQ', 1<<32, lo, hi) + bytes(24)
    u.mem_write(base, data); u.mem_write(base+0x100, bytes(16))
    call(0x2a7834, [base+0x100, base])
    result = bytes(u.mem_read(base+0x100, 8))
    rows.append(dict(stride_B=stride, semantic=data.hex(), speclm=result.hex()))
print(json.dumps(dict(umd_sha256=digest, functions=['0x315428','0x2a7834'], results=rows,
    limitation='Offline arithmetic and encoder execution only; no hardware validation.'), indent=2))
