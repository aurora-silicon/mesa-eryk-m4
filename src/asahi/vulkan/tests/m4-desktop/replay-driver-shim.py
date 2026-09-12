#!/usr/bin/env python3
"""Bounded offline compiler/command-recording replay; no GPU execution.

Uses the M4 noop DRM shim. Rebinding, offscreen swapchains and removal of
unsupported captured capabilities are explicit diagnostics, not app support.
"""
import argparse, hashlib, json, os, platform, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('capture',type=Path)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--graphics-feature-probe',action='store_true')
p.add_argument('--compiler-captures',type=Path,default=ROOT/'build/compiler-failures')
p.add_argument('--seconds',type=int,default=25,choices=range(1,61))
a=p.parse_args()
if platform.machine()!='x86_64':p.error('Host-only noop-shim replay')
a.output.mkdir(parents=True,exist_ok=False)
mesa=ROOT/'build/mesa-m4-asahi-host'
env=dict(os.environ)
for k in list(env):
 if k.startswith(('VK_','ASAHI_','AGX_','AGX3_','MESA_','HK_','GFXRECON_')) or k=='LD_PRELOAD':del env[k]
env.update(ASAHI_SHIM_CHIP='g16g',ASAHI_M4_EXPERIMENTAL='1',MESA_SHADER_CACHE_DISABLE='true',
 AGX_NATIVE_RESOURCE_DIAGNOSTICS='1',AGX_COMPILER_CAPTURE_DIR=str(a.compiler_captures.resolve()),
 LD_PRELOAD=str(mesa/'src/asahi/drm-shim/libasahi_noop_drm_shim.so'),
 VK_DRIVER_FILES=str(mesa/'src/asahi/vulkan/asahi_devenv_icd.x86_64.json'))
if a.graphics_feature_probe:env['ASAHI_M4_GRAPHICS_FEATURE_PROBE']='1'
cmd=[str(ROOT/'build/gfxreconstruct-host/tools/replay/gfxrecon-replay'),'--swapchain','offscreen','--memory-translation','rebind','--omit-pipeline-cache-data','--remove-unsupported',str(a.capture.resolve())]
repo=ROOT/'mesa/m4-asahi'
def git(*args):
 return subprocess.check_output(['git','-C',str(repo),*args])
def digest(path):
 with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
patch=git('diff','HEAD','--binary')
(a.output/'mesa.patch').write_bytes(patch)
provenance={'head':git('rev-parse','HEAD').decode().strip(),
 'status':git('status','--short').decode(),'patch_sha256':hashlib.sha256(patch).hexdigest(),
 'sha256':{str(f):digest(f) for f in [a.capture,Path(cmd[0]),
   mesa/'src/asahi/vulkan/libvulkan_asahi.so',
   mesa/'src/asahi/drm-shim/libasahi_noop_drm_shim.so']}}
(a.output/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
with (a.output/'replay.log').open('w') as log:
 try:code=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=a.seconds).returncode
 except subprocess.TimeoutExpired:code=124
lines=(a.output/'replay.log').read_text(errors='replace').splitlines()
interesting=[v for v in lines if any(x in v for x in ('encoding for','failure saved','FATAL','unsupported raw upload','shape refused','tile layout refused','VK_ERROR_FEATURE_NOT_PRESENT'))]
result={'returncode':code,'gpu_executed':False,'command':cmd,'environment':{k:v for k,v in env.items() if k.startswith(('VK_','ASAHI_','AGX_','MESA_')) or k=='LD_PRELOAD'},'diagnostics':interesting}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'returncode':code,'gpu_executed':False,'output':str(a.output),'diagnostics':interesting[-10:]},indent=2))
raise SystemExit(code)
