#!/usr/bin/env python3
"""Target-side GFXReconstruct capture/replay; requires an existing live guard.

Does not arm or renew the watchdog. Use gpu-experiment.py to supervise this
command. Capture keeps the private Zink build while explicitly selecting an ICD.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def sha(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['capture', 'replay'])
    p.add_argument('--guard-output', required=True, type=Path)
    p.add_argument('--renderer', required=True, choices=['lavapipe', 'asahi'])
    p.add_argument('--seconds', type=int, default=25, choices=range(1, 1801), metavar='1..1800')
    p.add_argument('--tool-prefix', type=Path, default=Path('/opt/gfxreconstruct'))
    p.add_argument('--trace', type=Path, help='Existing trace for replay')
    p.add_argument('--asahi-debug', default='nocompress', help='Explicit Asahi driver debug flags, retained in environment evidence')
    p.add_argument('--icd', type=Path, help='Explicit candidate ICD; retained and hashed with its library')
    p.add_argument('--mesa-prefix', type=Path, default=Path('/opt/asahi-m4-port'),
                   help='Matching EGL/Gallium/GBM prefix for this isolated run')
    p.add_argument('--profile', type=Path, help='Capture with Khronos Profiles feature/extension limits')
    p.add_argument('--frames', help='Capture frame range, or replay screenshot frames (1-based)')
    p.add_argument('--end-frame', type=int, help='Replay through this frame, retaining all preceding state')
    p.add_argument('--loop-frame', type=int, help='Experimental fixed-frame replay; requires bounded --loop-count')
    p.add_argument('--loop-count', type=int, help='Repeat count from 1 to 1000; never replay indefinitely')
    p.add_argument('--profile-frame-loop', action='store_true', help='Log replay object-restoration phase times')
    p.add_argument('--replace-shaders', type=Path, help='Explicit diagnostic SPIR-V replacements; never application-support evidence')
    p.add_argument('--validation-layer', type=Path, help='Explicit diagnostic Khronos validation manifest for replay')
    p.add_argument('--dump-config', type=Path, help='GFXReconstruct resource dump selection for replay')
    p.add_argument('--memory-translation', choices=['none', 'rebind'], default='none')
    p.add_argument('--omit-pipeline-cache', action='store_true')
    p.add_argument('--remove-unsupported', action='store_true', help='Explicit diagnostic adaptation; never proves unmodified application support')
    p.add_argument('--retain', type=Path, action='append', default=[], help='Copy an application config into evidence')
    # The command follows an explicit --, so ordinary options remain parseable.
    import sys
    argv = sys.argv[1:]
    split = argv.index('--') if '--' in argv else len(argv)
    command = argv[split + 1:]
    a = p.parse_args(argv[:split])
    if (a.mode == 'capture' and (not command or a.trace)) or (a.mode == 'replay' and (command or not a.trace)):
        p.error('capture requires -- PROGRAM ARGS; replay requires --trace and no command')
    if a.mode == 'capture' and (a.memory_translation != 'none' or a.omit_pipeline_cache):
        p.error('Replay adaptations do not apply to capture')
    if a.remove_unsupported and a.mode != 'replay':
        p.error('--remove-unsupported is only a diagnostic replay adaptation')
    if a.profile and (a.mode != 'capture' or a.renderer != 'lavapipe'):
        p.error('Profiles are only supported for lavapipe reference capture')
    if a.replace_shaders and a.mode != 'replay':
        p.error('--replace-shaders requires replay')
    if a.dump_config and a.mode != 'replay':
        p.error('--dump-config requires replay')
    if a.end_frame is not None and (a.mode != 'replay' or a.end_frame < 1):
        p.error('--end-frame requires replay and a positive frame number')
    if a.loop_frame is not None or a.loop_count is not None:
        if a.mode != 'replay' or not a.loop_frame or a.loop_frame < 1 or not a.loop_count or not 1 <= a.loop_count <= 1000:
            p.error('Loop replay requires a positive frame and --loop-count 1..1000')
        if a.end_frame is not None and a.end_frame < a.loop_frame:
            p.error('--end-frame must include the loop frame')
    guard = a.guard_output.resolve(strict=True)
    spec = importlib.util.spec_from_file_location('guard_check', Path(__file__).with_name('firmware-snapshot.py'))
    guard_check = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard_check)
    boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()

    def check():
        guard_check.check_guard(guard, boot)
        assert not (guard / 'result.json').exists()
        assert not (guard / 'thermal-stop.json').exists()

    check()
    out = guard / ('vulkan-' + a.mode)
    out.mkdir(exist_ok=False)
    compiler_capture = out / 'compiler-capture'
    compiler_capture.mkdir(mode=0o700)
    env = dict(os.environ)
    # A previous shell's capture/ICD selection must not leak into replay.
    for key in list(env):
        if key.startswith(('GFXRECON_', 'VK_KHRONOS_PROFILES_')) or key in ('VK_ICD_FILENAMES', 'VK_INSTANCE_LAYERS', 'VK_LAYER_PATH', 'VK_ADD_LAYER_PATH'):
            del env[key]
    icd = Path('/usr/share/vulkan/icd.d/lvp_icd.json' if a.renderer == 'lavapipe' else '/opt/asahi-m4-port/asahi_icd.json')
    if a.profile_frame_loop:
        env['GFXRECON_FRAME_LOOP_PROFILE'] = '1'
    if a.icd:
        icd = a.icd.resolve(strict=True)
    assert a.mesa_prefix.is_absolute() and a.mesa_prefix.is_dir()
    env.update(VK_DRIVER_FILES=str(icd), LD_LIBRARY_PATH=str(a.mesa_prefix/'lib'),
               LIBGL_DRIVERS_PATH=str(a.mesa_prefix/'lib/dri'),
               GBM_BACKENDS_PATH=str(a.mesa_prefix/'lib/gbm'),
               __EGL_VENDOR_LIBRARY_FILENAMES=str(a.mesa_prefix/'egl.json'),
               MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink',
               LIBGL_ALWAYS_SOFTWARE='1' if a.renderer == 'lavapipe' else '0',
               ASAHI_M4_EXPERIMENTAL='1', ASAHI_MESA_DEBUG=a.asahi_debug,
               HK_DEBUG_LOG='1', MESA_LOG_LEVEL='debug',
               AGX_COMPILER_CAPTURE_DIR=str(compiler_capture),
               MESA_GL_VERSION_OVERRIDE='3.3', MESA_GLES_VERSION_OVERRIDE='3.0')
    if a.validation_layer:
        if a.mode != 'replay':
            p.error('--validation-layer is replay-only')
        manifest = a.validation_layer.resolve(strict=True)
        metadata = json.loads(manifest.read_text())
        assert metadata['layer']['name'] == 'VK_LAYER_KHRONOS_validation'
        env.update(VK_LAYER_PATH=str(manifest.parent),
                   VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',
                   VK_LAYER_ENABLES='VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
        (out/'validation-manifest.json').write_bytes(manifest.read_bytes())
        (out/'validation-manifest.sha256').write_text(sha(manifest)+'\n')
        layer_library = Path(metadata['layer']['library_path'])
        if not layer_library.is_absolute():
            candidates = [manifest.parent / layer_library, Path('/usr/lib') / layer_library]
            layer_library = next(candidate for candidate in candidates if candidate.is_file())
        layer_library = layer_library.resolve(strict=True)
        (out/'validation-library.json').write_text(json.dumps({
            'path': str(layer_library), 'sha256': sha(layer_library)}, indent=2) + '\n')
    layer = a.tool_prefix / 'lib/libVkLayer_gfxreconstruct.so'
    replay = a.tool_prefix / 'bin/gfxrecon-replay'
    if a.mode == 'capture':
        env.update(VK_LAYER_PATH=str(a.tool_prefix / 'share/vulkan/explicit_layer.d'),
                   VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',
                   GFXRECON_CAPTURE_FILE=str(out / 'capture.gfxr'),
                   GFXRECON_CAPTURE_FILE_TIMESTAMP='false')
        if a.frames:
            env.update(GFXRECON_CAPTURE_FRAMES=a.frames, GFXRECON_QUIT_AFTER_CAPTURE_FRAMES='true')
        if a.profile:
            env.update(VK_LAYER_PATH=env['VK_LAYER_PATH'] + ':/usr/share/vulkan/explicit_layer.d',
                       VK_INSTANCE_LAYERS=env['VK_INSTANCE_LAYERS'] + ':VK_LAYER_KHRONOS_profiles',
                       VK_KHRONOS_PROFILES_PROFILE_FILE=str(a.profile.resolve(strict=True)),
                       VK_KHRONOS_PROFILES_SIMULATE_CAPABILITIES='SIMULATE_API_VERSION_BIT,SIMULATE_FEATURES_BIT,SIMULATE_EXTENSIONS_BIT,SIMULATE_PROPERTIES_BIT',
                       VK_KHRONOS_PROFILES_DEFAULT_FEATURE_VALUES='DEFAULT_FEATURE_VALUES_FALSE',
                       VK_KHRONOS_PROFILES_UNKNOWN_FEATURE_VALUES='UNKNOWN_FEATURE_VALUES_UNCHANGED')
    else:
        command = [str(replay), '--swapchain', 'offscreen', '--memory-translation', a.memory_translation,
                   '--screenshot-dir', str(out), '--screenshot-format', 'png']
        if a.frames:
            command += ['--screenshots', a.frames]
        if a.remove_unsupported:
            command += ['--remove-unsupported']
        if a.omit_pipeline_cache:
            command += ['--omit-pipeline-cache-data']
        if a.end_frame and a.loop_frame is None:
            command += ['--measurement-frame-range', f'1-{a.end_frame + 1}',
                        '--quit-after-measurement-range', '--measurement-file',
                        str(out / 'measurements.json')]
        if a.loop_frame is not None:
            command += ['--loop-frame', str(a.loop_frame), '--loop-count', str(a.loop_count),
                        '--measurement-frame-range', f'{a.loop_frame}-{a.loop_frame + a.loop_count}',
                        '--measurement-file', str(out / 'measurements.json')]
        if a.dump_config:
            command += ['--dump-resources', str(a.dump_config.resolve(strict=True)), '--dump-resources-dir', str(out)]
        if a.replace_shaders:
            command += ['--replace-shaders', str(a.replace_shaders.resolve(strict=True))]
        command.append(str(a.trace.resolve(strict=True)))
    replacement_files = []
    if a.replace_shaders:
        import re
        replacement_files = sorted(a.replace_shaders.iterdir())
        if not replacement_files or any(not f.is_file() or not re.fullmatch(r'sh[0-9]+', f.name) for f in replacement_files):
            raise ValueError('Replacement directory must contain only sh<decimal-handle> files')
        saved = out / 'shader-replacements'; saved.mkdir()
        for f in replacement_files: (saved / f.name).write_bytes(f.read_bytes())
    library = Path(json.loads(icd.read_text())['ICD']['library_path'])
    if not library.is_absolute():
        library = Path('/usr/lib') / library
    files = [icd, library, layer, replay, Path(__file__), Path(__file__).with_name('firmware-snapshot.py'),
             a.tool_prefix / 'build-manifest.json', a.mesa_prefix/'lib/libgallium-26.1.4.so']
    files += replacement_files
    if a.profile:
        files += [a.profile, Path('/usr/lib/libVkLayer_khronos_profiles.so')]
        (out / 'capture-profile.json').write_bytes(a.profile.read_bytes())
        report = a.profile.with_suffix('.report.json')
        if report.exists():
            files.append(report)
            (out / 'capture-profile.report.json').write_bytes(report.read_bytes())
    if a.trace:
        files.append(a.trace)
    if a.dump_config:
        files.append(a.dump_config)
        (out / 'dump-config.json').write_bytes(a.dump_config.read_bytes())
    manifest = dict(mode=a.mode, renderer=a.renderer, command=command,
                    environment={k: v for k, v in sorted(env.items()) if k.startswith(
                        ('VK_', 'GFXRECON_', 'MESA_', 'LIBGL_', 'GALLIUM_', 'ASAHI_', 'AGX_', 'HK_', 'ZINK_', 'LD_LIBRARY_', 'GBM_', '__EGL_'))},
                    hashes={str(f): sha(f) for f in files}, adaptations=dict(
                        memory_translation=a.memory_translation, omit_pipeline_cache=a.omit_pipeline_cache,
                        remove_unsupported=a.remove_unsupported,
                        loop_frame=a.loop_frame, loop_count=a.loop_count,
                        shader_replacements=[f.name for f in replacement_files],
                        swapchain='offscreen' if a.mode == 'replay' else 'application'),
                    seconds=a.seconds, guard=str(guard))
    for index, source in enumerate(a.retain):
        data = source.read_bytes()
        (out / f'config-{index}-{source.name}').write_bytes(data)
        manifest['hashes'][str(source)] = hashlib.sha256(data).hexdigest()
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    child = None
    result = dict(started=time.monotonic())
    def interrupted(signum, frame):
        raise InterruptedError(f'Trace workload interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        check()
        # Inherit the parent's log: API errors remain visible to its classifier.
        child = subprocess.Popen(command, env=env, start_new_session=True)
        while child.poll() is None:
            check()
            if time.monotonic() - result['started'] > a.seconds:
                raise TimeoutError('Trace workload exceeded its bounded duration')
            time.sleep(.2)
        result['returncode'] = child.returncode
        check()
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        if child is not None and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=2)
        result['ended'] = time.monotonic()
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        os.sync()
    if result['returncode']:
        raise SystemExit(result['returncode'])
    traces = list(out.glob('*.gfxr'))
    if a.mode == 'capture' and not any(f.stat().st_size > 0 for f in traces):
        raise RuntimeError('Application exited without producing a trace')
    print(f'PASS: Vulkan {a.mode} process completed; rendering requires separate comparison', flush=True)


if __name__ == '__main__':
    main()
