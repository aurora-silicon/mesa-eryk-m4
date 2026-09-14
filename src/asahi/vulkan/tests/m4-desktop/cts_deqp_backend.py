"""Bounded deqp-runner backend; invoked only by guarded cts-sample.py batch."""
import collections
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time


def kill_tree(proc):
    # Remain in the outer guard's process group. It can always terminate the
    # entire workload, including this wrapper, on thermal/progress failure.
    parents = [tuple(map(int, line.split())) for line in subprocess.check_output(
        ['ps', '-eo', 'pid=,ppid='], text=True).splitlines()]
    children = [proc.pid]
    for parent in children:
        children.extend(pid for pid, ppid in parents if ppid == parent)
    for pid in reversed(children):
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    proc.wait(timeout=3)


def run(args, check, boot, out, env, metadata, save):
    frozen = args.cases.read_text().splitlines()
    cases = frozen[args.start:args.start + args.count]
    if len(cases) != args.count or len(set(cases)) != len(cases):
        raise ValueError('Expected the exact requested number of unique cases')
    selected = out / 'runner-cases.txt'
    selected.write_text('\n'.join(cases) + '\n')
    result_dir = out / 'deqp-runner'
    argv = [str(args.deqp_runner), 'run', '--deqp', str(args.binary),
            '--caselist', str(selected), '--output', str(result_dir),
            '--jobs', str(args.jobs), '--tests-per-group', str(args.tests_per_group),
            '--timeout', str(args.case_timeout), '--first-attempt', '--no-deqp-shadercache',
            '--renderer-check', 'Apple M4' if args.renderer == 'm4' else 'llvmpipe|lavapipe',
            '--', '--deqp-log-images=' + ('enable' if args.log_images else 'disable'),
            '--deqp-log-shader-sources=' + ('enable' if args.log_sources else 'disable')]
    save(out / 'runner.json', dict(argv=argv, path=str(args.deqp_runner),
        sha256=hashlib.sha256(args.deqp_runner.read_bytes()).hexdigest(),
        tests_per_group=args.tests_per_group, deadline_seconds=args.runner_seconds,
        no_retries=True, no_baseline=True, no_skips=True,
        compatibility='Omit unsupported CTS 1.0 shadercache CLI flags; Mesa cache remains unchanged'))
    begin = time.monotonic()
    started = time.time()
    error = None
    with (out / 'runner.log').open('w') as log:
        proc = subprocess.Popen(argv, cwd=args.binary.parent, env=env,
                                stdout=log, stderr=subprocess.STDOUT)
        try:
            while proc.poll() is None:
                check()
                if time.monotonic() - begin > args.runner_seconds:
                    raise TimeoutError('deqp-runner workload deadline reached')
                time.sleep(0.2)
        except BaseException as exc:
            error = repr(exc)
            kill_tree(proc)
        log.flush()
        os.fsync(log.fileno())
    elapsed = time.monotonic() - begin
    results = collect(out, cases, metadata, args.start, boot)
    save(out / 'results.json', results)
    save(out / 'timing.json', dict(backend='deqp-runner', jobs=args.jobs,
        tests_per_group=args.tests_per_group, elapsed_seconds=elapsed,
        started_unix=started, finished_unix=time.time(), returncode=proc.returncode,
        error=error, completed=sum(r['status'] != 'Unrun' for r in results),
        case_timeout_seconds=args.case_timeout))
    counts = dict(collections.Counter(r['status'] for r in results))
    print(json.dumps(dict(counts=counts, runner_seconds=elapsed, error=error)), flush=True)
    check()
    if error or proc.returncode not in (0, 1) or any(r['status'] in ('Unrun', 'HarnessError', 'Crash', 'Timeout') for r in results):
        raise RuntimeError('Incomplete or fatal CTS run; evidence retained; do not automatically retry')
    print(f'CTS_BATCH_COMPLETE: {len(results)} first-attempt outcomes', flush=True)


def collect(out, cases, metadata, start, boot):
    """Offline recovery also works when a guard interrupted the wrapper."""
    result_dir = out / 'deqp-runner'
    # Preserve the original QPA status/message. deqp-runner folds several CTS
    # outcomes into Fail/Warn/Skip; keep both, and never equate Skip with Pass.
    qpa_records = {}
    for path in sorted(result_dir.glob('*.qpa')):
        if not re.search(r'\.c\d+\.r\d+\.qpa$|^c\d+\.r\d+\.qpa$', path.name):
            continue  # extracted failure copies are not additional attempts
        for name, content in re.findall(r'#beginTestCaseResult ([^\n]+)\n(.*?)#endTestCaseResult',
                                        path.read_text(errors='replace'), re.S):
            found = re.search(r'<Result StatusCode="([^"]+)"[^>]*>(.*?)</Result>', content, re.S)
            if found:
                if name in qpa_records:
                    raise RuntimeError(f'Duplicate attempt: {name}')
                qpa_records[name] = dict(status=found[1], message=found[2], qpa=str(path.relative_to(out)))
    csv_records = {}
    csv_path = result_dir / 'results.csv'
    if csv_path.exists():
        with csv_path.open() as stream:
            for row in csv.reader(stream):
                if not row or row[0].startswith('#'):
                    continue
                name, status, *duration = row
                if name not in cases or name in csv_records:
                    raise RuntimeError(f'Unexpected/duplicate runner result: {name}')
                csv_records[name] = (status, float(duration[0]) if duration else None)
    results = []
    for index, case in enumerate(cases, start):
        runner_status, duration = csv_records.get(case, ('Missing', None))
        record = dict(index=index, case=case, boot_id=boot, runner_status=runner_status,
                      seconds=duration, eligibility=(metadata['cases'][index] if metadata else
                      dict(case=case, classification='unclassified', rule='bare case list')))
        if case in qpa_records:
            record.update(qpa_records[case])
        else:
            record.update(status='Unrun', message='No completed QPA result')
        if runner_status in ('Crash', 'Timeout'):
            record.update(status=runner_status, message='Runner detected abnormal execution; inspect QPA/log')
        elif runner_status == 'Fail' and record['status'] == 'Pass':
            record.update(status='HarnessError', message='QPA Pass disagrees with runner failure')
        elif runner_status == 'Missing' and case in csv_records and record['status'] == 'Unrun':
            record.update(status='HarnessError', message='Runner could not obtain a result')
        results.append(record)
    return results
