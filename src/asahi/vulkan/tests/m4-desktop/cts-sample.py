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
    assert cases and len(cases) == len(set(cases))
    assert all(re.fullmatch(r'dEQP-VK\.[\w.\-]+', c) for c in cases)
    excluded = [c for c in cases if args.linux_only and
                ('win32' in c or c.startswith('dEQP-VK.wsi.android.'))]
    excluded_set = set(excluded)
    eligible = [c for c in cases if c not in excluded_set]
    chosen = random.Random(args.seed).sample(eligible, args.count)
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'cases.txt').write_text('\n'.join(chosen) + '\n')
    save(args.output / 'selection.json', dict(seed=args.seed, count=args.count,
         population=len(cases), eligible_population=len(eligible),
         platform_exclusions=excluded, linux_only=args.linux_only, mustpass=str(args.mustpass.resolve()),
         mustpass_sha256=hashlib.sha256(raw).hexdigest(),
         method='Python random.Random(seed).sample, original mustpass order; only recorded platform exclusions',
         categories=dict(collections.Counter(c.split('.')[1] for c in chosen)), cases=chosen))
    print((args.output / 'selection.json').read_text())


def batch(args):
    spec = importlib.util.spec_from_file_location('guardcheck', '/root/firmware-snapshot.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    check = lambda: module.check_guard(args.guard_output, boot)
    check()
    cases = args.cases.read_text().splitlines()
    out = args.guard_output / 'cts'
    out.mkdir()
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
    run_cases(args, check, boot, out, env)


def run_cases(args, check, boot, out, env):
    """One guard owns all children; a single loop checks progress and bounds."""
    cases = args.cases.read_text().splitlines()
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
            # Reserve the existing 20s case bound plus evidence time within
            # the outer 40s guard. No independent watchdog owners/workers.
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
                timed_out = proc.poll() is None and time.monotonic() - started > 20
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
                    status, message = 'Timeout', 'Exceeded the recorded 20s per-case bound'
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
             launched=next_index-args.start, case_timeout_seconds=20,
             launch_window_seconds=10))
    check()
    print(f'CTS_BATCH_COMPLETE: {len(results)} recorded outcomes; next index {next_index}', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='mode', required=True)
    s = sub.add_parser('select')
    s.add_argument('--mustpass', type=Path, required=True)
    s.add_argument('--output', type=Path, required=True)
    s.add_argument('--seed', type=int, default=20260913)
    s.add_argument('--count', type=int, default=100)
    s.add_argument('--linux-only', action='store_true',
                   help='Exclude Windows handles/WSI and Android WSI before selection; retain all other cases')
    b = sub.add_parser('batch')
    b.add_argument('--guard-output', type=Path, required=True)
    b.add_argument('--binary', type=Path, required=True)
    b.add_argument('--cases', type=Path, required=True)
    b.add_argument('--start', type=int, default=0)
    b.add_argument('--count', type=int, default=100)
    b.add_argument('--jobs', type=int, default=1, help='Concurrent CTS processes inside one guard')
    b.add_argument('--renderer', choices=('m4', 'lavapipe'), default='m4')
    b.add_argument('--log-sources', action='store_true')
    b.add_argument('--log-images', action='store_true')
    b.add_argument('--capture-success', action='store_true')
    args = p.parse_args()
    if args.mode == 'select':
        select(args)
    else:
        if args.jobs < 1 or args.start < 0 or args.count < 1:
            p.error('jobs/count must be positive and start nonnegative')
        batch(args)


if __name__ == '__main__':
    main()
