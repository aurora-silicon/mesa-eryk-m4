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
    chosen = random.Random(args.seed).sample(cases, args.count)
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'cases.txt').write_text('\n'.join(chosen) + '\n')
    save(args.output / 'selection.json', dict(seed=args.seed, count=args.count,
         population=len(cases), mustpass=str(args.mustpass.resolve()),
         mustpass_sha256=hashlib.sha256(raw).hexdigest(),
         method='Python random.Random(seed).sample, original mustpass order; no exclusions',
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
               ASAHI_MESA_DEBUG='nocompress', XDG_RUNTIME_DIR='/run/user/1000')
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
    save(out / 'environment.json', {k: v for k, v in env.items() if k.startswith(
         ('VK_', 'ASAHI_', 'AGX_', 'LD_LIBRARY', 'WAYLAND_', 'XDG_RUNTIME'))})
    save(out / 'binary.json', dict(path=str(args.binary), sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest()))
    results = []
    begin = time.monotonic()
    for index in range(args.start, min(len(cases), args.start + args.count)):
        # Leave time for a 20s case, guard evidence collection, and lease release.
        if time.monotonic() - begin > 10:
            break
        check()
        case = cases[index]
        directory = out / f'{index:03d}'
        directory.mkdir()
        qpa = directory / 'result.qpa'
        argv = [str(args.binary), '--deqp-case=' + case,
                '--deqp-log-images=disable',
                '--deqp-log-shader-sources=' + ('enable' if args.log_sources else 'disable'),
                '--deqp-log-filename=' + str(qpa)]
        record = dict(index=index, case=case, argv=argv, boot_id=boot, status='Started')
        save(directory / 'case.json', record)
        started = time.monotonic()
        timed_out = False
        with (directory / 'stdout.log').open('w') as log:
            proc = subprocess.Popen(argv, cwd=args.binary.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                while proc.poll() is None:
                    check()
                    if time.monotonic() - started > 20:
                        timed_out = True
                        proc.kill()
                        proc.wait(timeout=2)
                        break
                    time.sleep(0.2)
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.wait(timeout=2)
                log.flush()
                os.fsync(log.fileno())
        check()
        content = qpa.read_text(errors='replace') if qpa.exists() else ''
        codes = re.findall(r'<Result StatusCode="([^"]+)"[^>]*>(.*?)</Result>', content, re.S)
        status, message = codes[-1] if codes else ('Crash', 'No completed CTS result')
        if timed_out:
            status, message = 'Timeout', 'Exceeded the recorded 20s per-case bound'
        elif proc.returncode < 0:
            status, message = 'Crash', f'Process signal {-proc.returncode}'
        elif status == 'Pass' and proc.returncode != 0:
            status, message = 'HarnessError', 'CTS reported Pass but exited nonzero'
        record.update(status=status, message=message, returncode=proc.returncode,
                      seconds=time.monotonic() - started)
        save(directory / 'case.json', record)
        results.append(record)
        save(out / 'results.json', results)
        print(json.dumps(record), flush=True)
        output = (directory / 'stdout.log').read_text(errors='replace')
        if status in ('Crash', 'Timeout') or 'VK_ERROR_DEVICE_LOST' in output + content:
            break
    check()
    print(f'CTS_BATCH_COMPLETE: {len(results)} recorded outcomes; next index {args.start + len(results)}', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='mode', required=True)
    s = sub.add_parser('select')
    s.add_argument('--mustpass', type=Path, required=True)
    s.add_argument('--output', type=Path, required=True)
    s.add_argument('--seed', type=int, default=20260913)
    s.add_argument('--count', type=int, default=100)
    b = sub.add_parser('batch')
    b.add_argument('--guard-output', type=Path, required=True)
    b.add_argument('--binary', type=Path, required=True)
    b.add_argument('--cases', type=Path, required=True)
    b.add_argument('--start', type=int, default=0)
    b.add_argument('--count', type=int, default=100)
    b.add_argument('--renderer', choices=('m4', 'lavapipe'), default='m4')
    b.add_argument('--log-sources', action='store_true')
    args = p.parse_args()
    if args.mode == 'select':
        select(args)
    else:
        batch(args)


if __name__ == '__main__':
    main()
