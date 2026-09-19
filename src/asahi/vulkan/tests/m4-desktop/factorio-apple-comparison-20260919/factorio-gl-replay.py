#!/usr/bin/env python3
"""Target-only bounded OpenGL replay. Requires gpu-experiment watchdog/thermals.

Omitting unsupported fragment statistics is an explicit comparison adaptation;
no rendering calls are omitted. Save screenshots separately from timing runs.
"""
import argparse, hashlib, json, os, re, subprocess, time, tempfile, shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);p.add_argument('--trace',type=Path,required=True);p.add_argument('--ignore-statistics',type=Path,required=True);p.add_argument('--snapshot',type=int);p.add_argument('--profile',action='store_true');p.add_argument('--call-cost-range',help='Inclusive call interval FIRST-LAST; opt-in diagnostic overhead');p.add_argument('--predecode-range',help='Predecode inclusive FIRST-LAST before executing FIRST; include preceding swap');a=p.parse_args()
g=json.loads((a.output/'start.json').read_text());assert g['watchdog']['armed'] and g['watchdog']['timeout_seconds']==60
sessions=[]
for proc in Path('/proc').glob('[0-9]*'):
 try:
  if proc.stat().st_uid==1000 and (proc/'exe').resolve().name=='Hyprland':sessions.append(proc)
 except OSError:pass
assert len(sessions)==1
env=dict(x.split('=',1) for x in (sessions[0]/'environ').read_text().split('\0') if '=' in x)
for proc in Path('/proc').glob('[0-9]*'):
 try:
  argv=(proc/'cmdline').read_bytes().split(b'\0')
  if argv[0]==b'Xwayland' and proc.stat().st_uid==1000:
   env['DISPLAY']=argv[1].decode()
   if b'-auth' in argv:env['XAUTHORITY']=argv[argv.index(b'-auth')+1].decode()
 except OSError:pass
profile=json.loads(Path('/home/e/.local/share/factorio-driver/profile/environment.json').read_text())
for k,v in profile.items():
 if k.startswith(('HK_','MESA_','AGX_','ASAHI_','LIBGL_','__GLX_','__EGL_')):env[k]=v
for k in list(env):
 if k.startswith(('GALLIUM_HUD','TRACE_','GFXRECON_')) or k in ('LD_PRELOAD','MESA_EXTENSION_OVERRIDE','MESA_GL_VERSION_OVERRIDE','MESA_GLES_VERSION_OVERRIDE'):env.pop(k)
env.update(LD_LIBRARY_PATH='/opt/asahi-m4-port/lib:/opt/m4-apitrace/usr/lib',VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json',GALLIUM_DRIVER='zink',MESA_LOADER_DRIVER_OVERRIDE='zink',ASAHI_MESA_DEBUG='',MESA_SHADER_CACHE_DISABLE='false',vblank_mode='0',EGL_PLATFORM='surfaceless')
ignore=a.ignore_statistics.read_text().strip();assert re.fullmatch(r'\d+(,\d+)*',ignore)
scratch=Path(tempfile.mkdtemp(prefix='factorio-gl-',dir='/home/e/.cache'));os.chown(scratch,1000,1000)
cmd=['/home/e/.local/share/factorio-driver/gl-comparison-20260919/eglretrace-comparison','--headless','--core','--ignore-calls='+ignore]
if a.snapshot is not None:cmd+=['-s',str(scratch/'frame-'),'-S',str(a.snapshot)]
if a.profile:
 cmd+=['-b']
 env['APITRACE_FRAME_COMPLETION_TIMING']='1'
env['APITRACE_FACTORIO_COPY_COMPAT']='1'
if a.call_cost_range:
 assert re.fullmatch(r'\d+-\d+',a.call_cost_range)
 env['APITRACE_CALL_COST_RANGE']=a.call_cost_range
if a.predecode_range:
 assert re.fullmatch(r'\d+-\d+',a.predecode_range)
 env['APITRACE_PREDECODE_RANGE']=a.predecode_range
cmd+=[str(a.trace)]
manifest=dict(trace=str(a.trace),sha256=hashlib.file_digest(a.trace.open('rb'),'sha256').hexdigest(),replay_binary_sha256=hashlib.file_digest(Path(cmd[0]).open('rb'),'sha256').hexdigest(),ignored_statistic_calls=ignore,environment={k:v for k,v in env.items() if k.startswith(('HK_','MESA_','ASAHI_','VK_','GALLIUM_','LIBGL_','APITRACE_'))},command=cmd)
(a.output/'gl-replay.json').write_text(json.dumps(manifest,indent=2)+'\n')
try:
 with (a.output/'gl-replay.log').open('w') as log:
  r=subprocess.run(['runuser','-u','e','--','env',*[k+'='+v for k,v in env.items()],*cmd],env=env,cwd=scratch,stdout=log,stderr=subprocess.STDOUT,timeout=min(85,g['seconds']-15))
finally:
 for image in scratch.glob('*.png'):shutil.copyfile(image,a.output/image.name)
 shutil.rmtree(scratch)
r.check_returncode()
text=(a.output/'gl-replay.log').read_text(errors='replace');errors=[l for l in text.splitlines() if re.search(r'GL_INVALID|error:|failed|unsupported',l,re.I)]
if errors:(a.output/'gl-errors.txt').write_text('\n'.join(errors)+'\n');raise RuntimeError('Replay API/shader errors; do not accept timings')
if a.snapshot is not None:assert (a.output/f'frame-{a.snapshot:010d}.png').exists()
print('PASS: bounded OpenGL replay, no API/shader errors',flush=True)
