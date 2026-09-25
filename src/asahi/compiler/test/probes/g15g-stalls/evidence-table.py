import json, sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stalls import instrs, find_stalls
from fields import decompose_many
base = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wait-base.json')))
ASYNC = {'f32interp','itrcoords','ldcfr','uvsw','pixwait','lstcol','movmsk','depthf','pixrel','rld','lddev','ldtg','smp','copy','clr','atomicdev','atomictg','cndst','imgwci','ldimm','sttg','stdev'}
tab = collections.Counter(); ex = {}
producers = collections.Counter()
for f in sys.argv[1:]:
    for name, d in json.load(open(f))['kernels'].items():
        st, s = find_stalls(instrs(d['applegpu_g15g']))
        stalled = {x['index']: x['value'] for x in st}
        dec = decompose_many([x['hex'] for x in s])
        pend = collections.defaultdict(list)
        for k, x in enumerate(s):
            m = dec[x['hex']][0]; mn = m['mnemonic']; fl = m['fields']
            w = sum(fl.get(base[mn] + j, 0) << j for j in range(6)) if mn in base else 0
            if w or k in stalled:
                prods = tuple(sorted({p for j in range(6) if w >> j & 1 for p in pend[j]} or {'none'}))
                key = (mn + ' ' + m['ep'], prods, 'STALL' if k in stalled else 'nostall')
                tab[key] += 1; ex.setdefault(key, name)
                for j in range(6):
                    if w >> j & 1: pend[j] = []
            sl = fl.get(12, 0)
            if mn in ASYNC and 1 <= sl <= 6:
                pend[sl - 1].append(mn + ' ' + m['ep']); producers[mn + ' ' + m['ep']] += 1
for k, v in sorted(tab.items(), key=lambda kv: (kv[0][1], kv[0][2], kv[0][0])): print(v, k, ex[k])
print(producers)
