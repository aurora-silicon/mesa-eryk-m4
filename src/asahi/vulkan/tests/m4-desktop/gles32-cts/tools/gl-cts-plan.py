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
import re
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
p.add_argument('mode', choices=['freeze', 'run', 'report', 'status'])
p.add_argument('--campaign', type=Path, required=True)
p.add_argument('--plan', type=Path)
p.add_argument('--data', type=Path)
p.add_argument('--environment', type=Path)
p.add_argument('--binary', default='/root/gl-cts-1.4.6.2/glcts')
p.add_argument('--segment-size', type=int, default=2500)
p.add_argument('--first', type=int, default=0)
p.add_argument('--last', type=int)
p.add_argument('--jobs', type=int, default=4)
p.add_argument('--run-jobs', type=int, choices=(1,4), help='Explicit execution worker cap; frozen cases/configuration indices are unchanged')
p.add_argument('--live', action='store_true', help='Read the latest target runner progress alongside saved coverage')
p.add_argument('--seconds', type=int, default=900)
p.add_argument('--continue-compiler-aborts', action='store_true',
    help='After the reviewed AGX compile/pipeline assertion only, retain crashes and continue unrun/canceled peers under a fresh guard')
p.add_argument('--continue-batch-deadlines', action='store_true',
    help='Resume only unfinished cases after a verified workload deadline, preserving all completed outcomes and the original bounds')
p.add_argument('--continue-lod-bias-timeouts', action='store_true',
    help='Retain reviewed KHR-GLES3 texture_lod_bias_all timeouts and resume only canceled/unrun peers')
p.add_argument('--continue-reviewed-timeout', action='append', default=[], metavar='EXACT_CASE',
    help='Explicitly reviewed per-case timeouts to retain while resuming canceled/unrun peers')
p.add_argument('--recovery-review', type=Path, help='Exact manually reviewed GPU incident with successful guarded post-reboot probe')
a = p.parse_args()
recovery_review = json.loads(a.recovery_review.read_text()) if a.recovery_review else None
if a.continue_lod_bias_timeouts:
    a.continue_reviewed_timeout.append('KHR-GLES3.texture_lod_bias.texture_lod_bias_all')
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
def attempt_paths(folder):
    return ([folder/'run'] if (folder/'run/summary.json').exists() else []) + sorted(p for p in folder.glob('continuations/*/run') if (p/'summary.json').exists())

