#!/usr/bin/env python3
"""Run exact-pixel indexed multi-draw witnesses through the mandatory M3 guard.

Reuses g17p_offscreen_triangle.c's AGX_ROBUST_INDEX_BATCH_PROBE build. Exercises
batch boundaries, GPU-written arguments/counts, partial and wholly out-of-bounds
indices, zero work, concurrent heap allocations, and command-buffer reuse.
These synthetic witnesses are separate from CTS results and benchmark scores.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix', required=True)
p.add_argument('--sha256', required=True)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--disable-batching', action='store_true')
a = p.parse_args()
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
source = ROOT/'mesa/m3m4-merged/src/asahi/vulkan/tests/g17p_offscreen_triangle.c'
binary = a.output/'index-batch-probe'
command = ['aarch64-linux-gnu-gcc', '-O2', '-Wall', '-Wextra', '-Werror',
    '-DTEST_DEVICE_ID=0x6030', '-DAGX_ROBUST_INDEX_BATCH_PROBE=1',
    '-I'+str(ROOT/'mesa/m3m4-merged/include'), '-I'+str(source.parent),
    str(source), '-ldl', '-o', str(binary)]
subprocess.run(command, check=True)
manifest = dict(scope='Synthetic exact-pixel witness, not CTS or benchmark',
    compile_command=command, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    mesa_prefix=a.prefix, mesa_sha256=a.sha256, batching=not a.disable_batching)
(a.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
host = 'root@fedora-m3'
ssh = ['ssh', '-oConnectTimeout=10', host]
base = '/root/robust-index-batch-'+hashlib.sha256(str(a.output).encode()).hexdigest()[:12]
subprocess.run(ssh+[shlex.join(['mkdir', base])], check=True)
subprocess.run(['scp', str(binary), str(a.output/'manifest.json'), host+':'+base+'/'], check=True)
identity = subprocess.check_output(ssh+[shlex.join(['python3', '-c',
    'import hashlib,json,sys;from pathlib import Path;'
    'p=Path(sys.argv[1]);assert not (p/"CANDIDATE_INCOMPLETE").exists();'
    'assert hashlib.sha256((p/"lib/libvulkan_asahi.so").read_bytes()).hexdigest()==sys.argv[2];'
    'print(json.dumps(dict(boot_id=Path("/proc/sys/kernel/random/boot_id").read_text().strip())))',
    a.prefix, a.sha256])], text=True)
manifest.update(json.loads(identity))
(a.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
cases = []
def add(name, **env):
    cases.append(dict(name=name, env={'AGX_BATCH_'+k.upper():str(v) for k,v in env.items()}))
for count in (2, 127, 128, 129, 257, 2048):
    add('last-valid-'+str(count), count=count, scenario=0)
    add('last-partial-'+str(count), count=count, scenario=1)
for bits in (16, 32):
    for scenario in (1, 2, 3, 4):
        add('parallel-%d-%d'%(bits,scenario), count=257, scenario=scenario,
            parallel=1, index_bits=bits)
for active in (0, 127, 128, 255, 256):
    add('selected-'+str(active), count=257, active_draw=active, scenario=1)
add('count-last-partial', count=257, indirect_count=1, scenario=1)
add('count-predicated-last', count=257, indirect_count=1, executed=256, scenario=1)
add('count-zero', count=257, indirect_count=1, executed=0, scenario=1)
(a.output/'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
results=[]
for i,case in enumerate(cases):
    label='%02d-%s'%(i,case['name'])
    env=dict(VK_DRIVER_FILES=a.prefix+'/asahi.json', LD_LIBRARY_PATH=a.prefix+'/lib',
        ASAHI_M3_EXPERIMENTAL='1', AGX_MESA_DEBUG='nopromote',
        ASAHI_MESA_DEBUG='nocompress', VK_LAYER_PATH='/var/empty',
        VK_IMPLICIT_LAYER_PATH='/var/empty', ASAHI_TEST_REPEAT='2',
        HK_BATCH_ROBUST_INDEX='false' if a.disable_batching else 'true', **case['env'])
    cmd=['python3','/root/m3-gpu-tools/gpu/guard.py', '--seconds','30',
         '--keep-cpu-clocks',base+'/'+label, '--', 'env',
         *[k+'='+v for k,v in env.items()], base+'/index-batch-probe']
    with (a.output/(label+'.log')).open('w') as log:
        rc=subprocess.run(ssh+[shlex.join(cmd)], stdout=log, stderr=subprocess.STDOUT,
                          timeout=90).returncode
    subprocess.run(['rsync','-az',host+':'+base+'/'+label+'/',str(a.output/label)+'/'],
                   check=True,timeout=90)
    guard=json.loads((a.output/label/'result.json').read_text())
    workload=(a.output/label/'workload.log').read_text()
    ok=not rc and guard['phase']=='complete' and ': PASS' in workload
    results.append(dict(case=case, path=str(a.output/label), passed=ok,
                        returncode=rc, guard_phase=guard['phase']))
    (a.output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
    print(label, 'Pass' if ok else 'FAIL', flush=True)
    if not ok:
        raise SystemExit('Stopped at failed witness; evidence retained')
print(json.dumps(dict(passed=len(results), failed=0, scope=manifest['scope'])))
