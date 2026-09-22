#!/usr/bin/env python3
"""Identify the installed Zink renderer and enumerate a pinned GL CTS run plan.
Target only; run with gpu-experiment.py GUARD_OUTPUT. Saves ordinary desktop
renderer settings, binary hashes, QPA and Khronos runner's configuration plan.
"""
import argparse, hashlib, importlib.util, json, os, subprocess, sys
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',type=Path)
parser.add_argument('case',nargs='?')
parser.add_argument('--api',choices=['gl33','gl46','es32'],default='gl33')
parser.add_argument('--env', action='append', default=[])
parser.add_argument('--cases', type=Path, help='Headless-wrapper case identity')
parser.add_argument('--jobs', type=int, default=1)
parser.add_argument('--binary', default='/root/gl-cts-1.4.6.2/glcts')
a=parser.parse_args()
out=a.output; boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
spec=importlib.util.spec_from_file_location('fw','/root/firmware-snapshot.py'); fw=importlib.util.module_from_spec(spec); spec.loader.exec_module(fw)
fw.check_guard(out,boot)
env=None
for p in Path('/proc').glob('[0-9]*'):
 try:
  if (p/'comm').read_text().strip()=='Hyprland' and b'--watchdog-fd' in (p/'cmdline').read_bytes():
   env=dict(s.split('=',1) for s in (p/'environ').read_text().split('\0') if '=' in s); break
 except OSError: pass
assert env
for key in ('LD_PRELOAD','TRACE_FILE'): env.pop(key,None)
env.update(HOME='/root',USER='root',LOGNAME='root')
sockets=[s for s in Path(env['XDG_RUNTIME_DIR']).glob('wayland-*') if s.is_socket()]
assert len(sockets)==1
env['WAYLAND_DISPLAY']=sockets[0].name
for p in Path('/proc').glob('[0-9]*'):
 try:
  argv=(p/'cmdline').read_bytes().decode().split('\0')
  if argv and Path(argv[0]).name=='Xwayland':
   env['DISPLAY']=argv[1]
   if '-auth' in argv: env['XAUTHORITY']=argv[argv.index('-auth')+1]
 except OSError: pass
env.update(dict(item.split('=',1) for item in a.env))
assert env['GALLIUM_DRIVER']=='zink'
if a.api in ('gl46','es32'):
 for key in ('MESA_GL_VERSION_OVERRIDE','MESA_GLES_VERSION_OVERRIDE','MESA_GLSL_VERSION_OVERRIDE'):
  env.pop(key,None)
root=Path('/root/gl-cts-1.4.6.2')
record={k:v for k,v in env.items() if k.startswith(('GALLIUM','MESA','VK_','__EGL','LD_LIBRARY','LIBGL','ASAHI','HK_','WAYLAND','DISPLAY','XDG_RUNTIME'))}
(out/'renderer-environment.json').write_text(json.dumps(record,indent=2)+'\n')
(out/'binary-hashes.json').write_text(json.dumps({n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ('glcts','cts-runner')},indent=2)+'\n')
package = 'GLES32' if a.api == 'es32' else a.api.upper()
args=[str(root/'glcts'),'--deqp-case=KHR-'+package+'.info.*','--deqp-gl-context-type=egl','--deqp-egl-display-type=Wayland','--deqp-surface-width=64','--deqp-surface-height=64','--deqp-log-filename='+str(out/'info.qpa')]
subprocess.run(args,cwd=root,env=env,check=True,timeout=20)
fw.check_guard(out,boot)
if a.case:
 case=a.case
 assert case.startswith('KHR-NoContext.')
 direct=[a if not a.startswith('--deqp-case=') else '--deqp-case='+case for a in args]
 direct=[a if not a.startswith('--deqp-log-filename=') else '--deqp-log-filename='+str(out/'no-context.qpa') for a in direct]
 subprocess.run(direct,cwd=root,env=env,check=True,timeout=20)
 print('PASS: identified Zink and completed no-default-context case')
 raise SystemExit(0)
(out/'plan').mkdir()
result=subprocess.run([str(root/'cts-runner'),'--type='+a.api,'--summary','--logdir='+str(out/'plan')],cwd=root,env=env,check=False,timeout=20)
# --summary compares against absent result files and normally exits 1.
assert result.returncode in (0,1)
assert (out/'plan/cts-run-summary.xml').is_file()
assert 'zink Vulkan' in (out/'info.qpa').read_text() and 'Apple M4' in (out/'info.qpa').read_text()
print('PASS: '+a.api.upper()+' renderer identity and Khronos run plan saved')
