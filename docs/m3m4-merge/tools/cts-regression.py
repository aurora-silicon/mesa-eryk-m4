#!/usr/bin/env python3
"""Run a bounded CTS regression selection with the existing per-GPU guards.

Use a fresh output directory for each revision. Does not change full-catalog
outcomes or install a driver into the desktop's prefix. Cases must have the
same .meta.json sidecar as cts-bulk/cts-paired selections.
"""
import argparse
from collections import Counter
import fcntl
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BINARY = '/root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit'
RUNNER = ROOT/'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--machine', required=True, choices=['m3', 'm4'])
    p.add_argument('--cases', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--prefix', required=True)
    p.add_argument('--env', action='append', default=[], metavar='KEY=VALUE',
                   help='Recorded diagnostic overrides forwarded to the guarded workload')
    p.add_argument('--batch-size', type=int, default=0,
                   help='Split a larger selection into separately guarded batches')
    a = p.parse_args()
    a.cases = a.cases.resolve()
    a.output = a.output.resolve()
    if a.batch_size:
        assert a.batch_size > 0
        a.output.mkdir(parents=True, exist_ok=False)
        meta = json.loads(a.cases.with_suffix('.meta.json').read_text())
        raw = a.cases.read_bytes()
        assert hashlib.sha256(raw).hexdigest() == meta['cases_sha256']
        assert raw.decode().splitlines() == [r['case'] for r in meta['cases']]
        counts = Counter()
        for start in range(0, len(meta['cases']), a.batch_size):
            if (a.output/'STOP').exists():
                print('Stopped between guarded batches; prior receipts retained', flush=True)
                return 2
            batch = dict(meta, cases=meta['cases'][start:start+a.batch_size])
            cases = a.output/('cases-%06d.txt' % start)
            cases.write_text(''.join(r['case']+'\n' for r in batch['cases']))
            batch['cases_sha256'] = hashlib.sha256(cases.read_bytes()).hexdigest()
            cases.with_suffix('.meta.json').write_text(json.dumps(batch)+'\n')
            out = a.output/(a.output.name+'-%06d' % start)
            cmd = [sys.executable, str(Path(__file__).resolve()), '--machine',
                   a.machine, '--cases', str(cases), '--output', str(out),
                   '--prefix', a.prefix]
            for value in a.env:
                cmd += ['--env', value]
            with (a.output/('batch-%06d.log' % start)).open('w') as log:
                rc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT).returncode
            if not (out/'summary.json').exists():
                return rc or 1
            summary = json.loads((out/'summary.json').read_text())
            counts.update(summary['counts'])
            failed = any(count and status not in ('Pass', 'NotSupported', 'QualityWarning')
                         for status, count in summary['counts'].items())
            combined = dict(healthy=not rc and summary['healthy'],
                            counts=counts, selected=len(meta['cases']),
                            completed_batches_end=start+len(batch['cases']),
                            stopped_on_test_failure=failed)
            (a.output/'summary.json').write_text(json.dumps(combined, indent=2)+'\n')
            print(json.dumps(combined), flush=True)
            if not combined['healthy'] or failed:
                return 1
        return 0
    lock = (ROOT/'build'/('cts-paired-'+a.machine+'.lock')).open('w')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    a.output.mkdir(parents=True, exist_ok=False)
    host = 'root@fedora-m3' if a.machine == 'm3' else 'root@omarchy-m4'
    env = dict(VK_DRIVER_FILES=a.prefix+'/asahi.json',
        VK_ICD_FILENAMES=a.prefix+'/asahi.json', LD_LIBRARY_PATH=a.prefix+'/lib',
        ASAHI_M3_EXPERIMENTAL='1', ASAHI_M4_EXPERIMENTAL='1',
        ASAHI_MESA_DEBUG='nocompress', HK_DEBUG_LOG='0', AGX_MESA_DEBUG='',
        VK_LAYER_PATH='/var/empty', VK_IMPLICIT_LAYER_PATH='/var/empty',
        MESA_SHADER_CACHE_DIR='/root/cts-regression-cache/mesa')
    for override in a.env:
        key, value = override.split('=', 1)
        assert key and '=' not in key
        env[key] = value
    (a.output/'environment.json').write_text(json.dumps(env, indent=2)+'\n')
    if a.machine == 'm4':
        cmd = [sys.executable, str(ROOT/'tools/cts-bulk.py'),
            '--cases', str(a.cases), '--output', str(a.output/'run'),
            '--name', a.output.name, '--runner', str(RUNNER), '--binary', BINARY,
            '--mesa-source', str(ROOT/'mesa/m3m4-merged'), '--jobs', '1',
            '--cpu-max-khz', '4104000', '--runner-seconds', '180',
            '--case-timeout', '60']
        for k, v in env.items():
            cmd += ['--env', k+'='+v]
        if '.wsi.' in a.cases.read_text():
            cmd += ['--headless']
        rc = subprocess.run(cmd).returncode
        if not (a.output/'run/summary.json').exists():
            return rc or 1
        summary = json.loads((a.output/'run/summary.json').read_text())
        summary['healthy'] = not rc and summary['guard_verdict'] == 'pass'
    else:
        # Different local evidence parents may use the same batch basename.
        # Never fetch an older run after the guard rejects a reused directory.
        identity = hashlib.sha256(str(a.output).encode()).hexdigest()[:12]
        base = '/root/cts-regression/'+a.output.name+'-'+identity
        ssh = ['ssh', '-oConnectTimeout=10', host]
        subprocess.run(ssh+[shlex.join(['mkdir', '-p', '/root/cts-regression'])], check=True)
        subprocess.run(ssh+[shlex.join(['mkdir', base])], check=True)
        subprocess.run(['scp', str(a.cases), str(a.cases.with_suffix('.meta.json')),
                        str(a.output/'environment.json'), host+':'+base+'/'], check=True)
        cmd = ['python3', '/root/m3-gpu-tools/gpu/guard.py',
            '--gpu-progress-seconds', '120', '--keep-cpu-clocks', base+'/run', '--',
            'python3', '/root/m3-gpu-tools/gpu/vk-cts-bulk.py', base+'/run',
            '--cases', base+'/'+a.cases.name, '--environment', base+'/environment.json',
            '--binary', BINARY, '--runner', '/root/cts-paired/deqp-runner',
            '--cache', '/root/cts-paired-cache/deqp']
        with (a.output/'controller.log').open('w') as log:
            rc = subprocess.run(ssh+[shlex.join(cmd)], stdout=log,
                                stderr=subprocess.STDOUT).returncode
        subprocess.run(['rsync', '-az', host+':'+base+'/run/',
                        str(a.output/'run')+'/'], check=True, timeout=180)
        out = a.output/'run'
        if not (out/'results.json').exists():
            return rc or 1
        rows = json.loads((out/'results.json').read_text())
        guard = json.loads((out/'result.json').read_text())
        # A guard may kill the runner on the first GPU fault before it writes
        # its final batch receipt. Keep partial outcomes and the guard failure.
        batch_path = out/'batch-result.json'
        batch = (json.loads(batch_path.read_text()) if batch_path.exists() else
                 dict(status='incomplete', reason='No batch receipt; inspect guard evidence'))
        summary = dict(healthy=not rc and guard['phase']=='complete' and
            batch['status']=='complete', guard=guard, batch=batch,
            counts=Counter(r['status'] for r in rows), selected=len(rows))
    (a.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(dict(healthy=summary['healthy'], counts=summary['counts'])))
    return 0 if summary['healthy'] else 1

if __name__ == '__main__':
    sys.exit(main())
