#!/usr/bin/env python3
"""Select a reproducible CTS sample or run a bounded batch inside the J713 guard.

See README-cts.md. Each case is a separate CTS process with its original QPA log.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import re
import subprocess
import time


def save(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def select(args):
    raw = args.mustpass.read_bytes()
    cases = raw.decode().splitlines()
    if not cases or len(cases) != len(set(cases)) or not all(
            re.fullmatch(r'dEQP-VK\.[\w.\-]+', c) for c in cases):
        raise ValueError('Expected unique CTS case names')
    classifier = None
    if args.scope == 'required-advertised':
        if args.profile is None:
            raise ValueError('Filtered selection requires --profile (see the profile subcommand)')
        from cts_requirements import Classifier
        classifier = Classifier(args.profile, raw)
        labels = {c: classifier.classify(c) for c in cases}
    else:
        labels = {c: dict(classification=('platform-excluded' if args.linux_only and
                       ('win32' in c or '.android.' in c) else 'unclassified'),
                       rule='legacy unfiltered selection', requirements=[]) for c in cases}
    accepted = {'required', 'advertised'} if classifier else {'unclassified'}
    eligible = [c for c in cases if labels[c]['classification'] in accepted]
    if args.count < 1 or args.count > len(eligible):
        raise ValueError(f'count must be in 1..{len(eligible)}; no fallback to unmapped cases')
    chosen = random.Random(args.seed).sample(eligible, args.count)
    args.output.mkdir(parents=True, exist_ok=False)
    chosen_raw = ('\n'.join(chosen) + '\n').encode()
    (args.output / 'cases.txt').write_bytes(chosen_raw)
    provenance = dict(scope=args.scope,
        mustpass_sha256=hashlib.sha256(raw).hexdigest(),
        profile_sha256=hashlib.sha256(classifier.profile_raw).hexdigest() if classifier else None,
        catalog_sha256=hashlib.sha256(classifier.catalog_raw).hexdigest() if classifier else None,
        classifier_sha256=hashlib.sha256(Path(__file__).with_name('cts_requirements.py').read_bytes()).hexdigest() if classifier else None,
        identity=classifier.profile['identity'] if classifier else None)
    metadata = dict(schema=1, cases_sha256=hashlib.sha256(chosen_raw).hexdigest(),
                    provenance=provenance,
                    cases=[dict(case=c, **labels[c]) for c in chosen])
    save(args.output / 'cases.meta.json', metadata)
    with (args.output / 'case-labels.tsv').open('w') as stream:
        stream.write('classification\tcase\trule\n')
        for c in chosen:
            stream.write(f"{labels[c]['classification']}\t{c}\t{labels[c]['rule']}\n")
    # Retain every exclusion, including missing optional requirements. No CTS
    # outcomes are read or used, and no failed case is replaced after execution.
    with (args.output / 'exclusions.jsonl').open('w') as stream:
        for c in cases:
            if labels[c]['classification'] not in accepted:
                stream.write(json.dumps(dict(case=c, **labels[c])) + '\n')
    if classifier:
        (args.output / 'device-profile.json').write_bytes(classifier.profile_raw)
        (args.output / 'requirements.json').write_bytes(classifier.catalog_raw)
        (args.output / 'cts_requirements.py').write_bytes(Path(__file__).with_name('cts_requirements.py').read_bytes())
    summary = dict(seed=args.seed, count=args.count, population=len(cases),
        eligible_population=len(eligible),
        population_classifications=dict(collections.Counter(v['classification'] for v in labels.values())),
        selected_classifications=dict(collections.Counter(labels[c]['classification'] for c in chosen)),
        linux_only=bool(classifier) or args.linux_only, mustpass=str(args.mustpass.resolve()),
        **provenance,
        method='Python random.Random(seed).sample, original mustpass order after recorded eligibility filter',
        coverage_note='Reviewed rule coverage only; unclassified cases are excluded, not claimed optional or unsupported.',
        categories=dict(collections.Counter(c.split('.')[1] for c in chosen)), cases=chosen)
    save(args.output / 'selection.json', summary)
    print(json.dumps({k: v for k, v in summary.items() if k != 'cases'}, indent=2))


def case_metadata(path):
    sidecar = path.with_suffix('.meta.json')
    if not sidecar.exists():
        return None
    raw = path.read_bytes()
    meta = json.loads(sidecar.read_text())
    if (meta.get('schema') != 1 or meta.get('cases_sha256') != hashlib.sha256(raw).hexdigest()
            or [c['case'] for c in meta['cases']] != raw.decode().splitlines()):
        raise ValueError(f'Case metadata does not match frozen case list: {sidecar}')
    return meta


def batch(args):
    spec = importlib.util.spec_from_file_location('guardcheck', '/root/firmware-snapshot.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    check = lambda: module.check_guard(args.guard_output, boot)
    check()
    metadata = case_metadata(args.cases)
    if metadata and args.renderer == 'm4' and metadata['provenance'].get('identity'):
        identity = metadata['provenance']['identity']
        if os.uname().release != identity['kernel']:
            raise RuntimeError('Capability profile kernel changed; recapture and reselect')
        for name, expected in identity['files'].items():
            if hashlib.sha256(Path(name).read_bytes()).hexdigest() != expected:
                raise RuntimeError(f'Capability profile driver changed: {name}; recapture and reselect')
    cases = args.cases.read_text().splitlines()
    # A latched display failure can leave headless GPU execution healthy while
    # every presentation test blocks. Refuse that invalid WSI environment.
    if any('.wsi.' in c for c in cases[args.start:args.start + args.count]):
        for status in {p.resolve() for p in Path('/sys/kernel/debug/dri').glob('*/m4_status')}:
            fields = dict(line.split(maxsplit=1) for line in status.read_text().splitlines())
            if int(fields.get('failed', '0')):
                raise RuntimeError(f'WSI requires display recovery: {status}: {fields}')
    out = args.guard_output / 'cts'
    out.mkdir()
    if metadata:
        save(out / 'selection-meta.json', metadata)
    env = os.environ.copy()
    env.update(VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json',
               VK_ICD_FILENAMES='/opt/asahi-m4-port/asahi_icd.json',
               ASAHI_M4_EXPERIMENTAL='1', LD_LIBRARY_PATH='/opt/asahi-m4-port/lib',
               ASAHI_MESA_DEBUG='nocompress', HK_DEBUG_LOG='1',
               XDG_RUNTIME_DIR='/run/user/1000')
    if args.renderer == 'lavapipe':
        env.update(VK_DRIVER_FILES='/usr/share/vulkan/icd.d/lvp_icd.json',
                   VK_ICD_FILENAMES='/usr/share/vulkan/icd.d/lvp_icd.json',
                   LD_LIBRARY_PATH='/usr/lib')
    displays = sorted(Path('/run/user/1000').glob('wayland-*'))
    env['WAYLAND_DISPLAY'] = next((p.name for p in displays if p.is_socket()), 'wayland-1')
    capture = out / 'compiler-captures'
    capture.mkdir()
    env['AGX_COMPILER_CAPTURE_DIR'] = str(capture)
    env.pop('AGX_COMPILER_CAPTURE_SUCCESS', None)
    if args.capture_success:
        env['AGX_COMPILER_CAPTURE_SUCCESS'] = 'all'
        env['MESA_SHADER_CACHE_DISABLE'] = 'true'
    save(out / 'environment.json', {k: v for k, v in env.items() if k.startswith(
         ('VK_', 'ASAHI_', 'AGX_', 'HK_', 'MESA_', 'LD_LIBRARY', 'WAYLAND_', 'XDG_RUNTIME'))})
    save(out / 'binary.json', dict(path=str(args.binary), sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest()))
    if args.backend == 'deqp-runner':
        from cts_deqp_backend import run
        run(args, check, boot, out, env, metadata, save)
    else:
        run_cases(args, check, boot, out, env)


def run_cases(args, check, boot, out, env):
    """One guard owns all children; a single loop checks progress and bounds."""
    cases = args.cases.read_text().splitlines()
    metadata = case_metadata(args.cases)
    results, active = [], {}
    begin = time.monotonic()
    started_unix = time.time()
    next_index = args.start
    end = min(len(cases), args.start + args.count)
    stop_launching = False
    peak_workers = 0
    save(out / 'results.json', results)
    try:
        while active or (next_index < end and not stop_launching and
                         time.monotonic() - begin < 10):
            check()
            # The campaign reserves the case bound plus 10s launch time and
            # 10s evidence margin. No independent watchdog owners/workers.
            while (len(active) < args.jobs and next_index < end and
                   not stop_launching and time.monotonic() - begin < 10):
                index = next_index
                directory = out / f'{index:03d}'
                directory.mkdir()
                qpa = directory / 'result.qpa'
                argv = [str(args.binary), '--deqp-case=' + cases[index],
                        '--deqp-log-images=' + ('enable' if args.log_images else 'disable'),
                        '--deqp-log-shader-sources=' + ('enable' if args.log_sources else 'disable'),
                        '--deqp-log-filename=' + str(qpa)]
                record = dict(index=index, case=cases[index], argv=argv,
                              boot_id=boot, status='Started', started_unix=time.time())
                record['eligibility'] = (metadata['cases'][index] if metadata else
                    dict(case=cases[index], classification='unclassified', rule='bare case list'))
                save(directory / 'case.json', record)
                log = (directory / 'stdout.log').open('w')
                try:
                    proc = subprocess.Popen(argv, cwd=args.binary.parent, env=env,
                                            stdout=log, stderr=subprocess.STDOUT)
                except BaseException:
                    log.close()
                    raise
                active[index] = (proc, log, time.monotonic(), directory, qpa, record)
                next_index += 1
                peak_workers = max(peak_workers, len(active))

            for index, item in list(active.items()):
                proc, log, started, directory, qpa, record = item
                timed_out = proc.poll() is None and time.monotonic() - started > args.case_timeout
                if timed_out:
                    proc.kill()
                    proc.wait(timeout=2)
                if proc.poll() is None:
                    continue
                log.flush()
                os.fsync(log.fileno())
                log.close()
                content = qpa.read_text(errors='replace') if qpa.exists() else ''
                codes = re.findall(r'<Result StatusCode="([^"]+)"[^>]*>(.*?)</Result>', content, re.S)
                status, message = codes[-1] if codes else ('HarnessError', 'No completed CTS result (check case selection and process output)')
                if timed_out:
                    status, message = 'Timeout', f'Exceeded the recorded {args.case_timeout}s per-case bound'
                elif proc.returncode < 0:
                    status, message = 'Crash', f'Process signal {-proc.returncode}'
                elif status == 'Pass' and proc.returncode != 0:
                    status, message = 'HarnessError', 'CTS reported Pass but exited nonzero'
                record.update(status=status, message=message, returncode=proc.returncode,
                              seconds=time.monotonic() - started, finished_unix=time.time(),
                              pid=proc.pid)
                save(directory / 'case.json', record)
                results.append(record)
                results.sort(key=lambda x: x['index'])
                save(out / 'results.json', results)
                del active[index]
                print(json.dumps(record), flush=True)
                output = (directory / 'stdout.log').read_text(errors='replace')
                if status in ('Crash', 'Timeout', 'HarnessError') or 'VK_ERROR_DEVICE_LOST' in output + content:
                    # Drain already-running bounded cases but never schedule
                    # more work after a fatal result. Preserve every outcome.
                    stop_launching = True
            if active:
                time.sleep(0.05)
    finally:
        # All children share the outer guard's process group. Also clean up
        # promptly on a local guard-check or evidence-writing exception.
        for proc, log, *_ in active.values():
            if proc.poll() is None:
                proc.kill()
        for proc, log, *_ in active.values():
            proc.wait(timeout=2)
            if not log.closed:
                log.flush()
                os.fsync(log.fileno())
                log.close()
        save(out / 'timing.json', dict(jobs=args.jobs, peak_workers=peak_workers,
             started_unix=started_unix, finished_unix=time.time(),
             elapsed_seconds=time.monotonic() - begin, completed=len(results),
             launched=next_index-args.start, case_timeout_seconds=args.case_timeout,
             launch_window_seconds=10))
    check()
    print(f'CTS_BATCH_COMPLETE: {len(results)} recorded outcomes; next index {next_index}', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='mode', required=True)
    profile = sub.add_parser('profile', help='Offline: bind guarded vulkaninfo JSON to target-before.json')
    profile.add_argument('--vulkaninfo', type=Path, required=True)
    profile.add_argument('--identity', type=Path, required=True)
    profile.add_argument('--output', type=Path, required=True)
    s = sub.add_parser('select')
    s.add_argument('--mustpass', type=Path, required=True)
    s.add_argument('--output', type=Path, required=True)
    s.add_argument('--seed', type=int, default=20260913)
    s.add_argument('--count', type=int, default=100)
    s.add_argument('--profile', type=Path, help='Saved capabilities from the profile subcommand')
    s.add_argument('--scope', choices=('required-advertised', 'all'), default='required-advertised',
                   help='Default: reviewed required/advertised cases; all reproduces legacy sampling')
    s.add_argument('--linux-only', action='store_true',
                   help='Exclude Windows handles/WSI and Android WSI before selection; retain all other cases')
    b = sub.add_parser('batch')
    b.add_argument('--guard-output', type=Path, required=True)
    b.add_argument('--binary', type=Path, required=True)
    b.add_argument('--cases', type=Path, required=True)
    b.add_argument('--start', type=int, default=0)
    b.add_argument('--count', type=int, default=100)
    b.add_argument('--jobs', type=int, default=1, help='Concurrent CTS processes inside one guard')
    b.add_argument('--case-timeout', type=int, default=20, choices=range(1, 121),
                   metavar='SECONDS', help='Per-case bound, 1..120s; outer guard must allow 20s more')
    b.add_argument('--backend', choices=('isolated', 'deqp-runner'), default='isolated')
    b.add_argument('--deqp-runner', type=Path, default=Path('/root/deqp-runner'))
    b.add_argument('--tests-per-group', type=int, default=50)
    b.add_argument('--runner-seconds', type=int, default=90,
                   help='Bulk workload bound; outer guard must allow additional evidence time')
    b.add_argument('--renderer', choices=('m4', 'lavapipe'), default='m4')
    b.add_argument('--log-sources', action='store_true')
    b.add_argument('--log-images', action='store_true')
    b.add_argument('--capture-success', action='store_true')
    args = p.parse_args()
    if args.mode == 'profile':
        from cts_requirements import make_profile
        if args.output.exists():
            p.error('profile output already exists; retain old capability evidence')
        save(args.output, make_profile(args.vulkaninfo, args.identity))
    elif args.mode == 'select':
        select(args)
    else:
        if args.jobs < 1 or args.start < 0 or args.count < 1 or args.tests_per_group < 1 or not 1 <= args.runner_seconds <= 1700:
            p.error('jobs/count must be positive and start nonnegative')
        batch(args)


if __name__ == '__main__':
    main()
