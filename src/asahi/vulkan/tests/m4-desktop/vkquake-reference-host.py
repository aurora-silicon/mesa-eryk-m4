#!/usr/bin/env python3
"""Capture a short shareware vkQuake map on x86 lavapipe, never on hardware.

Keeps exact binaries, data/configuration hashes, explicit capability restrictions
and a reference game screenshot. Hardware replays require gpu-experiment.py.
"""
import argparse, hashlib, json, os, platform, select, shutil, subprocess, time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--profile',type=Path,required=True)
p.add_argument('--seconds',type=int,default=12,choices=range(5,31))
p.add_argument('--samples',type=int,choices=[1,2,4],default=4,help='Explicit reference antialiasing setting; use negotiated native sample count')
a=p.parse_args()
assert platform.machine()=='x86_64'
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
base=out/'game';(base/'id1').mkdir(parents=True)
pak=ROOT/'build/quake-shareware/extracted/ID1/PAK0.PAK'
shutil.copyfile(pak,base/'id1/pak0.pak')
config=base/'id1/m4-reference.cfg'
config.write_text('map e1m1\n')
(base/'id1/autoexec.cfg').write_text('vid_vsync 0\nhost_maxfps 60\nexec m4-reference.cfg\n')
(base/'id1/vkQuake.cfg').write_text(f'vid_fsaa {a.samples}\n')
(base/'vkQuake.cfg').write_text(f'vid_fsaa {a.samples}\n')
(out/'initial-vkQuake.cfg').write_text(f'vid_fsaa {a.samples}\n')
mesa=ROOT/'build/mesa-m4-reference'
exe=ROOT/'build/vkquake-host/vkquake'
gfx=ROOT/'build/gfxreconstruct-host/layer/libVkLayer_gfxreconstruct.so'
prof=ROOT/'build/stk-reference-host/usr/lib/libVkLayer_khronos_profiles.so'
layers=out/'layers';layers.mkdir()
for source,library in [(ROOT/'build/gfxreconstruct-stage/share/vulkan/explicit_layer.d/VkLayer_gfxreconstruct.json',gfx),(ROOT/'build/stk-reference-host/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_profiles.json',prof)]:
    data=json.loads(source.read_text());data['layer']['library_path']=str(library)
    (layers/source.name).write_text(json.dumps(data))
shutil.copyfile(a.profile,out/'capture-profile.json')
report=json.loads(a.profile.with_suffix('.report.json').read_text())
(out/'capture-profile.report.json').write_text(json.dumps(report,indent=2))
env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','MESA_','AGX_','ASAHI_','HK_','GFXRECON_','LD_'))}
env.update(VK_DRIVER_FILES=str(mesa/'src/gallium/targets/lavapipe/lvp_devenv_icd.x86_64.json'),
    LP_NUM_THREADS='4',SDL_VIDEODRIVER='x11',XDG_DATA_HOME=str(out/'data'),XDG_CONFIG_HOME=str(out/'config'),HOME=str(out/'home'),
    VK_LAYER_PATH=str(layers),VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct:VK_LAYER_KHRONOS_profiles',
    VK_KHRONOS_PROFILES_PROFILE_FILE=str(out/'capture-profile.json'),
    VK_KHRONOS_PROFILES_SIMULATE_CAPABILITIES='SIMULATE_API_VERSION_BIT,SIMULATE_FEATURES_BIT,SIMULATE_EXTENSIONS_BIT,SIMULATE_PROPERTIES_BIT,SIMULATE_FORMATS_BIT',
    VK_KHRONOS_PROFILES_DEFAULT_FEATURE_VALUES='DEFAULT_FEATURE_VALUES_FALSE',
    VK_KHRONOS_PROFILES_EXCLUDE_DEVICE_EXTENSIONS=','.join(report['removed_reference_extensions']),
    GFXRECON_CAPTURE_FILE=str(out/'capture.gfxr'),GFXRECON_CAPTURE_FILE_TIMESTAMP='false',GFXRECON_CAPTURE_FILE_FLUSH='true')
cmd=[str(exe),'-basedir','.','-window','-width','800','-height','600','-nosound','-nojoy','-userdir','.','+vid_fsaa',str(a.samples),
     ]
def sha(f):
    with f.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
manifest=dict(command=cmd,cwd=str(base),hashes={str(f):sha(f) for f in [exe,pak,config,base/'id1/autoexec.cfg',base/'id1/vkQuake.cfg',gfx,prof,a.profile]},
              environment={k:v for k,v in env.items() if k.startswith(('VK_','GFXRECON_','LP_','SDL_','XDG_'))},
              adaptations=['x86 lavapipe/Xvfb reference','explicit capability intersection','800x600 window','shareware e1m1','no sound',f'explicit software reference sample count {a.samples}'])
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
server=game=None
try:
    r,w=os.pipe()
    with (out/'xvfb.log').open('w') as log:
        server=subprocess.Popen(['Xvfb','-displayfd',str(w),'-screen','0','1024x768x24','-nolisten','tcp'],pass_fds=(w,),stdout=log,stderr=log)
    os.close(w)
    if not select.select([r],[],[],8)[0]:raise TimeoutError('Xvfb startup')
    env['DISPLAY']=':'+os.read(r,100).decode().strip();os.close(r)
    with (out/'game.log').open('w') as log:
        game=subprocess.Popen(cmd,cwd=base,env=env,stdout=log,stderr=subprocess.STDOUT)
        time.sleep(a.seconds)
        if game.poll() is not None:raise RuntimeError(f'vkQuake exited {game.returncode}')
        subprocess.run(['import','-display',env['DISPLAY'],'-window','root',str(out/'reference.png')],check=True,timeout=5)
        (out/'game-maps.txt').write_text(Path(f'/proc/{game.pid}/maps').read_text())
        game.terminate();game.wait(timeout=4)
    print('PASS: software vkQuake capture completed:',out)
finally:
    for child in [game,server]:
        if child and child.poll() is None:
            child.terminate()
            try:child.wait(timeout=3)
            except subprocess.TimeoutExpired:child.kill();child.wait(timeout=3)
