#!/usr/bin/env python3
"""Identify G15G stall groups and verify stall removal reproduces G15S."""
import json, sys, os, subprocess, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agx3dis import walk

HEADER_END = 0x40   # constant program + 06 00 filler precede the entry

def instrs(hx):
    code = bytes.fromhex(hx)
    return [dict(off=off, len=l, name=n, hex=b.hex(), raw=b) for off, l, n, b, asm in walk(code)]

def find_stalls(g):
    """Return (list of stall dicts, stripped instruction list)."""
    stalls, out, k = [], [], 0
    while k < len(g):
        x = g[k]
        if x['name'] == 'nop_ep0' and x['raw'][1] != 0 and len(g[k:k+4]) == 4 and all(y['hex'] == '0600' for y in g[k+1:k+4]):
            stalls.append(dict(index=len(out), off=x['off'], value=(x['raw'][1] >> 2) & 0x3f, low=x['raw'][1] & 3, raw=x['hex']))
            k += 4
            continue
        out.append(x)
        k += 1
    return stalls, out

if __name__ == '__main__':
    tot = collections.Counter()
    for f in sys.argv[1:]:
        data = json.load(open(f))['kernels']
        for name, d in data.items():
            g = instrs(d['applegpu_g15g']); s = instrs(d['applegpu_g15s'])
            st, stripped = find_stalls(g)
            tot['kernels'] += 1; tot['stalls'] += len(st)
            tot['bytes_g'] += len(bytes.fromhex(d['applegpu_g15g'])); tot['bytes_s'] += len(bytes.fromhex(d['applegpu_g15s']))
            if len(stripped) != len(s):
                tot['count_mismatch'] += 1; print('COUNT', name, len(stripped), len(s)); continue
            diff = [(a, b) for a, b in zip(stripped, s) if a['hex'] != b['hex']]
            names = collections.Counter(a['name'] for a, b in diff)
            if diff: tot['kernels_with_diff'] += 1
            for a, b in diff: tot['diff_' + a['name']] += 1
            if any(n for n in names if n not in ('bn', 'jmp_any', 'bz', 'bnz') and not n.startswith('b')):
                print('NONBRANCH DIFF', name, [(a['name'], a['hex'], b['hex']) for a, b in diff])
            if 'applegpu_g16g' in d and d['applegpu_g16g'] != d['applegpu_g15s']:
                tot['g16g_ne_g15s'] += 1
    print(dict(tot))
