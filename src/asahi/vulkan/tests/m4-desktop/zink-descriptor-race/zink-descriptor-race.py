#!/usr/bin/env python3
"""Stress real Zink descriptor allocation offline under ASan, without a GPU.

Requires build/mesa-m4-asahi-host (including compile_commands.json and its util
archives). Run from any directory with --output pointing to a new evidence dir.
The companion C file includes the live Zink descriptor implementation and stubs
only Vulkan descriptor-layout creation/destruction.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / 'build/mesa-m4-asahi-host'
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output.resolve()
    entry = next(
        entry for entry in json.loads((build / 'compile_commands.json').read_text())
        if entry['file'].endswith('/zink_descriptors.c')
    )
    command = shlex.split(entry['command'])
    flags = []
    index = 2 if Path(command[0]).name == 'ccache' else 1
    while index < len(command):
        flag = command[index]
        index += 1
        if flag in ('-MQ', '-MF', '-o'):
            index += 1
            continue
        if flag in ('-MD', '-c') or flag == entry['file']:
            continue
        flags.append(flag)
    # Assertions are part of the reproducer, even with a release host build.
    flags += ['-UNDEBUG', '-fsanitize=address', '-fno-omit-frame-pointer', '-O1',
              '-ffunction-sections', '-fdata-sections']
    compiler = ['cc'] + flags
    commands = [
        compiler + ['-c', str(root / 'tools/zink-descriptor-race.c'),
                    '-o', str(output / 'test.o')],
        compiler + ['-c', str(root / 'mesa/m4-asahi/src/util/ralloc.c'),
                    '-o', str(output / 'ralloc.o')],
        ['cc', '-fsanitize=address', '-pthread', '-Wl,--gc-sections',
         str(output / 'test.o'), str(output / 'ralloc.o'),
         str(build / 'src/util/libmesa_util.a'),
         str(build / 'src/vulkan/util/libvulkan_util.a'),
         '-ldl', '-lm', '-lzstd', '-lz', '-lunwind', '-o', str(output / 'test')],
    ]
    (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    with (output / 'build.log').open('w') as log:
        for command in commands:
            subprocess.run(command, cwd=build, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
    with (output / 'test.log').open('w') as log:
        result = subprocess.run(
            [str(output / 'test')], stdout=log, stderr=subprocess.STDOUT,
            timeout=30,
            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'),
        )
    (output / 'result.json').write_text(
        json.dumps({'returncode': result.returncode}) + '\n')
    print('returncode', result.returncode)
    return result.returncode if result.returncode >= 0 else 128 - result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