if a.mode in ('report', 'status'):
    attempts, latest, runs = [], {}, []
    elapsed = 0
    for segment in catalog['segments']:
        folder = a.campaign/'segments'/f"{segment['index']:04d}"
        for run in attempt_paths(folder):
            summary = json.loads((run/'summary.json').read_text())
            runs.append(dict(segment=segment['index'], directory=str(run), **summary))
            elapsed += summary['elapsed_seconds']
            review_file = run/'continuation-review.json'
            canceled = set(json.loads(review_file.read_text())['pending']) if review_file.exists() else set()
            for item in json.loads((run/'results.json').read_text()):
                record = dict(**item, segment=segment['index'], session=segment['session'],
                    global_index=item['eligibility']['catalog_index'], run=str(run))
                if record['status'] != 'Unrun':
                    attempts.append(record)
                if record['case'] in canceled and record['status'] == 'Crash' and 'SIGKILL' in record.get('worker_exit',''):
                    record = dict(record, status='Interrupted', original_status='Crash',
                        disposition='Peer canceled after reviewed userspace abort; original attempt retained')
                idx = record['global_index']
                if idx not in latest or record['status'] not in ('Unrun','Interrupted'):
                    latest[idx] = record
    records = sorted(latest.values(), key=lambda r:r['global_index'])
    counts = Counter(r['status'] for r in records)
    counts['Unrun'] += catalog['total'] - len(records)
    completed = sum(n for key,n in counts.items() if key not in ('Unrun','Interrupted'))
    summary = dict(total=catalog['total'], completed=completed, counts=dict(counts),
        attempts=len(attempts), elapsed_seconds=round(elapsed,2),
        cases_per_second=round(completed/elapsed,2) if elapsed else None)
    if a.mode == 'report':
        save(a.campaign/'results.json', records)
        save(a.campaign/'attempts.json', attempts)
        save(a.campaign/'summary.json', dict(**summary, runs=runs))
        save(a.campaign/'incidents.json', [r for r in runs if r['guard_verdict'] not in ('pass','process_failure')])
        bad = [r for r in records if r['status'] not in ('Pass','NotSupported','CompatibilityWarning','QualityWarning','Unrun','Interrupted')]
        save(a.campaign/'failures.json', bad)
        grouped = []
        for session in catalog['sessions']:
            selected = [r for r in records if r['session'] == session['index']]
            statuses = Counter(r['status'] for r in selected)
            statuses['Unrun'] += session['count'] - len(selected)
            grouped.append(dict(session=session['index'], name=session['name'],
                total=session['count'], options=session['options'], counts=dict(statuses)))
        save(a.campaign/'sessions-summary.json', grouped)
        by_case = {}
        for record in bad:
            entry = by_case.setdefault(record['case'], dict(case=record['case'], occurrences=[]))
            entry['occurrences'].append(dict(global_index=record['global_index'],
                session=record['session'], status=record['status'], run=record['run']))
        save(a.campaign/'unique-failures.json', list(by_case.values()))
        (a.campaign/'failures.tsv').write_text('global_index\tsession\tstatus\tcase\n'+''.join(
            f"{r['global_index']}\t{r['session']}\t{r['status']}\t{r['case']}\n" for r in bad))
    if a.live:
        probe = subprocess.run(['ssh', '-o', 'ConnectTimeout=10', 'root@omarchy-m4', 'python3', '-'],
            input="""from pathlib import Path
import json
paths = sorted(Path('/root/asahi-evidence').glob('*-gles32-plan-*'))
if paths:
    p = paths[-1]
    log = p/'cts/runner.log'
    progress = []
    if log.exists():
        with log.open('rb') as f:
            f.seek(max(0, log.stat().st_size-500000))
            progress = [x for x in f.read().decode(errors='replace').splitlines() if 'Remaining:' in x]
    print(json.dumps(dict(evidence=p.name, progress=progress[-1:])))
""", text=True, capture_output=True, timeout=20)
        summary['live'] = json.loads(probe.stdout) if probe.returncode == 0 and probe.stdout.strip() else dict(error=probe.stderr.strip())
    print(json.dumps(summary, indent=2))
    sys.exit(0)

