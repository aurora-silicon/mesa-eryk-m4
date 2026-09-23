#!/usr/bin/env python3
"""Observe one real application in the logged-in desktop under gpu-experiment.

Retains logs, loaded renderer mappings and native scanout at two points. Kills
only the launched process group afterwards. Screenshots and logs require review;
process survival alone does not establish correct GPU rendering.
"""
import argparse, hashlib, importlib.util, json, os, pwd, signal, stat, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--guard-output',type=Path,required=True)
p.add_argument('--seconds',type=int,default=15,choices=range(2,121))
p.add_argument('--capture',action='store_true')
p.add_argument('--headless', action='store_true', help='Use the enclosing private compositor environment and capture its output')
p.add_argument('--renderer', choices=['asahi','lavapipe'], default='asahi')
p.add_argument('--mesa-environment', type=Path,
               help='Explicit isolated Mesa environment for an Asahi candidate')
p.add_argument('--capture-profile',type=Path)
p.add_argument('--retain',type=Path,action='append',default=[])
p.add_argument('command',nargs=argparse.REMAINDER)
a=p.parse_args();command=a.command[1:] if a.command[:1]==['--'] else a.command
assert command and Path(command[0]).is_absolute()
spec=importlib.util.spec_from_file_location('fw',Path(__file__).with_name('firmware-snapshot.py'));fw=importlib.util.module_from_spec(spec);spec.loader.exec_module(fw)
boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
def check():fw.check_guard(a.guard_output,boot)
check();out=a.guard_output/'application';out.mkdir()
display_status={str(p):p.read_text() for p in
                {p.resolve() for p in Path('/sys/kernel/debug/dri').glob('*/m4_status')}}
(out/'display-before.json').write_text(json.dumps(display_status,indent=2)+'\n')
for status in display_status.values():
    fields=dict(line.split(maxsplit=1) for line in status.splitlines() if ' ' in line)
    if int(fields.get('failed','0')):
        raise RuntimeError('DCP has a prior failed flip; recover display output before a live application test')
user=pwd.getpwnam('e');runtime=Path(os.environ['XDG_RUNTIME_DIR']) if a.headless else Path('/run/user')/str(user.pw_uid)
sockets=[f for f in runtime.glob('wayland-*') if stat.S_ISSOCK(f.stat().st_mode)];assert len(sockets)==1
scratch=Path(user.pw_dir)/'.cache/m4-app-probes'/a.guard_output.name;scratch.mkdir(parents=True)
for f in [scratch.parent,scratch]:os.chown(f,user.pw_uid,user.pw_gid)
env=dict(os.environ)
for k in list(env):
    if k.startswith(('VK_','GFXRECON_','AGX_','HK_','LD_PRELOAD')):env.pop(k)
for line in Path('/etc/environment.d/90-omarchy-j713-rendering.conf').read_text().splitlines():
    if line and not line.startswith('#') and '=' in line:k,v=line.split('=',1);env[k]=v
