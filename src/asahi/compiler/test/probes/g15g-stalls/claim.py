"""Verify: stripping stalls from G15G gives G15S, up to branch displacements
that grow by exactly 8 bytes per stall between branch and target."""
import json, sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stalls import instrs, find_stalls
from fields import decompose_many
c = collections.Counter()
for f in sys.argv[1:]:
    for name, d in json.load(open(f))['kernels'].items():
        g = instrs(d['applegpu_g15g']); s = instrs(d['applegpu_g15s'])
        st, gs = find_stalls(g)
        c['kernels'] += 1
        if len(gs) != len(s): c['different_instruction_count'] += 1; continue
        diffs = [(a, b) for a, b in zip(gs, s) if a['hex'] != b['hex']]
        if not diffs: c['identical_after_strip'] += 1; continue
        dec = decompose_many([a['hex'] for a, _ in diffs] + [b['hex'] for _, b in diffs])
        ok = True
        stall_offs = [x['off'] for x in st]
        for a, b in diffs:
            ma, mb = dec[a['hex']][0], dec[b['hex']][0]
            if ma['mnemonic'] not in ('b', 'bn') or {k: v for k, v in ma['fields'].items() if k} != {k: v for k, v in mb['fields'].items() if k}:
                ok = False; break
            da, db = ma['fields'].get(0, 0), mb['fields'].get(0, 0)
            da = da - (1 << 48) if da >> 47 else da; db = db - (1 << 48) if db >> 47 else db
            lo, hi = sorted((a['off'], a['off'] + da))
            crossed = sum(1 for o in stall_offs if lo <= o < hi)
            if da != db + (8 * crossed if da > 0 else -8 * crossed): ok = False; c['bad_fixup'] += 1; break
        c['branch_displacement_only' if ok else 'other_differences'] += 1
print(dict(c))
