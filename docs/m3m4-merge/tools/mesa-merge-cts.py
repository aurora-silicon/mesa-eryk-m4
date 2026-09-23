#!/usr/bin/env python3
"""Freeze prior repaired indices, or run that selection on M4 with its guard.

M3 uses tools/m3-gpu/gl-cts-focused.py with the generated selection. M4 uses
cts-bulk.py; the exact cases and configurations are retained on both targets.
No retry or continuation after an unexpected result is automatic.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def save(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['prepare', 'run-m4'])
    p.add_argument('--campaign', type=Path)
    p.add_argument('--selection', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--environment', type=Path)
    p.add_argument('--mesa-source', type=Path, default=ROOT/'mesa/m3m4-merged')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    if a.mode == 'prepare':
        campaign = a.campaign.resolve()
        wanted = {r['global_index'] for r in json.loads((campaign/'repairs.json').read_text())}
        covered = set()
        parts = []
        for source in sorted((campaign/'segments').glob('*/cases.meta.json')):
            data = json.loads(source.read_text())
            assert digest(source.with_name('cases.txt').read_bytes()) == data['cases_sha256']
            rows = [r for r in data['cases'] if r['catalog_index'] in wanted]
            for start in range(0, len(rows), 16):
                group = rows[start:start+16]
                folder = a.output/f'{len(parts):04d}'
                folder.mkdir()
                raw = ('\n'.join(r['case'] for r in group)+'\n').encode()
                (folder/'cases.txt').write_bytes(raw)
                save(folder/'cases.meta.json', dict(schema=1, cases=group,
                                                   provenance=data.get('provenance', {}),
                                                   cases_sha256=digest(raw)))
                (folder/'options.json').write_bytes(source.with_name('options.json').read_bytes())
                covered.update(r['catalog_index'] for r in group)
                parts.append(folder.name)
        assert covered == wanted and parts
        save(a.output/'focused.json', dict(campaign=str(campaign), parts=parts,
             pattern='exact prior repaired global indices', session=None,
             catalog_sha256=digest((campaign/'catalog.json').read_bytes())))
        print(len(covered), 'indexed cases in', len(parts), 'bounded parts')
        return

    plan = json.loads((a.selection/'focused.json').read_text())
    env = json.loads(a.environment.read_text())
    save(a.output/'selection.json', plan)
    for name in plan['parts']:
        part = a.selection/name
        cases = (part/'cases.txt').read_text().splitlines()
        options = json.loads((part/'options.json').read_text())
        defaults = ['--deqp-gl-context-type=egl', '--deqp-egl-display-type=x11',
                    '--deqp-log-flush=enable']
        supplied = {x.split('=',1)[0] for x in options}
        options += [x for x in defaults if x.split('=',1)[0] not in supplied]
        folder = a.output/name
        cmd = [sys.executable, str(ROOT/'tools/cts-bulk.py'), '--cases', str(part/'cases.txt'),
               '--output', str(folder), '--name', a.output.name+'-'+name,
               '--mesa-source', str(a.mesa_source), '--binary', '/root/gl-cts-1.4.6.2/glcts',
               '--runner', str(ROOT/'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner'),
               '--headless', '--jobs', '1' if any(c.startswith('dEQP-EGL.') for c in cases) else '4',
               '--runner-seconds', '90', '--case-timeout', '60', '--tests-per-group', '4',
               '--log-images', '--log-sources']
        for k,v in env.items():
            cmd += ['--env', k+'='+v]
        for x in options:
            cmd += ['--deqp-arg='+x]
        for k in ['MESA_GL_VERSION_OVERRIDE', 'MESA_GLES_VERSION_OVERRIDE', 'MESA_GLSL_VERSION_OVERRIDE']:
            cmd += ['--unset-env', k]
        result = subprocess.run(cmd, cwd=ROOT)
        assert result.returncode == 0, 'Guard/process failure; inspect evidence'
        summary = json.loads((folder/'summary.json').read_text())
        print(json.dumps(dict(part=name, counts=summary['counts'])), flush=True)
        assert set(summary['counts']) <= {'Pass','NotSupported'}, 'Unexpected result; triage before continuing'

if __name__ == '__main__':
    main()