env.update(HOME=user.pw_dir,USER=user.pw_name,LOGNAME=user.pw_name,XDG_RUNTIME_DIR=str(runtime),WAYLAND_DISPLAY=sockets[0].name,DBUS_SESSION_BUS_ADDRESS=f'unix:path={runtime}/bus',MESA_SHADER_CACHE_DIR=str(scratch/'mesa-cache'),AGX_COMPILER_CAPTURE_DIR=str(scratch/'compiler'),PYTHONUNBUFFERED='1')
env.update(VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json' if a.renderer=='asahi' else '/usr/share/vulkan/icd.d/lvp_icd.json',LIBGL_ALWAYS_SOFTWARE='0' if a.renderer=='asahi' else '1',LP_NUM_THREADS='4')
mesa_libraries = [Path('/opt/asahi-m4-port/lib/libvulkan_asahi.so'),
                  Path('/opt/asahi-m4-port/lib/libgallium-26.1.4.so')]
if a.mesa_environment:
    assert a.renderer == 'asahi'
    candidate = json.loads(a.mesa_environment.read_text())
    assert all(isinstance(k, str) and isinstance(v, str) for k,v in candidate.items())
    assert all(k.startswith(('VK_', 'MESA_', 'AGX_', 'GALLIUM_', 'LIBGL_',
                             'LD_LIBRARY_', '__EGL_', 'ASAHI_', 'HK_')) for k in candidate)
    env.update(candidate)
    prefix = Path(env['LD_LIBRARY_PATH'])
    assert prefix.is_absolute() and prefix.is_dir()
    mesa_libraries = [prefix/'libvulkan_asahi.so', prefix/'libgallium-26.1.4.so']
    (out/'candidate-environment.json').write_bytes(a.mesa_environment.read_bytes())
(scratch/'compiler').mkdir();os.chown(scratch/'compiler',user.pw_uid,user.pw_gid)
if a.capture:
    env.update(VK_LAYER_PATH='/opt/gfxreconstruct/share/vulkan/explicit_layer.d',VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',GFXRECON_CAPTURE_FILE=str(scratch/'app-${ProcessId}.gfxr'),GFXRECON_CAPTURE_FILE_TIMESTAMP='false',GFXRECON_CAPTURE_FILE_FLUSH='true')
if a.capture_profile:
    assert a.capture and a.renderer=='lavapipe'
    report=json.loads(a.capture_profile.with_suffix('.report.json').read_text())
    (out/'capture-profile.json').write_bytes(a.capture_profile.read_bytes())
    (out/'capture-profile.report.json').write_bytes(a.capture_profile.with_suffix('.report.json').read_bytes())
    env.update(VK_LAYER_PATH=env['VK_LAYER_PATH']+':/usr/share/vulkan/explicit_layer.d',VK_INSTANCE_LAYERS=env['VK_INSTANCE_LAYERS']+':VK_LAYER_KHRONOS_profiles',VK_KHRONOS_PROFILES_PROFILE_FILE=str(a.capture_profile),VK_KHRONOS_PROFILES_SIMULATE_CAPABILITIES='SIMULATE_API_VERSION_BIT,SIMULATE_FEATURES_BIT,SIMULATE_EXTENSIONS_BIT,SIMULATE_PROPERTIES_BIT,SIMULATE_FORMATS_BIT',VK_KHRONOS_PROFILES_DEFAULT_FEATURE_VALUES='DEFAULT_FEATURE_VALUES_FALSE',VK_KHRONOS_PROFILES_EXCLUDE_DEVICE_EXTENSIONS=','.join(report['removed_reference_extensions']))
manifest=dict(renderer=a.renderer,command=command,boot_id=boot,environment={k:v for k,v in env.items() if k.startswith(('VK_','MESA_','AGX_','GALLIUM_','LIBGL_','GFXRECON_','LD_LIBRARY_','ASAHI_'))},hashes={})
for f in [Path(command[0]),*mesa_libraries,*a.retain]:
    manifest['hashes'][str(f)]=hashlib.sha256(f.read_bytes()).hexdigest()
for i,f in enumerate(a.retain):(out/f'config-{i}-{f.name}').write_bytes(f.read_bytes())
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
child=None;result={}
def stop_test_processes():
    # Session leaders may exit while GNOME/D-Bus descendants ignore SIGTERM.
    # Match the exact per-run cache path, and hold pidfds to avoid PID reuse.
    marker=('MESA_SHADER_CACHE_DIR='+str(scratch/'mesa-cache')).encode()
    fds=[]
    for proc in Path('/proc').glob('[0-9]*'):
        fd=None
        try:
            fd=os.pidfd_open(int(proc.name))
            if marker in (proc/'environ').read_bytes().split(b'\0'):
                fds.append(fd);fd=None
        except (OSError,ProcessLookupError):pass
        finally:
            if fd is not None:os.close(fd)
    try:
        for sig in (signal.SIGTERM,signal.SIGKILL):
            for fd in fds:
                try:signal.pidfd_send_signal(fd,sig)
                except ProcessLookupError:pass
            if sig==signal.SIGTERM:time.sleep(1)
    finally:
        for fd in fds:os.close(fd)
def interrupt(signum,frame):raise InterruptedError(f'signal {signum}')
signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt)
try:
    with (out/'application.log').open('w') as log:
        child=subprocess.Popen(['runuser','-u',user.pw_name,'--',*command],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        started=time.monotonic();snapshots=[min(5,a.seconds/2),a.seconds]
        while snapshots:
            check()
            if child.poll() is not None:raise RuntimeError(f'application exited {child.returncode}')
            elapsed=time.monotonic()-started
            if elapsed>=snapshots[0]:
                stamp=snapshots.pop(0)
                if a.headless:
                    screenshot = subprocess.run(['runuser','-u',user.pw_name,'--','grim','-o','HEADLESS-1','-'],env=env,capture_output=True,check=True,timeout=5)
                    (out/f'screen-{stamp:g}.png').write_bytes(screenshot.stdout)
                else:
                    nodes={f.resolve() for f in Path('/sys/kernel/debug/dri').glob('*/m4_scanout')};assert len(nodes)==1
                    (out/f'screen-{stamp:g}.m4fb').write_bytes(nodes.pop().read_bytes())
                for proc in Path('/proc').glob('[0-9]*'):
                    try:
                        if os.getpgid(int(proc.name))==child.pid:
                            (out/f'{proc.name}-maps.txt').write_text((proc/'maps').read_text())
                    except (OSError,ProcessLookupError):pass
            time.sleep(.2)
        result.update(observed_seconds=time.monotonic()-started,alive_at_end=True)
finally:
    if child and child.poll() is None:
        os.killpg(child.pid,signal.SIGTERM)
        try:child.wait(timeout=3)
        except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
    stop_test_processes()
    import shutil
    shutil.copytree(scratch,out/'diagnostics')
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    if (out/'application.log').exists():print((out/'application.log').read_text(errors='replace')[-6000:])
    os.sync()
print('PASS: bounded real application observation completed; inspect images and renderer evidence')
