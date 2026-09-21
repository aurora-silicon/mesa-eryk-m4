#!/usr/bin/env python3
"""Bounded J713 macOS STK benchmark with read-only PMP and powermetrics telemetry.

Uses a privately adapted STK 1.5 bundle, Linux-matched quality settings and an
explicit Vulkan driver. The observer fixes a borderless 1080p output with 2x
render scale. Caller stages libs/config in --base; never changes clock/thermal
policy. No GUI clicks. Resolution evidence is required before reporting FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import signal
import struct
import subprocess
import sys
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--renderer', choices=['moltenvk', 'kosmickrisp'], required=True)
    p.add_argument('--shader-dump', action='store_true')
    p.add_argument('--api-dump', action='store_true', help='Record initial Vulkan calls; diagnostic timing only')
    p.add_argument('--seconds', type=int, default=240)
    a = p.parse_args()
    assert 1 <= a.seconds <= 240
    assert subprocess.check_output(['sw_vers', '-buildVersion'], text=True).strip() == '25G83'
    assert subprocess.check_output(['sysctl', '-n', 'hw.model'], text=True).strip() == 'Mac16,12'
    os.chdir(a.base)
    sys.path.insert(0, '/private/var/tmp/m4-native-traces-20260910')
    from g16state import DevMem
    user = pwd.getpwuid(os.stat('/dev/console').st_uid)
    assert user.pw_uid != 0, 'A logged-in desktop is required'
    a.output.mkdir(parents=True, exist_ok=False)
    saved = a.output / 'saved'
    config = saved / 'config-0.10'
    shutil.copytree(a.base / 'config-0.10', config)
    shaders = a.output / 'shaders'
    shaders.mkdir()
    for path in [a.output, saved, config, shaders, *config.iterdir()]:
        os.chown(path, user.pw_uid, user.pw_gid)
    env = dict(SUPERTUXKART_SAVEDIR=str(saved), SDL_VIDEO_HIGHDPI_DISABLED='1',
               SDL_VIDEO_FULLSCREEN_DISPLAY='1', SDL_VIDEO_WINDOW_POS='-1920,0',
               VK_DRIVER_FILES=str(a.base / (a.renderer + '.json')),
               VK_ICD_FILENAMES=str(a.base / (a.renderer + '.json')),
               HOME=user.pw_dir, USER=user.pw_name, LOGNAME=user.pw_name,
               DYLD_INSERT_LIBRARIES=str(a.base/'stk-observe.dylib'))
    if a.shader_dump:
        env.update(MESA_KK_DEBUG='msl', MESA_SHADER_CACHE_DISABLE='true',
                   MVK_CONFIG_SHADER_DUMP_DIR=str(shaders), MVK_CONFIG_DEBUG='1',
                   STK_METAL_ARCHIVES=str(shaders), VK_LAYER_PATH=str(a.base/'layers'),
                   VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_screenshot',
                   VK_SCREENSHOT_FRAMES='600', VK_SCREENSHOT_DIR=str(a.output))
    if a.api_dump:
        env.update(VK_LAYER_PATH=str(a.base/'layers'),
                   VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_api_dump',
                   VK_APIDUMP_OUTPUT_RANGE='0', VK_APIDUMP_FLUSH='false')
    exe = a.base / 'SuperTuxKart.app/Contents/MacOS/supertuxkart'
    # The bundle's loader shim selects this explicit ICD. Documented separately.
    command = ['launchctl', 'asuser', str(user.pw_uid), 'sudo', '-u', user.pw_name,
               '/usr/bin/env', *[k+'='+v for k, v in env.items()], str(exe),
               '--benchmark', '--fullscreen', '--screensize=1920x1080', '--render-driver=vulkan']
    manifest = dict(command=command, exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                    renderer=a.renderer, shader_dump=a.shader_dump, api_dump=a.api_dump,
                    requested_internal=[3840, 2160], requested_output=[1920, 1080],
                    thermal_policy='Unmodified macOS native management; temperatures recorded without cutoffs',
                    boot_uuid=subprocess.check_output(['sysctl','-n','kern.bootsessionuuid'], text=True).strip())
    (a.output / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    memory = DevMem()
    # Qualified this exact boot/build: record table is 0x60 earlier than Linux.
    records = [(name, 0x685c4 + i * 0xa4) for i, name in enumerate(
        ['ta000m','te000m','te001m','tp000m','tp001m','tp002m'])]
    def sample():
        row = dict(monotonic_ns=time.monotonic_ns(), unix_ns=time.time_ns(), temperatures_c={}, raw={})
        for name, offset in records:
            for attempt in range(20):
                first = memory.read_mmio(0x380500000+offset, 0x44)
                second = memory.read_mmio(0x380500000+offset, 0x44)
                if first == second:
                    break
            else:
                raise RuntimeError('Unstable PMP record: '+name)
            assert first.split(b'\0', 1)[0].decode() == name, (name, first.hex())
            raw = struct.unpack_from('<i', first, 0x34)[0]
            floating = struct.unpack_from('<f', first, 0x38)[0]
            valid = struct.unpack_from('<I', first, 0x40)[0]
            assert valid in (0, 1)
            row['raw'][name] = dict(offset=offset, raw=raw, float_c=floating, valid=valid)
            if valid:
                assert abs(raw/64 - floating) < .02, 'PMP encodings disagree'
                row['temperatures_c'][name] = raw/64
        assert row['temperatures_c'], 'No valid PMP channels: '+json.dumps(row)
        return row
    # Check sensor identity and availability before launching any workload.
    initial = sample()
    (a.output/'initial-temperature.json').write_text(json.dumps(initial)+'\n')
    game = power = awake = None
    begin = time.monotonic()
    def stop(sig, frame):
        raise RuntimeError('interrupted: '+str(sig))
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        # Sleeping displays can change SDL's fullscreen drawable geometry.
        awake = subprocess.Popen(['caffeinate', '-u', '-d', '-t', str(a.seconds)],
                                 start_new_session=True)
        time.sleep(2)
        with (a.output/'game.log').open('w') as game_log, (a.output/'powermetrics.txt').open('w') as power_log, (a.output/'temperatures.jsonl').open('w') as temps:
            power = subprocess.Popen(['powermetrics', '--samplers', 'cpu_power,gpu_power,thermal',
                '-i', '500', '-n', str(a.seconds*2), '-b', '1'],
                stdout=power_log, stderr=subprocess.STDOUT, start_new_session=True)
            game = subprocess.Popen(command, stdout=game_log, stderr=subprocess.STDOUT, start_new_session=True, cwd=a.output)
            manifest.update(start_monotonic_ns=time.monotonic_ns(), start_unix_ns=time.time_ns(), pid=game.pid)
            (a.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
            captures = set()
            while game.poll() is None:
                elapsed = time.monotonic() - begin
                if elapsed > a.seconds:
                    raise TimeoutError('benchmark duration bound')
                assert power.poll() is None, 'powermetrics stopped'
                text = (a.output/'game.log').read_text(errors='replace')
                assert 'createVulkanDriver failed' not in text, 'Vulkan failed; refusing OpenGL fallback'
                row = sample()
                temps.write(json.dumps(row)+'\n'); temps.flush()
                for when in [5, 15, 30, 45]:
                    if elapsed >= when and when not in captures:
                        captures.add(when)
                        subprocess.run(['launchctl','asuser',str(user.pw_uid), 'sudo','-u',user.pw_name,
                            '/usr/sbin/screencapture','-x','-D','2',str(a.output/('race-%ds.png'%when))],
                            stdout=game_log, stderr=game_log, timeout=10)
                time.sleep(.05)
            assert game.returncode == 0, game.returncode
    finally:
        for process in [game, power, awake]:
            if process and process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL); process.wait(timeout=3)
        memory.close()
        manifest['end_monotonic_ns'] = time.monotonic_ns()
        (a.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    reports = list(config.glob('*.perf-report-black_forest.csv'))
    assert len(reports) == 1, reports
    lines = reports[0].read_text().splitlines()
    frames, milliseconds = map(int, lines[1].rstrip(',').split(','))
    values = next(l for l in lines if l.startswith('Values,')).split(',')
    text = (a.output/'game.log').read_text()
    assert float(values[3]) == 2, values
    swapchains = set(re.findall(r'STK Vulkan swapchain: (\d+)x(\d+)', text))
    assert swapchains == {('1920', '1080')}, swapchains
    images = set(re.findall(r'STK (?:Metal|Vulkan) image: (\d+)x(\d+)', text))
    if a.api_dump:
        for block in text.split('vkCreateImage(device,')[1:]:
            block = block.split('\nThread ', 1)[0]
            extent = re.search(r'extent:.*?width:\s+uint32_t = (\d+)\s+height:\s+uint32_t = (\d+)', block, re.S)
            if extent:
                images.add(extent.groups())
    assert ('3840', '2160') in images, images
    assert not images.intersection({('3420','2224'), ('3420','2044'), ('2940','1846'), ('7680','4320')}), images
    result = dict(frames=frames, seconds=milliseconds/1000, average_fps=frames*1000/milliseconds,
                  renderer=a.renderer, shader_dump=a.shader_dump, api_dump=a.api_dump, values=values, menus_excluded=True,
                  actual_swapchain=[1920,1080], actual_render_images=[3840,2160])
    (a.output/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
