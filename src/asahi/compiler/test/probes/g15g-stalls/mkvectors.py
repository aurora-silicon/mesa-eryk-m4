#!/usr/bin/env python3
"""Emit test vectors 'name stripped_hex expected_hex' from compiled corpus JSON.

stripped = Apple's G15G code with every stall removed and b/bn displacements
recomputed for the shorter stream; expected = Apple's G15G code."""
import json, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stalls import instrs, find_stalls
from fields import decompose_many

def strip(hx):
    g = instrs(hx)
    st, s = find_stalls(g)
    # new offset of every kept instruction
    stall_offs = sorted(x['off'] for x in st)
    newoff, si = {}, 0
    for x in s:
        while si < len(stall_offs) and stall_offs[si] < x['off']:
            si += 1
        newoff[x['off']] = x['off'] - 8 * si
    dec = decompose_many([x['hex'] for x in s])
    out = bytearray()
    for x in s:
        b = bytearray(x['raw'])
        m = dec[x['hex']][0]
        if m['mnemonic'] in ('b', 'bn'):
            assert len(b) == 10
            disp = int.from_bytes(b[3:9], 'little')
            assert disp == m['fields'].get(0, 0), (x['hex'], m)
            if disp >> 47: disp -= 1 << 48
            tgt = x['off'] + disp
            # a branch to a stall targets the instruction after it
            while tgt not in newoff: tgt += 2
            nd = newoff[tgt] - newoff[x['off']]
            b[3:9] = (nd & ((1 << 48) - 1)).to_bytes(6, 'little')
        out += b
    return out.hex(), len(st)

if __name__ == '__main__':
    for f in sys.argv[1:]:
        tag = os.path.splitext(os.path.basename(f))[0]
        for name, d in json.load(open(f))['kernels'].items():
            hx = d['applegpu_g15g']
            sh, n = strip(hx)
            print(f'{tag}/{name.replace(":", "/").replace(",", "_")} {sh} {hx}')
