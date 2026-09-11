from pathlib import Path
import subprocess, os, signal, re
assert not Path('/sys/module/g16_lab').exists()
output=Path(os.environ['M4_DESKTOP_EVIDENCE'])
before=subprocess.check_output(['dmesg','--color=never'],text=True)
kernel_log=(output/'kernel-live.log').open('w')
stream=subprocess.Popen(['dmesg','--color=never','--follow-new'],stdout=kernel_log)
profiler=None
profile_log=None
profile_env={**os.environ, 'LD_LIBRARY_PATH':'/opt/m4-profiling/usr/lib',
             'DEBUGINFOD_URLS':''}
perf='/opt/m4-profiling/usr/bin/perf'
try:
    if not Path('/sys/module/asahi').exists():
        subprocess.run(['modprobe','asahi'],check=True,timeout=25)
    nodes=[p for p in Path('/sys/class/drm').glob('renderD*') if (p/'device/driver').resolve().name == 'asahi']
    assert len(nodes)==1, nodes
    env=dict(os.environ, VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json', ASAHI_M4_EXPERIMENTAL='1', MESA_SHADER_CACHE_DISABLE='true', HK_G17P_LAUNCH_TRACE='0')
    env.update(LD_LIBRARY_PATH='/opt/asahi-m4-port/lib', LIBGL_DRIVERS_PATH='/opt/asahi-m4-port/lib/dri', GBM_BACKENDS_PATH='/opt/asahi-m4-port/lib/gbm', __EGL_VENDOR_LIBRARY_FILENAMES='/opt/asahi-m4-port/egl.json', MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink', ASAHI_MESA_DEBUG='nocompress', LIBGL_ALWAYS_SOFTWARE='0', MESA_GL_VERSION_OVERRIDE='3.3', MESA_GLES_VERSION_OVERRIDE='3.0')
    subprocess.run(['systemctl','stop','sddm'],check=True,timeout=5)
    subprocess.run(['modprobe','drm_dma_helper'],check=True,timeout=5)
    if not Path('/sys/module/m4_dcp_rtkit').exists():
        subprocess.run(['insmod','/root/m4_dcp_rtkit-asahi.ko','kernel_client=1','kms=1','takeover=1'],check=True,timeout=20)
    env.update(M4_RENDER_OUTPUT=str(output), M4_TEST_SUPERTUX='0' if any(os.environ.get(key)=='1' for key in ('M4_TEST_SUPERTUXKART','M4_TEST_0AD','M4_TEST_SAUERBRATEN','M4_TEST_SHELL')) else '1', M4_HYPR_NATIVE_KMS='1', M4_HYPR_PHYSICAL='1', M4_HYPR_USER='e', M4_HYPR_WIDTH='2560', M4_HYPR_HEIGHT='1664', PATH='/opt/m4-hyprland/bin:'+os.environ['PATH'])
    if os.environ.get('M4_DESKTOP_CPU_PROFILE')=='1':
        profile_log=(output/'perf-record.log').open('w')
        profiler=subprocess.Popen([perf,'record','-e','cpu-clock','-F','99',
                                   '-a','-g','-o',str(output/'perf.data')],
                                  env=profile_env,stdout=profile_log,stderr=subprocess.STDOUT)
    subprocess.run(['python3','/root/hyprland-validation/asahi-session.py'],env=env,check=True,timeout=int(os.environ.get('M4_HYPR_INTERACTIVE_SECONDS','0'))+(120 if any(os.environ.get(k)=='1' for k in ('M4_TEST_0AD','M4_TEST_SAUERBRATEN')) else 40))
finally:
    if profiler is not None:
        if profiler.poll() is None:
            profiler.send_signal(signal.SIGINT)
        profiler.wait(timeout=10)
        profile_log.close()
        # Decode offline after fetching. Symbol discovery must not prolong a
        # live hardware guard after the final GPU retirement.
    stream.terminate()
    stream.wait(timeout=3)
    kernel_log.close()
after=subprocess.check_output(['dmesg','--color=never'],text=True)
log=(output/'kernel-live.log').read_text()
print(log,flush=True)
for bad in ['Internal error:', 'BUG:', 'WARNING:', 'GPU retirement unproven', 'firmware crashed']:
    assert bad not in log, bad
assert 'userspace GPU job' in log
# Per-stage stamp logs are optional diagnostics. The retained timestamp record
# is emitted after both render stages retire; reject incomplete/zero pairs.
timings = re.findall(r'render GPU timestamps=\[\[([0-9a-f]+), ([0-9a-f]+)\], '
                    r'\[([0-9a-f]+), ([0-9a-f]+)\]\]', log)
assert any(all(t) and t[1] >= t[0] and t[3] >= t[2]
           for t in ([int(v, 16) for v in row] for row in timings)), 'No completed render timing pair'
print('PASS: shared-buffer Zink and native Hyprland pixels',flush=True)
