#!/usr/bin/env python3
"""Compare retained compiler inputs, exact output bytes, and host wall times.

The replayers must support --binary-output=FILE. --inputs is a JSON list of
input.bin paths. Alternate execution order across paired rounds. Timings include
process startup; they are host compilation measurements, not GPU frame times.
No hardware access. Each compiler process has a 30-second bound.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=20)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 100:
        parser.error('--rounds must be in [1,100]')
    inputs = [Path(p).resolve() for p in json.loads(args.inputs.read_text())]
    if not inputs:
        parser.error('input list is empty')
    args.output.mkdir(parents=True, exist_ok=False)
    executables = {'before': args.before.resolve(), 'after': args.after.resolve()}
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('AGX_COMPILER_CAPTURE') and k != 'AGX_MESA_DEBUG'}
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = dict(scope=__doc__, executables={
        k: dict(path=str(p), sha256=digest(p)) for k, p in executables.items()},
        environment={k: v for k, v in env.items() if k.startswith(('AGX_', 'ASAHI_', 'MESA_'))},
        comparisons=[], rounds=[])

    def save():
        (args.output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(side, source, output=None):
        command = [str(executables[side]), str(source)]
        if output is not None:
            command.append('--binary-output=' + str(output.resolve()))
        started = time.perf_counter()
        result = subprocess.run(command, capture_output=True, timeout=30, env=env)
        seconds = time.perf_counter() - started
        if result.returncode:
            raise RuntimeError(f'{command}: {result.returncode}: {result.stderr.decode(errors="replace")}')
        return seconds, result

    try:
        for index, source in enumerate(inputs):
            outputs = {}
            for side in executables:
                output = args.output / f'{index:04d}-{side}.bin'
                _, result = run(side, source, output)
                (args.output / f'{index:04d}-{side}.log').write_bytes(result.stdout + result.stderr)
                outputs[side] = output
            identical = outputs['before'].read_bytes() == outputs['after'].read_bytes()
            report['comparisons'].append(dict(input=str(source), input_sha256=digest(source),
                identical=identical, binary_sha256={k: digest(p) for k, p in outputs.items()}))
            save()
            if not identical:
                raise RuntimeError(f'Binary mismatch: {source}')
        for iteration in range(args.rounds):
            totals = dict(before=0.0, after=0.0)
            for index, source in enumerate(inputs):
                order = ['before', 'after'] if (iteration + index) % 2 == 0 else ['after', 'before']
                for side in order:
                    seconds, _ = run(side, source)
                    totals[side] += seconds
            report['rounds'].append(totals)
            save()
        report['median_seconds'] = {side: statistics.median(r[side] for r in report['rounds'])
                                    for side in executables}
        report['median_paired_reduction_percent'] = statistics.median(
            100 * (1 - r['after'] / r['before']) for r in report['rounds'])
        print(json.dumps({k: report[k] for k in ('median_seconds', 'median_paired_reduction_percent')}))
    except BaseException as error:
        report['error'] = str(error)
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
