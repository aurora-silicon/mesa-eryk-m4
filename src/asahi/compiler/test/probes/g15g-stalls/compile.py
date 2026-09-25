#!/usr/bin/env python3
"""Compile every kernel of a .metal file with Apple's metal-nt for several
applegpu targets and store the extracted __text bytes as JSON.

usage: compile.py SRC.metal CASES.txt OUT.json [--fast-math] [--archs a,b,c]
"""
import argparse, json, os, subprocess, sys, tempfile, concurrent.futures as cf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from macho import texts

env = dict(os.environ, DEVELOPER_DIR=os.environ.get('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer'))
def run(args, **kw):
    return subprocess.run(args, env=env, check=True, capture_output=True, **kw)

ap = argparse.ArgumentParser()
ap.add_argument('src'); ap.add_argument('cases'); ap.add_argument('out')
ap.add_argument('--fast-math', action='store_true')
ap.add_argument('--render', action='store_true', help='cases are fragment functions; vertex function vs')
ap.add_argument('--archs', default='applegpu_g15g,applegpu_g15s,applegpu_g16g')
a = ap.parse_args()
mt = os.path.dirname(run(['xcrun', '-f', 'metal'], text=True).stdout.strip())
tmp = tempfile.mkdtemp(prefix='g15stall-')
flags = [] if a.fast_math else ['-fno-fast-math']
run(['xcrun', 'metal', '-c', *flags, a.src, '-o', f'{tmp}/k.air'])
run(['xcrun', 'metallib', f'{tmp}/k.air', '-o', f'{tmp}/k.metallib'])
names = [l.strip() for l in open(a.cases) if l.strip()]
archs = a.archs.split(',')

def one(job):
    name, arch = job
    js = f'{tmp}/{name}.{arch}.mtlp-json'
    if a.render:
        spec = {'libraries': {'paths': [{'label': 'probe', 'path': f'{tmp}/k.metallib'}]},
                'pipelines': {'render_pipelines': [{'vertex_function': 'alias:probe#vs', 'fragment_function': 'alias:probe#' + name,
                                                    'raster_sample_count': 1, 'color_attachments': [{'pixel_format': 'RGBA8Unorm'}]}]}}
    else:
        spec = {'pipelines': {'compute_pipelines': [{'compute_function': name}]}}
    open(js, 'w').write(json.dumps(spec))
    ob = f'{tmp}/{name}.{arch}.bin'
    r = subprocess.run([f'{mt}/metal-nt', '-arch', arch, '-platform_version', 'macos', '27.0', '27.0',
                        '-N', js, f'{tmp}/k.metallib', '-o', ob], env=env, capture_output=True, text=True)
    if r.returncode:
        return name, arch, None, r.stderr
    t = texts(open(ob, 'rb').read())
    if a.render:
        return name, arch, {p: d.hex() for p, d in t}, None
    assert len(t) == 1, (name, arch, [p for p, _ in t])
    return name, arch, t[0][1].hex(), None

res = {'toolchain': run([f'{mt}/metal-nt', '--version'], text=True).stdout.strip().splitlines()[0],
       'metal': run(['xcrun', 'metal', '--version'], text=True).stdout.strip().splitlines()[0],
       'flags': flags, 'kernels': {}}
with cf.ThreadPoolExecutor(os.cpu_count()) as ex:
    for name, arch, hx, err in ex.map(one, [(n, ar) for n in names for ar in archs]):
        if err: print('FAIL', name, arch, err.strip()[:200], file=sys.stderr); continue
        if isinstance(hx, dict):
            for p, h in hx.items():
                res['kernels'].setdefault(name + ':' + p, {})[arch] = h
        else:
            res['kernels'].setdefault(name, {})[arch] = hx
json.dump(res, open(a.out, 'w'), indent=1)
print(len(res['kernels']), 'kernels compiled')
