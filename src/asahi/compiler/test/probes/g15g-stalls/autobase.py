"""Derive per-mnemonic wait-field base (fields b..b+5 deposit encoded bits 12..17) automatically."""
import json, sys, os, subprocess, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stalls import instrs, find_stalls
from fields import decompose_many
BP = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wait-base.json')
base = json.load(open(BP))
inst = collections.defaultdict(dict)
for f in sys.argv[1:]:
    for name, d in json.load(open(f))['kernels'].items():
        st, s = find_stalls(instrs(d['applegpu_g15g']))
        dec = decompose_many([x['hex'] for x in s])
        for x in s:
            m = dec[x['hex']][0]
            inst[m['mnemonic']].setdefault(m['ep'], (x['hex'], m))
for mn, eps in inst.items():
    if mn in base: continue
    found = None
    for ep, (h, m) in eps.items():
        args = [os.path.join(os.environ.get('G15G_WORK', '/tmp/g15g-stalls-work'), 'fieldbits'), str(m['form'])] + [f'{k}:{v}' for k, v in m['fields'].items()] + ['--'] + [str(k) for k in range(160)]
        tog = {}
        for l in subprocess.run(args, capture_output=True, text=True).stdout.splitlines():
            if 'reject' in l or ':' not in l: continue
            k, _, bits = l.partition(':'); tog[int(k[1:])] = bits.split()
        for b in range(154):
            if all(tog.get(b + j) == [str(12 + j)] for j in range(6)):
                found = b; break
        if found is not None: break
    print(mn, list(eps), found)
    if found is not None: base[mn] = found
json.dump(base, open(BP, 'w'), indent=1, sort_keys=True)
