#!/usr/bin/env python3
"""Bounded x86 software-only STK Vulkan reference with an isolated Xvfb display.

Requires an existing local Zink/lavapipe build. This cannot launch on J713 and
never substitutes for guarded hardware replay. Write ordered JSON requests to
OUTPUT/requests/N.json: {"action":"key","key":"Return"}, snapshot, or finish.
Each input retains its request and one image. Review transitions before sending
more input. All rendering settings are copied intact from the supplied profile.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--config',type=Path,required=True,help='Original config-0.10 directory')
p.add_argument('--profile',type=Path,required=True,help='Explicit software/M4 Vulkan capability intersection')
p.add_argument('--seconds',type=int,default=300,choices=range(1,601))
a=p.parse_args()
if platform.machine() != 'x86_64':
    p.error('This helper is only for the x86 software reference; J713 requires gpu-experiment.py')
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
requests=out/'requests';requests.mkdir()
mesa=ROOT/'build/mesa-m4-reference';gfx=ROOT/'build/gfxreconstruct-host';pkg=ROOT/'build/stk-reference-host'
binary=pkg/'usr/bin/supertuxkart'
saved=out/'profile';(saved/'config-0.10').mkdir(parents=True)
for f in a.config.glob('*.xml'):
    shutil.copy2(f,saved/'config-0.10'/f.name)
shutil.copytree(saved,out/'original-profile')
shutil.copy2(a.profile,out/'capture-profile.json')
layers=out/'layers';layers.mkdir()
for source,library in [(ROOT/'build/gfxreconstruct-stage/share/vulkan/explicit_layer.d/VkLayer_gfxreconstruct.json',gfx/'layer/libVkLayer_gfxreconstruct.so'),
                       (pkg/'usr/share/vulkan/explicit_layer.d/VkLayer_khronos_profiles.json',pkg/'usr/lib/libVkLayer_khronos_profiles.so')]:
    data=json.loads(source.read_text());data['layer']['library_path']=str(library)
    (layers/source.name).write_text(json.dumps(data,indent=2)+'\n')
env=dict(os.environ)
for key in list(env):
    if key.startswith(('VK_','MESA_','GFXRECON_','AGX_','ASAHI_','HK_','ZINK_')) or key=='LD_PRELOAD':del env[key]
env.update(VK_DRIVER_FILES=str(mesa/'src/gallium/targets/lavapipe/lvp_devenv_icd.x86_64.json'),
    LD_LIBRARY_PATH=':'.join(str(mesa/x) for x in ['src/glx','src/egl','src/gbm','src/gallium/targets/dri','src/gallium/targets/lavapipe']),
    __EGL_VENDOR_LIBRARY_FILENAMES=str(mesa/'src/egl/devenv_glvnd_egl.json'),
    GALLIUM_DRIVER='zink',MESA_LOADER_DRIVER_OVERRIDE='zink',LIBGL_ALWAYS_SOFTWARE='1',
    MESA_GL_VERSION_OVERRIDE='3.3',MESA_GLES_VERSION_OVERRIDE='3.0',
    MESA_SHADER_CACHE_DIR=str(out/'mesa-cache'),XDG_CACHE_HOME=str(out/'cache'),LP_NUM_THREADS='4',
    SUPERTUXKART_DATADIR=str(pkg/'usr/share/supertuxkart'),SUPERTUXKART_SAVEDIR=str(saved),
    SDL_VIDEODRIVER='x11',ALSOFT_DRIVERS='null',
    VK_LAYER_PATH=str(layers),VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct:VK_LAYER_KHRONOS_profiles',
    VK_KHRONOS_PROFILES_PROFILE_FILE=str(out/'capture-profile.json'),
    VK_KHRONOS_PROFILES_SIMULATE_CAPABILITIES='SIMULATE_API_VERSION_BIT,SIMULATE_FEATURES_BIT,SIMULATE_EXTENSIONS_BIT,SIMULATE_PROPERTIES_BIT,SIMULATE_FORMATS_BIT',
    VK_KHRONOS_PROFILES_DEFAULT_FEATURE_VALUES='DEFAULT_FEATURE_VALUES_FALSE',
    GFXRECON_CAPTURE_FILE=str(out/'capture.gfxr'),GFXRECON_CAPTURE_FILE_TIMESTAMP='false',GFXRECON_CAPTURE_FILE_FLUSH='true')
files=[binary,gfx/'layer/libVkLayer_gfxreconstruct.so',pkg/'usr/lib/libVkLayer_khronos_profiles.so',
       mesa/'src/gallium/targets/dri/libgallium-26.1.4.so',mesa/'src/gallium/targets/lavapipe/libvulkan_lvp.so',a.profile,Path(__file__)]
def sha(f):
    with f.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
manifest=dict(architecture=platform.machine(),renderer='Zink/lavapipe software reference only',
    mesa_commit=subprocess.check_output(['git','-C',str(ROOT/'mesa/m4-asahi'),'rev-parse','HEAD'],text=True).strip(),
    hashes={str(f):sha(f) for f in files},environment=env,
    adaptations=['x86 host with X11/Xvfb instead of J713 Wayland','Explicit Vulkan capability profile','Null audio output'],
    graphics_config_changed=False,seconds=a.seconds)
# Do not save unrelated inherited environment values.
manifest['environment']={k:v for k,v in env.items() if k.startswith(('VK_','GFXRECON_','MESA_','GALLIUM_','LIBGL_','LP_','LD_LIBRARY_','SUPERTUXKART_','SDL_','ALSOFT_','__EGL_'))}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
server=game=None
result=dict(started=time.monotonic(),completed=False)
def interrupted(signum,frame):raise InterruptedError(f'Signal {signum}')
signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGINT,interrupted)
try:
    readfd,writefd=os.pipe()
    with (out/'xvfb.log').open('w') as log:
        server=subprocess.Popen(['Xvfb','-displayfd',str(writefd),'-screen','0','2560x1664x24','-nolisten','tcp'],pass_fds=(writefd,),stdout=log,stderr=log)
    os.close(writefd)
    import select
    if not select.select([readfd],[],[],8)[0]:raise TimeoutError('Xvfb startup')
    env['DISPLAY']=':'+os.read(readfd,100).decode().strip();os.close(readfd)
    with (out/'game.log').open('w') as log:
        game=subprocess.Popen([str(binary),'--seed=1'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    (out/'ready.json').write_text(json.dumps(dict(display=env['DISPLAY'],pid=game.pid))+'\n')
    print('Ready: '+str(out),flush=True)
    seen=set()
    while game.poll() is None:
        if time.monotonic()-result['started']>a.seconds:raise TimeoutError('Reference deadline')
        for request in sorted(requests.glob('*.json')):
            if request.name in seen:continue
            seen.add(request.name);data=json.loads(request.read_text());action=data['action']
            if action=='finish':
                result['completed']=True
                break
            if action=='key':
                subprocess.run(['xdotool','key','--clearmodifiers',data['key']],env=env,check=True,timeout=3)
                time.sleep(1)
            elif action=='click':
                subprocess.run(['xdotool','mousemove',str(int(data['x'])),str(int(data['y'])),'click','1'],env=env,check=True,timeout=3)
                time.sleep(1)
            elif action!='snapshot':raise ValueError('Unknown action')
            image=out/(request.stem+'.png')
            subprocess.run(['import','-display',env['DISPLAY'],'-window','root',str(image)],check=True,timeout=5)
            print(json.dumps(dict(request=request.name,image=str(image),time=time.monotonic())),flush=True)
        if result['completed']:break
        if server.poll() is not None:raise RuntimeError('Xvfb stopped')
        time.sleep(.1)
    result['game_returncode']=game.poll()
except BaseException as error:
    result['error']=repr(error)
    raise
finally:
    if game and Path(f'/proc/{game.pid}/maps').exists():
        (out/'game-maps.txt').write_text(Path(f'/proc/{game.pid}/maps').read_text())
    if game and game.poll() is None:
        os.killpg(game.pid,signal.SIGTERM)
        try:game.wait(timeout=4)
        except subprocess.TimeoutExpired:os.killpg(game.pid,signal.SIGKILL);game.wait(timeout=3)
    if server and server.poll() is None:server.terminate();server.wait(timeout=3)
    result['ended']=time.monotonic()
    trace=out/'capture.gfxr'
    if trace.exists():result['trace_sha256']=sha(trace)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    os.sync()
print('Reference capture closed; race and replay validation remain separate.',flush=True)
