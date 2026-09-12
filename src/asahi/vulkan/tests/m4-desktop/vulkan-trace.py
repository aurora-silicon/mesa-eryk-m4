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
    p.add_argument('--profile', type=Path, help='Capture with Khronos Profiles feature/extension limits')
    p.add_argument('--frames', help='Capture frame range, or replay screenshot frames (1-based)')
    p.add_argument('--dump-config', type=Path, help='GFXReconstruct resource dump selection for replay')
    p.add_argument('--memory-translation', choices=['none', 'rebind'], default='none')
    p.add_argument('--omit-pipeline-cache', action='store_true')
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
    if a.profile and (a.mode != 'capture' or a.renderer != 'lavapipe'):
        p.error('Profiles are only supported for lavapipe reference capture')
    if a.dump_config and a.mode != 'replay':
        p.error('--dump-config requires replay')
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
    env = dict(os.environ)
    # A previous shell's capture/ICD selection must not leak into replay.
    for key in list(env):
        if key.startswith(('GFXRECON_', 'VK_KHRONOS_PROFILES_')) or key in ('VK_ICD_FILENAMES', 'VK_INSTANCE_LAYERS', 'VK_LAYER_PATH', 'VK_ADD_LAYER_PATH'):
            del env[key]
    icd = Path('/usr/share/vulkan/icd.d/lvp_icd.json' if a.renderer == 'lavapipe' else '/opt/asahi-m4-port/asahi_icd.json')
    env.update(VK_DRIVER_FILES=str(icd), LD_LIBRARY_PATH='/opt/asahi-m4-port/lib',
               LIBGL_DRIVERS_PATH='/opt/asahi-m4-port/lib/dri',
               GBM_BACKENDS_PATH='/opt/asahi-m4-port/lib/gbm',
               __EGL_VENDOR_LIBRARY_FILENAMES='/opt/asahi-m4-port/egl.json',
               MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink',
               LIBGL_ALWAYS_SOFTWARE='1' if a.renderer == 'lavapipe' else '0',
               ASAHI_M4_EXPERIMENTAL='1', ASAHI_MESA_DEBUG='nocompress',
               MESA_GL_VERSION_OVERRIDE='3.3', MESA_GLES_VERSION_OVERRIDE='3.0')
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
        if a.omit_pipeline_cache:
            command += ['--omit-pipeline-cache-data']
        if a.dump_config:
            command += ['--dump-resources', str(a.dump_config.resolve(strict=True)), '--dump-resources-dir', str(out)]
        command.append(str(a.trace.resolve(strict=True)))
    library = Path(json.loads(icd.read_text())['ICD']['library_path'])
    if not library.is_absolute():
        library = Path('/usr/lib') / library
    files = [icd, library, layer, replay, Path(__file__), Path(__file__).with_name('firmware-snapshot.py'),
             a.tool_prefix / 'build-manifest.json', Path('/opt/asahi-m4-port/lib/libgallium-26.1.4.so')]
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
