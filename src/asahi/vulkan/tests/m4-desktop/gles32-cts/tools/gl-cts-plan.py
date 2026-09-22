#!/usr/bin/env python3
"""Freeze/run/report a Khronos cts-runner XML plan through bounded CTS guards.

Configuration/case pairs retain global indices. Each run stops on an incomplete
or fatal segment; ordinary failed comparisons are recorded and do not stop it.
No retries, driver changes, result filtering, or guard bypasses are performed.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')
def digest(raw):
    return hashlib.sha256(raw).hexdigest()

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('mode', choices=['freeze', 'run', 'report'])
p.add_argument('--campaign', type=Path, required=True)
p.add_argument('--plan', type=Path)
p.add_argument('--data', type=Path)
p.add_argument('--environment', type=Path)
p.add_argument('--binary', default='/root/gl-cts-1.4.6.2/glcts')
p.add_argument('--segment-size', type=int, default=2500)
p.add_argument('--first', type=int, default=0)
p.add_argument('--last', type=int)
p.add_argument('--jobs', type=int, default=4)
p.add_argument('--seconds', type=int, default=900)
a = p.parse_args()
if a.mode == 'freeze':
    assert a.plan and a.data and a.environment and a.segment_size > 0
    a.campaign.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(a.plan, a.campaign/'plan.xml')
    shutil.copyfile(a.environment, a.campaign/'environment.json')
    sessions, segments, global_index = [], [], 0
    for session, element in enumerate(ET.parse(a.plan).findall('TestRun')):
        args = shlex.split(element.attrib['CmdLine'])
        source = next(x.split('=', 1)[1] for x in args if x.startswith('--deqp-caselist-file='))
        raw = (a.data/source).read_bytes()
        cases = raw.decode().splitlines()
        assert cases and len(cases) == len(set(cases))
        target = a.campaign/'sessions'/f'{session:03d}'
        target.mkdir(parents=True)
        (target/'cases.txt').write_bytes(raw)
        options = [x for x in args if not x.startswith('--deqp-caselist-file=')]
        sessions.append(dict(index=session, name=element.attrib['FileName'],
            source=source, sha256=digest(raw), count=len(cases), start=global_index, options=options))
        for start in range(0, len(cases), a.segment_size):
            number = len(segments)
            folder = a.campaign/'segments'/f'{number:04d}'
            folder.mkdir(parents=True)
            selected = cases[start:start+a.segment_size]
            contents = ('\n'.join(selected)+'\n').encode()
            (folder/'cases.txt').write_bytes(contents)
            meta = dict(schema=1, cases_sha256=digest(contents),
                provenance=dict(plan_sha256=digest(a.plan.read_bytes())),
                cases=[dict(case=case, catalog_index=global_index+start+i,
                    session=session, session_index=start+i,
                    classification='selected', rule='Frozen Khronos ES32 runner plan')
                    for i, case in enumerate(selected)])
            save(folder/'cases.meta.json', meta)
            segments.append(dict(index=number, session=session, start=global_index+start,
                count=len(selected), sha256=digest(contents),
                jobs=1 if any(x.startswith('dEQP-EGL.') for x in selected) else a.jobs))
        global_index += len(cases)
    save(a.campaign/'catalog.json', dict(plan_sha256=digest(a.plan.read_bytes()),
        binary=a.binary, total=global_index, sessions=sessions, segments=segments,
        note='Indices identify configuration/case pairs; repeated cases in other EGL configurations are distinct.'))
    print(f'FROZEN: {global_index} configuration/case pairs, {len(sessions)} sessions, {len(segments)} segments')
    sys.exit(0)

catalog = json.loads((a.campaign/'catalog.json').read_text())
assert digest((a.campaign/'plan.xml').read_bytes()) == catalog['plan_sha256']
if a.mode == 'report':
    records, runs = [], []
    for segment in catalog['segments']:
        folder = a.campaign/'segments'/f"{segment['index']:04d}"
        if not (folder/'run/summary.json').exists():
            continue
        runs.append(dict(segment=segment['index'], **json.loads((folder/'run/summary.json').read_text())))
        for item in json.loads((folder/'run/results.json').read_text()):
            records.append(dict(**item, segment=segment['index'],
                session=segment['session'], global_index=item['eligibility']['catalog_index']))
    counts = Counter(r['status'] for r in records)
    counts['Unrun'] += catalog['total'] - len(records)
    save(a.campaign/'results.json', records)
    save(a.campaign/'summary.json', dict(total=catalog['total'], counts=dict(counts), runs=runs))
    bad = [r for r in records if r['status'] not in ('Pass','NotSupported','CompatibilityWarning','QualityWarning','Unrun','Interrupted')]
    save(a.campaign/'failures.json', bad)
    (a.campaign/'failures.tsv').write_text('global_index\tsession\tstatus\tcase\n'+''.join(
        f"{r['global_index']}\t{r['session']}\t{r['status']}\t{r['case']}\n" for r in bad))
    print(json.dumps(dict(total=catalog['total'], counts=dict(counts)), indent=2))
    sys.exit(0)

env = json.loads((a.campaign/'environment.json').read_text())
for segment in catalog['segments'][a.first:a.last]:
    folder = a.campaign/'segments'/f"{segment['index']:04d}"
    assert digest((folder/'cases.txt').read_bytes()) == segment['sha256']
    assert not (folder/'run').exists(), 'Refusing to overwrite an attempt'
    options = catalog['sessions'][segment['session']]['options']
    cmd = [sys.executable, str(ROOT/'tools/cts-bulk.py'), '--cases', str(folder/'cases.txt'),
        '--output', str(folder/'run'), '--name', f"gles32-plan-{segment['index']:04d}",
        '--binary', catalog['binary'], '--runner', str(ROOT/'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner'),
        '--jobs', str(segment['jobs']), '--runner-seconds', str(a.seconds),
        '--case-timeout', '60', '--cpu-max-khz', '4104000', '--headless']
    # EGL config IDs in this plan were enumerated on the private Xwayland.
    for opt in options + ['--deqp-egl-display-type=x11', '--deqp-log-flush=enable']:
        cmd += ['--deqp-arg='+opt]
    for key in ('MESA_GL_VERSION_OVERRIDE','MESA_GLES_VERSION_OVERRIDE','MESA_GLSL_VERSION_OVERRIDE'):
        cmd += ['--unset-env',key]
    for key,value in env.items():
        if key.startswith(('GALLIUM','MESA','VK_','__EGL','LD_LIBRARY','LIBGL','ASAHI','AGX_','HK_')):
            cmd += ['--env',key+'='+value]
    print(f"START segment {segment['index']} [{segment['start']},{segment['start']+segment['count']})", flush=True)
    rc = subprocess.run(cmd, cwd=ROOT).returncode
    if rc:
        sys.exit(f"STOP segment {segment['index']}: inspect retained evidence before continuing")
print('SELECTED_SEGMENTS_COMPLETE', flush=True)