def reviewed_pending(run):
    summary = json.loads((run/'summary.json').read_text())
    rows = json.loads((run/'results.json').read_text())
    if summary['guard_verdict'] == 'pass':
        assert all(r['status'] not in ('Unrun','Interrupted','Crash','Timeout','HarnessError') for r in rows)
        return []
    if recovery_review and summary['guard_verdict'] == 'gpu_timeout' and summary['evidence'] == recovery_review['incident_evidence']:
        probe_run = Path(recovery_review['recovery_run'])
        probe = json.loads((probe_run/'summary.json').read_text())
        probe_rows = json.loads((probe_run/'results.json').read_text())
        old = json.loads((Path(summary['evidence'])/'target-before.json').read_text())
        new = json.loads((Path(probe['evidence'])/'target-before.json').read_text())
        assert probe['guard_verdict'] == 'pass' and probe['jobs'] == 1
        assert old['boot_id'] != new['boot_id'] == recovery_review['new_boot']
        assert old['kernel'] == new['kernel']
        assert all(old['files'][k] == new['files'][k] for k in old['files'].keys() & new['files'].keys())
        assert {r['case'] for r in probe_rows} == {r['case'] for r in rows if r['status'] == 'Interrupted'}
        assert all(r['status'] == 'Pass' for r in probe_rows)
        assert all(r['status'] in ('Pass','Fail','NotSupported','QualityWarning','CompatibilityWarning','Unrun','Interrupted') for r in rows)
        pending = [r for r in rows if r['status'] in ('Unrun','Interrupted')]
        save(run/'continuation-review.json', dict(reason=recovery_review['reason'],
            recovery_review=recovery_review, terminal_crashes=[], pending=[r['case'] for r in pending]))
        return pending
    assert (a.continue_compiler_aborts or a.continue_batch_deadlines or a.continue_reviewed_timeout) and summary['guard_verdict'] == 'process_failure', 'Needs explicit review'
    evidence = Path(summary['evidence'])/'remote'
    memory = [json.loads(line) for line in (evidence/'cts/memory.jsonl').read_text().splitlines()]
    assert memory and all(m['meminfo_kib']['MemAvailable'] > 1536*1024 for m in memory)
    assert len({m['vmstat']['oom_kill'] for m in memory}) == 1
    assert not (evidence/'cts/memory-pressure.json').exists()
    timing_file = evidence/'cts/timing.json'
    timing = json.loads(timing_file.read_text()) if timing_file.exists() else {}
    if a.continue_batch_deadlines and timing.get('error') == "TimeoutError('deqp-runner workload deadline reached')":
        runner = json.loads((evidence/'cts/runner.json').read_text())
        assert timing['returncode'] == -9
        assert runner['deadline_seconds'] <= timing['elapsed_seconds'] < runner['deadline_seconds'] + 5
        assert all(r['status'] in ('Pass','Fail','NotSupported','QualityWarning','CompatibilityWarning','Unrun','Interrupted') for r in rows)
        assert any(r['status'] == 'Pass' for r in rows), 'No demonstrated progress'
        assert sum(r['status'] == 'Interrupted' for r in rows) <= summary['jobs']
        pending = [r for r in rows if r['status'] in ('Unrun','Interrupted')]
        save(run/'continuation-review.json', dict(reason='Verified workload deadline with completed test progress, no per-case timeout/crash, GPU/display/thermal fault, OOM or memory-floor crossing. Preserve completed results and resume only unrun/interrupted cases under unchanged bounds.',
            terminal_crashes=[], pending=[r['case'] for r in pending], timing=timing))
        return pending
    timeouts = [r for r in rows if r['status'] == 'Timeout']
    if a.continue_reviewed_timeout and timeouts:
        assert timing.get('error') is None and timing.get('returncode') == 1
        assert all(r['case'] in a.continue_reviewed_timeout and
                   summary['case_timeout_seconds'] <= r['seconds'] < summary['case_timeout_seconds'] + 5
                   for r in timeouts), 'Unreviewed timeout'
        assert all(r['status'] not in ('HarnessError','InternalError','ResourceError') for r in rows)
        crash = [r for r in rows if r['status'] == 'Crash']
        assert all('SIGKILL' in r.get('worker_exit','') and 'coordinated stop' in r.get('message','') for r in crash)
        pending = [r for r in rows if r['status'] in ('Unrun','Interrupted','Crash')]
        save(run/'continuation-review.json', dict(reason='Explicitly reviewed case exceeded the unchanged per-case time limit. No GPU/display/thermal fault, OOM or memory-floor crossing. Retain timeout; resume only unfinished and SIGKILL-canceled peers. A fresh guarded renderer preflight precedes continuation.',
            terminal_crashes=[], terminal_timeouts=[r['case'] for r in timeouts],
            pending=[r['case'] for r in pending]))
        return pending
    assert a.continue_compiler_aborts, 'Not a reviewed batch deadline or per-case timeout'
    crash = [r for r in rows if r['status'] == 'Crash']
    assert crash and any('SIGABRT' in r.get('worker_exit','') for r in crash)
    for r in rows:
        assert r['status'] not in ('Timeout','HarnessError','InternalError','ResourceError'), r
    for r in crash:
        exit_status = r.get('worker_exit','')
        if 'SIGABRT' in exit_status:
            matches = [f.read_text(errors='replace') for f in (evidence/'cts/deqp-runner').glob('*.r1.log')
                       if "Test case '"+r['case']+"'" in f.read_text(errors='replace')]
            assert len(matches) == 1, r
            log = matches[0]
            assert 'AGX backend rejected' in log and "Assertion `pipeline' failed" in log, r
        else:
            assert 'SIGKILL' in exit_status and 'coordinated stop' in r.get('message',''), r
    pending = [r for r in rows if r['status'] in ('Unrun','Interrupted') or
               (r['status']=='Crash' and 'SIGKILL' in r.get('worker_exit',''))]
    save(run/'continuation-review.json', dict(reason='Known AGX compiler rejection followed by Zink pipeline assertion. Guard found no GPU/display/thermal fault; no OOM or memory-floor crossing. Keep SIGABRT cases terminal; resume only unrun/interrupted and identified canceled SIGKILL peers.',
        terminal_crashes=[r['case'] for r in crash if 'SIGABRT' in r.get('worker_exit','')],
        pending=[r['case'] for r in pending]))
    return pending

