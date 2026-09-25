"""Field-vector decomposition of AGX3b instructions via the composer tables (cached)."""
import subprocess, os, json
WORK = os.environ.get('G15G_WORK', '/tmp/g15g-stalls-work')
DECOMP = os.path.join(WORK, 'decomp')
CACHE = os.path.join(WORK, 'fields-cache.json')
_cache = json.load(open(CACHE)) if os.path.exists(CACHE) else {}
for _ms in _cache.values():
    for _m in _ms:
        _m['fields'] = {int(k): v for k, v in _m['fields'].items()}

def decompose_many(hexes):
    todo = sorted({h for h in hexes if h not in _cache})
    if todo:
        p = subprocess.run([DECOMP], input='\n'.join(todo) + '\n', text=True, capture_output=True, check=True)
        blocks = p.stdout.split('END\n')
        assert len(blocks) - 1 == len(todo), (len(blocks), len(todo))
        for h, b in zip(todo, blocks):
            ms = []
            for line in b.splitlines():
                if line.startswith('MATCH'):
                    head, _, fl = line.partition(' :')
                    parts = head.split()
                    ms.append(dict(form=int(parts[2]), mnemonic=parts[3], ep=parts[4],
                                   fields={int(k): int(v) for k, v in (t.split(':') for t in fl.split())}))
            _cache[h] = ms
        json.dump(_cache, open(CACHE, 'w'))
    return {h: _cache[h] for h in hexes}
