#!/usr/bin/env python3
"""Run the multi-draw resource pixel oracle through M3's mandatory guard.

The C probe is compiled natively before either GPU run. Each option variant
gets its own bounded guard/evidence directory; the normal desktop is untouched.
Source, binary, Mesa identity, command, environment and guard receipts are kept.
Synthetic pixel oracles are not CTS results or benchmark scores.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--api', choices=('opengl','vulkan'), default='opengl')
p.add_argument('--oracle-marker', default='PASS: 48')
p.add_argument('--source', required=True, type=Path)
p.add_argument('--prefix', required=True)
p.add_argument('--sha256', required=True)
p.add_argument('--gallium-sha256', help='Also verify an isolated Gallium candidate')
p.add_argument('--cpu-affinity', help='Restrict only the workload to this taskset CPU list')
p.add_argument('--gpu-uploads', action='store_true', help='Use GL buffer copies to update live UBO contents')
p.add_argument('--direct-draws', action='store_true', help='Exercise repeated direct draws instead of multi-draw')
p.add_argument('--profile-loader', action='store_true', help='Record complete loader cache hits/misses')
p.add_argument('--default-only', action='store_true',
               help='Exercise the driver default with the option absent')
p.add_argument('--output', required=True, type=Path)
p.add_argument('--option', default='HK_M3_MULTIDRAW_UBO_PROOF',
               help='Boolean diagnostic option compared false/true')
a = p.parse_args()
if not a.source.is_file():
    p.error('Probe source does not exist: ' + str(a.source))
a.source = a.source.resolve()
assert ((a.option.startswith('HK_') and a.option.replace('_', '').isalnum()) or
        a.option == 'ZINK_PREFER_HIGH_CAPACITY_CPUS')
if a.cpu_affinity:
    assert re.fullmatch(r'[0-9]+(?:-[0-9]+)?(?:,[0-9]+(?:-[0-9]+)?)*', a.cpu_affinity)
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
ssh = ['ssh', '-oConnectTimeout=10', 'root@fedora-m3']
remote = '/root/multidraw-resources-' + hashlib.sha256(
    str(a.output).encode()).hexdigest()[:12]
subprocess.run(ssh + [shlex.join(['mkdir', remote])], check=True)
subprocess.run(['scp', str(a.source), 'root@fedora-m3:' + remote + '/probe.c'],
               check=True)
command = ['cc', '-O2', '-Wall', '-Wextra', '-Werror', remote + '/probe.c',
           *(['-lEGL','-lGL'] if a.api=='opengl' else ['-lvulkan']), '-o', remote + '/probe']
with (a.output / 'compile.log').open('w') as f:
    subprocess.run(ssh + [shlex.join(command)], stdout=f,
                   stderr=subprocess.STDOUT, check=True, timeout=60)
code = '''import sys,json,hashlib;from pathlib import Path
p=Path(sys.argv[1]);assert not (p/'CANDIDATE_INCOMPLETE').exists()
assert hashlib.sha256((p/'lib/libvulkan_asahi.so').read_bytes()).hexdigest()==sys.argv[2]
g=hashlib.sha256((p/'lib/libgallium-26.1.4.so').read_bytes()).hexdigest()
if sys.argv[4]:assert g==sys.argv[4]
print(json.dumps(dict(boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
 gallium_sha256=g,
 binary_sha256=hashlib.sha256(Path(sys.argv[3]).read_bytes()).hexdigest())))'''
manifest = json.loads(subprocess.check_output(ssh + [shlex.join(
    ['python3', '-c', code, a.prefix, a.sha256, remote + '/probe',
     a.gallium_sha256 or ''])], text=True))
manifest.update(remote=remote, compile_command=command, option=a.option,
    api=a.api, oracle_marker=a.oracle_marker, direct_draws=a.direct_draws, gpu_uploads=a.gpu_uploads,
    source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),
    mesa_prefix=a.prefix, mesa_sha256=a.sha256,
    cpu_affinity=a.cpu_affinity,
    default_only=a.default_only,
    scope='Synthetic multi-draw resource pixel oracles, not CTS or benchmark')
(a.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
results = []
variants = [('default', None)] if a.default_only else [('baseline', 'false'), ('candidate', 'true')]
for label, option in variants:
    env = dict(PATH='/usr/bin:/bin', LD_LIBRARY_PATH=a.prefix + '/lib',
        VK_DRIVER_FILES=a.prefix + '/asahi.json',
        LIBGL_DRIVERS_PATH=a.prefix + '/lib/dri',
        GBM_BACKENDS_PATH=a.prefix + '/lib/gbm',
        __EGL_VENDOR_LIBRARY_FILENAMES=a.prefix + '/egl.json',
        MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink',
        ASAHI_M3_EXPERIMENTAL='1', ASAHI_M4_EXPERIMENTAL='1',
        HK_DEBUG_LOG='0', AGX_MESA_DEBUG='nopromote',
        EGL_PLATFORM='surfaceless', MESA_SHADER_CACHE_DIR=remote + '/cache',
        HK_M4_PROGRAM_PROFILE='true')
    if a.gpu_uploads:
        env['M3_PROBE_GPU_UPLOADS'] = '1'
    if a.direct_draws:
        env['M3_PROBE_DIRECT_DRAWS'] = '1'
    if a.api=='vulkan':
        env['HK_ARCHITECTURE_PROFILE']='true'
    if a.profile_loader:
        env['HK_NATIVE_PROGRAM_PROFILE'] = 'true'
    if option is not None:
        env[a.option] = option
    if a.option == 'ZINK_PREFER_HIGH_CAPACITY_CPUS':
        env['M3_PROBE_CHECK_WORKER_AFFINITY'] = '1'
    workload = ['env', '-i', *[k + '=' + v for k, v in env.items()], remote + '/probe']
    if a.cpu_affinity:
        workload = ['taskset', '--cpu-list', a.cpu_affinity, *workload]
    cmd = ['python3', '/root/m3-gpu-tools/gpu/guard.py', '--seconds', '40',
        '--keep-cpu-clocks', remote + '/' + label, '--', *workload]
    (a.output / (label + '-command.json')).write_text(
        json.dumps(cmd, indent=2) + '\n')
    with (a.output / (label + '-host.log')).open('w') as f:
        r = subprocess.run(ssh + [shlex.join(cmd)], stdout=f,
                           stderr=subprocess.STDOUT, timeout=90)
    subprocess.run(['rsync', '-az', 'root@fedora-m3:' + remote + '/' + label + '/',
                    str(a.output / label) + '/'], check=True, timeout=30)
    guard = json.loads((a.output / label / 'result.json').read_text())
    workload = (a.output / label / 'workload.log').read_text()
    ok = (not r.returncode and guard['phase'] == 'complete' and
          guard['boot_id'] == manifest['boot_id'] and a.oracle_marker in workload)
    results.append(dict(label=label, passed=ok, environment=env, guard=guard))
    (a.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(label, 'Pass' if ok else 'FAIL', flush=True)
    if not ok:
        raise SystemExit('Oracle failed; evidence retained; no further GPU run')