env = json.loads((a.campaign/'environment.json').read_text())
for segment in catalog['segments'][a.first:a.last]:
    base = a.campaign/'segments'/f"{segment['index']:04d}"
    assert digest((base/'cases.txt').read_bytes()) == segment['sha256']
    paths = attempt_paths(base)
    if paths:
        assert a.continue_compiler_aborts or a.continue_batch_deadlines or a.continue_reviewed_timeout, 'Refusing to overwrite an attempt'
        pending = reviewed_pending(paths[-1])
        if not pending:
            continue
    else:
        assert not (base/'run').exists(), 'Incomplete setup needs review'
        pending = None
    while True:
        ordinal = len(attempt_paths(base))
        if pending is None:
            folder = base
        else:
            folder = base/'continuations'/f'{ordinal:04d}'
            folder.mkdir(parents=True, exist_ok=False)
            raw = ('\n'.join(r['case'] for r in pending)+'\n').encode()
            (folder/'cases.txt').write_bytes(raw)
            meta = json.loads((base/'cases.meta.json').read_text())
            meta.update(cases_sha256=digest(raw), cases=[r['eligibility'] for r in pending])
            save(folder/'cases.meta.json', meta)
        options = catalog['sessions'][segment['session']]['options']
        cmd = [sys.executable, str(ROOT/'tools/cts-bulk.py'), '--cases', str(folder/'cases.txt'),
            '--output', str(folder/'run'), '--name', f"gles32-plan-{segment['index']:04d}-r{ordinal}",
            '--binary', catalog['binary'], '--runner', str(ROOT/'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner'),
            '--jobs', str(1 if recovery_review and segment['index'] == recovery_review['segment'] else min(segment['jobs'], a.run_jobs or segment['jobs'])), '--runner-seconds', str(a.seconds),
            '--case-timeout', '60', '--cpu-max-khz', '4104000', '--headless']
        for opt in options + ['--deqp-egl-display-type=x11', '--deqp-log-flush=enable']:
            cmd += ['--deqp-arg='+opt]
        for key in ('MESA_GL_VERSION_OVERRIDE','MESA_GLES_VERSION_OVERRIDE','MESA_GLSL_VERSION_OVERRIDE'):
            cmd += ['--unset-env',key]
        for key,value in env.items():
            if key.startswith(('GALLIUM','MESA','VK_','__EGL','LD_LIBRARY','LIBGL','ASAHI','AGX_','HK_')):
                cmd += ['--env',key+'='+value]
        print(f"START segment {segment['index']} attempt {ordinal}, cases={len(pending) if pending is not None else segment['count']}", flush=True)
        rc = subprocess.run(cmd, cwd=ROOT).returncode
        if not rc:
            break
        pending = reviewed_pending(folder/'run')
        if not pending:
            break
print('SELECTED_SEGMENTS_COMPLETE', flush=True)
