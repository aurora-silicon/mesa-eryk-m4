#!/usr/bin/env python3
"""Build an exact-output Vulkan fragment probe; never execute a GPU workload.

Retains the input shader, generated SPIR-V header, adapted triangle fixture,
commands and hashes. Run the resulting ARM binary under gpu-experiment.py.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=int, choices=[1, 2, 4], default=1)
    parser.add_argument('--rgba', type=int, nargs=4, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--sysroot', type=Path, default=Path(
        '/home/council/omarchy/graphics-build/sysroot'))
    args = parser.parse_args()
    if any(x < 0 or x > 255 for x in args.rgba):
        parser.error('RGBA bytes must be in 0..255')

    root = Path(__file__).resolve().parent.parent
    fixture = args.fixture or root / (
        'mesa/m4-asahi/src/asahi/vulkan/tests/g17p_offscreen_triangle.c')
    fixture = fixture.resolve()
    source = fixture.read_text()
    old = 'static const uint8_t expected[4] = {255, 71, 13, 255};'
    if source.count(old) != 1:
        raise RuntimeError('Fixture oracle changed; update adapter')
    source = source.replace(old, 'static const uint8_t expected[4] = {' +
                            ', '.join(map(str, args.rgba)) + '};')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.source, output / 'probe.frag')
    (output / 'probe.c').write_text(source)
    commands = [
        ['glslangValidator', '-V', '--target-env', 'vulkan1.1', '--vn',
         'g17p_wsi_triangle_frag_spv', '-o',
         str(output / 'g17p_wsi_triangle_frag.h'), str(output / 'probe.frag')],
        ['clang', '--target=aarch64-linux-gnu', '--sysroot=' + str(args.sysroot),
         '--gcc-install-dir=' + str(args.sysroot /
             'usr/lib/gcc/aarch64-unknown-linux-gnu/16.1.1'), '-fuse-ld=lld',
         '-DTEST_DEVICE_ID=0x8132', '-DG17P_MSAA_SAMPLES=' + str(args.samples),
         '-I' + str(fixture.parent), str(output / 'probe.c'), '-o',
         str(output / 'probe-arm64'), '-ldl'],
    ]
    with (output / 'build.log').open('w') as log:
        for command in commands:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=60)
    files = {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
             for f in output.iterdir() if f.is_file()}
    manifest = {
        'samples': args.samples, 'rgba': args.rgba,
        'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'commands': commands, 'sha256': files,
        'limitation': 'Adapted diagnostic fixture; not an application capture '
                      'or hardware qualification.',
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(output / 'probe-arm64')


if __name__ == '__main__':
    main()
