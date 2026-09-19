#!/usr/bin/env python3
"""Group HK_M4_SHADER_PROFILE logs by selected shader and render area.

Enable only during a bounded diagnostic run; hashing/logging perturbs timing.
Uploads are not draw counts or GPU time. Code hashes are local comparison keys,
not cryptographic build identities. agx_shader_replay prints matching keys.
"""
import argparse, collections, json, re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path)
a=p.parse_args()
counts=collections.Counter()
for line in a.log.read_text(errors='replace').splitlines():
    if line.startswith('HK_SHADER_PROFILE '):
        values=tuple(re.findall(r'(\w+)=([\da-f]+)',line))
        counts[values]+=1
rows=[]
for key,count in counts.most_common():
    row={k:v if k=='code' else int(v) for k,v in key}
    row['uploads']=count
    rows.append(row)
print(json.dumps({'scope':'Shader uploads, not draws or GPU time','shaders':rows},indent=2))
