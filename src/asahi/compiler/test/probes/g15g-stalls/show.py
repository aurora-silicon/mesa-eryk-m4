import json, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agx3dis import walk
from fields import decompose_many
base = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wait-base.json')))
f, k = sys.argv[1], sys.argv[2]
arch = sys.argv[3] if len(sys.argv) > 3 else 'applegpu_g15g'
code = bytes.fromhex(json.load(open(f))['kernels'][k][arch])
L = list(walk(code))
dec = decompose_many([b.hex() for _, _, _, b, _ in L])
for off, l, n, b, asm in L:
    m = dec[b.hex()][0] if dec[b.hex()] else None
    if n == 'nop_ep0' and b[1] == 0: continue
    mn = m['mnemonic'] if m else '?'
    fl = m['fields'] if m else {}
    w = sum(fl.get(base[mn] + j, 0) << j for j in range(6)) if mn in base else '?'
    print(f'{off:4x} {b.hex():<32} {mn:10} {m["ep"] if m else ""} wait={w} slot={fl.get(12,0)}  {dict(sorted(fl.items()))}')
