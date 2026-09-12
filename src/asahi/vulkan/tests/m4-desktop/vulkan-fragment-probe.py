#!/usr/bin/env python3
"""Build an exact-output Vulkan fragment probe; never execute a GPU workload.

Retains the input shader, generated SPIR-V header, adapted triangle fixture,
commands and hashes. Run the resulting ARM binary under gpu-experiment.py.
--host-compile-only builds a host pipeline-creation probe that cannot submit
work, for use with the M4 DRM shim. --vertex can replace its vertex shader.
"""
import argparse
import hashlib
import json
import os
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
    parser.add_argument('--workspace-root', type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--vertex', type=Path)
    parser.add_argument('--host-compile-only', action='store_true')
    parser.add_argument('--run-host-shim', action='store_true', help=
        'Build and run the host compile-only probe under the M4 no-op DRM shim')
    parser.add_argument('--record-command-buffer', action='store_true', help=
        'With --run-host-shim, also record one draw without submitting it')
    parser.add_argument('--sysroot', type=Path, default=Path(
        '/home/council/omarchy/graphics-build/sysroot'))
    args = parser.parse_args()
    args.host_compile_only |= args.run_host_shim
    if args.record_command_buffer and not args.run_host_shim:
        parser.error('--record-command-buffer requires --run-host-shim')
    for path in (args.source, args.vertex):
        if path is not None and not path.is_file():
            parser.error(f'Input does not exist: {path}')
    if any(x < 0 or x > 255 for x in args.rgba):
        parser.error('RGBA bytes must be in 0..255')

    root = args.workspace_root.resolve()
    fixture = args.fixture or root / (
        'mesa/m4-asahi/src/asahi/vulkan/tests/g17p_offscreen_triangle.c')
    fixture = fixture.resolve()
    source = fixture.read_text()
    old = 'static const uint8_t expected[4] = {255, 71, 13, 255};'
    if source.count(old) != 1:
        raise RuntimeError('Fixture oracle changed; update adapter')
    source = source.replace(old, 'static const uint8_t expected[4] = {' +
                            ', '.join(map(str, args.rgba)) + '};')
    if args.record_command_buffer:
        marker = '   const bool record_only ='
        if source.count(marker) != 1:
            raise RuntimeError('Recording fixture changed; update adapter')
        source = source.replace(marker, marker + ' true ||')
        source = source.replace('feature advertised, shaders compiled, ',
            'host shim shaders compiled; no GPU submission, ')
    elif args.host_compile_only:
        marker = '         "vkCreateGraphicsPipelines");'
        if source.count(marker) != 1:
            raise RuntimeError('Pipeline fixture changed; update adapter')
        source = source.replace(marker, marker + '\n'
            '   puts("PASS: pipeline compilation only; no GPU submission");\n'
            '   return EXIT_SUCCESS;\n')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.source, output / 'probe.frag')
    (output / 'probe.c').write_text(source)
    commands = [
        ['glslangValidator', '-V', '--target-env', 'vulkan1.1', '--vn',
         'g17p_wsi_triangle_frag_spv', '-o',
         str(output / 'g17p_wsi_triangle_frag.h'), str(output / 'probe.frag')],
    ]
    if args.vertex:
        shutil.copyfile(args.vertex, output / 'probe.vert')
        commands.append([
            'glslangValidator', '-V', '--target-env', 'vulkan1.1', '--vn',
            'g17p_wsi_triangle_vert_spv', '-o',
            str(output / 'g17p_wsi_triangle_vert.h'), str(output / 'probe.vert')])
    target_flags = [] if args.host_compile_only else [
         '--target=aarch64-linux-gnu', '--sysroot=' + str(args.sysroot),
         '--gcc-install-dir=' + str(args.sysroot /
             'usr/lib/gcc/aarch64-unknown-linux-gnu/16.1.1'), '-fuse-ld=lld']
    binary = output / ('probe-host-compile-only' if args.host_compile_only
                       else 'probe-arm64')
    commands.append(['clang', *target_flags,
         '-DTEST_DEVICE_ID=0x8132', '-DG17P_MSAA_SAMPLES=' + str(args.samples),
         '-I' + str(fixture.parent), str(output / 'probe.c'), '-o',
         str(binary), '-ldl'])
    with (output / 'build.log').open('w') as log:
        for command in commands:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=60)
    files = {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
             for f in output.iterdir() if f.is_file()}
    manifest = {
        'samples': args.samples, 'rgba': args.rgba,
        'host_compile_only': args.host_compile_only,
        'record_command_buffer': args.record_command_buffer,
        'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'commands': commands, 'sha256': files,
        'limitation': 'Adapted diagnostic fixture; not an application capture '
                      'or hardware qualification.',
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if args.run_host_shim:
        host = root / 'build/mesa-m4-asahi-host'
        shim = host / 'src/asahi/drm-shim/libasahi_noop_drm_shim.so'
        icd = host / 'src/asahi/vulkan/asahi_devenv_icd.x86_64.json'
        library = host / 'src/asahi/vulkan/libvulkan_asahi.so'
        captures = output / 'compiler'
        captures.mkdir()
        env = {k: v for k, v in os.environ.items() if not k.startswith(
            ('VK_', 'MESA_', 'AGX_', 'ASAHI_', 'HK_', 'LD_', 'GFXRECON_'))}
        overrides = dict(ASAHI_SHIM_CHIP='g16g', ASAHI_M4_EXPERIMENTAL='1',
            MESA_SHADER_CACHE_DISABLE='true', LD_PRELOAD=str(shim),
            VK_DRIVER_FILES=str(icd), AGX_COMPILER_CAPTURE_DIR=str(captures),
            AGX_COMPILER_CAPTURE_SUCCESS='all')
        env.update(overrides)
        manifest['host_environment'] = overrides
        manifest['host_sha256'] = {str(f): hashlib.sha256(f.read_bytes()).hexdigest()
                                  for f in (shim, icd, library)}
        manifest['limitation'] += ' Host shim performs no GPU execution; '
        manifest['limitation'] += 'unsupported input capabilities are diagnostic only.'
        (output / 'mesa.patch').write_bytes(subprocess.check_output([
            'git', '-C', str(fixture.parents[4]), 'diff', '--binary', 'HEAD']))
        try:
            with (output / 'compile.log').open('w') as log:
                result = subprocess.run([str(binary)], env=env, stdout=log,
                    stderr=subprocess.STDOUT, timeout=20)
            manifest['returncode'] = result.returncode
            print((output / 'compile.log').read_text()[-4000:])
            result.check_returncode()
        finally:
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(binary)


if __name__ == '__main__':
    main()
