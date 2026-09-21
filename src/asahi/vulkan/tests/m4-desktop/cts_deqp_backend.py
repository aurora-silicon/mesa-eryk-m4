"""Bounded deqp-runner backend; invoked only by guarded cts-sample.py batch."""
import collections
import csv
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
from cts_memory_policy import active_qpa_cases


def memory_sample(diagnostic=False):
    """Read-only context for allocation failures; never renews the watchdog."""
    sample = {'monotonic_seconds': time.monotonic()}
    try:
        sample['meminfo_kib'] = {
            name.rstrip(':'): int(value)
            for name, value, *unit in
            (line.split() for line in Path('/proc/meminfo').read_text().splitlines())
            if unit == ['kB']
        }
        sample['vmstat'] = {
            name: int(value)
            for name, value in
            (line.split() for line in Path('/proc/vmstat').read_text().splitlines())
            if name == 'oom_kill' or name.startswith(('allocstall', 'pgscan_direct'))
        }
        # PSI is optional in the target kernel; retain the core counters
        # even when this file is unavailable.
        pressure = Path('/proc/pressure/memory')
        if pressure.exists():
            sample['pressure'] = pressure.read_text().strip()
    except OSError as exc:
        sample['read_error'] = str(exc)
    if diagnostic:
        sample['clients'] = []
        sample['kernel_workers'] = []
        for proc in Path('/proc').glob('[0-9]*'):
            try:
                comm = (proc / 'comm').read_text().strip()
                if comm.startswith('kworker'):
                    stack = (proc / 'stack').read_text()
                    if any(name in stack for name in ('asahi', 'fput', 'drm_', 'shmem')):
                        sample['kernel_workers'].append(dict(pid=int(proc.name), comm=comm, stack=stack))
                command = (proc / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
                if not any(name in command.split(' ', 1)[0] for name in ('deqp', 'Hyprland')):
                    continue
                client = dict(pid=int(proc.name), command=command,
                              status=(proc / 'status').read_text(),
                              smaps_rollup=(proc / 'smaps_rollup').read_text(), fds=[])
                for fd in (proc / 'fd').iterdir():
                    try:
                        target = os.readlink(fd)
                        if '/dev/dri/' in target or 'memfd:' in target or '/dev/shm/' in target:
                            client['fds'].append(dict(fd=fd.name, target=target,
                                info=(proc / 'fdinfo' / fd.name).read_text()))
                    except OSError:
                        pass  # A CTS child may exit between reads.
                sample['clients'].append(client)
            except OSError:
                pass
        try:
            sample['drm_clients'] = Path('/sys/kernel/debug/dri/128/clients').read_text()
        except OSError as exc:
            sample['drm_clients_error'] = str(exc)
    return sample


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
    if args.memory_floor_mib < 512:
        raise ValueError('Memory floor must leave at least 512 MiB for guards and recovery')
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
            '--timeout', str(args.case_timeout), '--first-attempt',
            '--renderer-check', 'Apple M4' if args.renderer == 'm4' else 'llvmpipe|lavapipe',
            '--', '--deqp-log-images=' + ('enable' if args.log_images else 'disable'),
            '--deqp-log-shader-sources=' + ('enable' if args.log_sources else 'disable')]
    argv += getattr(args, 'deqp_arg', [])
    if args.legacy_shader_cache:
        argv.insert(argv.index('--'), '--no-deqp-shadercache')
    cache_lock = None
    if args.shader_cache_dir:
        args.shader_cache_dir.mkdir(parents=True, exist_ok=True)
        cache_lock = (args.shader_cache_dir / 'runner.lock').open('a')
        fcntl.flock(cache_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        pos = argv.index('--')
        argv[pos:pos] = ['--shader-cache-dir', str(args.shader_cache_dir)]
    if args.cpu_affinity:
        argv = ['taskset', '--cpu-list', args.cpu_affinity] + argv
    save(out / 'runner.json', dict(argv=argv, path=str(args.deqp_runner),
        sha256=hashlib.sha256(args.deqp_runner.read_bytes()).hexdigest(),
        tests_per_group=args.tests_per_group, deadline_seconds=args.runner_seconds,
        case_timeout_seconds=args.case_timeout,
        no_retries=True, no_baseline=True, no_skips=True,
        memory_floor_mib=args.memory_floor_mib,
        compatibility=('Legacy CTS: cache arguments omitted' if args.legacy_shader_cache else
                       'Private per-worker CTS shader caches; truncation disabled; Mesa cache unchanged')))
    begin = time.monotonic()
    started = time.time()
    error = None
    pressure = None
    with (out / 'runner.log').open('w') as log, \
         (out / 'memory.jsonl').open('w') as memory:
        proc = subprocess.Popen(argv, cwd=args.binary.parent, env=env,
                                stdout=log, stderr=subprocess.STDOUT)
        next_memory = begin
        try:
            while proc.poll() is None:
                check()
                if time.monotonic() >= next_memory:
                    sample = memory_sample(env.get('M4_CTS_MEMORY_DIAGNOSTIC') == '1')
                    memory.write(json.dumps(sample) + '\n')
                    memory.flush()
                    os.fsync(memory.fileno())
                    available = sample.get('meminfo_kib', {}).get('MemAvailable')
                    if available is None or available < args.memory_floor_mib * 1024:
                        pressure = dict(sample=sample, floor_mib=args.memory_floor_mib,
                                        active_cases=active_qpa_cases(result_dir))
                        raise RuntimeError(f'CTS memory floor reached: available_kib={available}, '
                                           f'floor_mib={args.memory_floor_mib}; inspect partial evidence')
                    next_memory = time.monotonic() + 0.5
                if time.monotonic() - begin > args.runner_seconds:
                    raise TimeoutError('deqp-runner workload deadline reached')
                time.sleep(0.2)
        except BaseException as exc:
            error = repr(exc)
            kill_tree(proc)
        log.flush()
        os.fsync(log.fileno())
        memory.write(json.dumps(memory_sample(env.get('M4_CTS_MEMORY_DIAGNOSTIC') == '1')) + '\n')
        memory.flush()
        os.fsync(memory.fileno())
    elapsed = time.monotonic() - begin
    if pressure is not None:
        save(out / 'memory-pressure.json', pressure)
    results = collect(out, cases, metadata, args.start, boot)
    save(out / 'results.json', results)
    save(out / 'timing.json', dict(backend='deqp-runner', jobs=args.jobs,
        tests_per_group=args.tests_per_group, elapsed_seconds=elapsed,
        started_unix=started, finished_unix=time.time(), returncode=proc.returncode,
        error=error, completed=sum(r['status'] not in ('Unrun', 'Interrupted') for r in results),
        case_timeout_seconds=args.case_timeout))
    counts = dict(collections.Counter(r['status'] for r in results))
    print(json.dumps(dict(counts=counts, runner_seconds=elapsed, error=error)), flush=True)
    check()
    if error or proc.returncode not in (0, 1) or any(r['status'] in ('Unrun', 'Interrupted', 'HarnessError', 'Crash', 'Timeout') for r in results):
        raise RuntimeError('Incomplete or fatal CTS run; evidence retained; do not automatically retry')
    print(f'CTS_BATCH_COMPLETE: {len(results)} first-attempt outcomes', flush=True)


def collect(out, cases, metadata, start, boot):
    """Offline recovery also works when a guard interrupted the wrapper."""
    allowed_cases = set(cases)
    result_dir = out / 'deqp-runner'
    runner = json.loads((out / 'runner.json').read_text())
    case_timeout = runner.get('case_timeout_seconds')
    if case_timeout is None and '--timeout' in runner.get('argv', []):
        case_timeout = float(runner['argv'][runner['argv'].index('--timeout') + 1])
    # Preserve the original QPA status/message. deqp-runner folds several CTS
    # outcomes into Fail/Warn/Skip; keep both, and never equate Skip with Pass.
    qpa_records = {}
    started_cases = set()
    group_cases = {}
    coordinated_stop = (out / 'runner.log').exists() and 'deqp-runner max-fails exceeded' in (out / 'runner.log').read_text(errors='replace')
    for log in result_dir.glob('*.log'):
        if not re.search(r'\.c\d+\.r\d+\.log$|^c\d+\.r\d+\.log$', log.name):
            continue
        content = log.read_text(errors='replace')
        death = re.search(r'^exit status: (.+)$', content, re.M)
        for name in re.findall(r"Test case '([^']+)'\.\.", content):
            started_cases.add(name)
            group_cases[name] = death[1] if death else 'unknown'
    for path in sorted(result_dir.glob('*.qpa')):
        if not re.search(r'\.c\d+\.r\d+\.qpa$|^c\d+\.r\d+\.qpa$', path.name):
            continue  # extracted failure copies are not additional attempts
        qpa_text = path.read_text(errors='replace')
        started_cases.update(re.findall(r'^#beginTestCaseResult (.+)$', qpa_text, re.M))
        for name, content in re.findall(r'#beginTestCaseResult ([^\n]+)\n(.*?)#endTestCaseResult',
                                        qpa_text, re.S):
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
                if name not in allowed_cases or name in csv_records:
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
        termination = group_cases.get(case)
        if termination:
            record['worker_exit'] = termination
        if runner_status in ('Crash', 'Timeout'):
            record.update(status=runner_status, message='Runner detected abnormal execution; inspect QPA/log')
            if runner_status == 'Crash' and coordinated_stop and termination == 'signal: 9 (SIGKILL)':
                # A global stop marker cannot distinguish the triggering OOM
                # kill from canceled peers. Keep the raw crash, with context;
                # otherwise the first crash is silently retried as unfinished.
                record['message'] += '; SIGKILL during coordinated stop (OOM or canceled peer; attribution unresolved)'
        elif runner_status == 'Fail' and record['status'] == 'Pass':
            record.update(status='HarnessError', message='QPA Pass disagrees with runner failure')
        elif runner_status == 'Missing' and coordinated_stop and record['status'] == 'Unrun':
            # Groups canceled before launch are included as Missing in the
            # runner CSV. Without a CTS start marker they were never run.
            # Preserve the CSV status, but do not invent a harness failure.
            record['message'] = 'No completed result before coordinated runner stop'
        elif runner_status == 'Missing' and case in csv_records and record['status'] == 'Unrun':
            record.update(status='HarnessError', message='Runner could not obtain a result')
        if record['status'] == 'Unrun' and case in started_cases:
            record.update(status='Interrupted', message='CTS started the case but did not produce a complete result')
        # A completed QPA Pass must not hide a missed execution deadline.
        # Keep the CTS result as evidence while recording the timeout failure.
        if case_timeout is not None and duration is not None and duration >= case_timeout:
            record['cts_status'] = qpa_records.get(case, {}).get('status')
            record['cts_message'] = qpa_records.get(case, {}).get('message')
            if record['status'] not in ('Crash', 'Timeout'):
                record.update(status='Timeout',
                              message=f'Reached the recorded {case_timeout:g}s per-case bound ({duration:.6f}s)')
        results.append(record)
    return results
