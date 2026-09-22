#!/usr/bin/env python3
"""Bounded surfaceless broadcast oracle using a specified candidate ICD."""
import hashlib,json,os,subprocess,sys,time
from pathlib import Path
out=Path(sys.argv[1]);icd=Path(sys.argv[2])
assert 'guard.py' in Path('/proc',str(os.getppid()),'cmdline').read_text()
assert json.loads((out/'result.json').read_text())['phase']=='thermal-policy-ready'
env=dict(os.environ)
for line in Path('/run/m3-gpu/graphics.env').read_text().splitlines():
    if line and not line.startswith('#'):k,v=line.split('=',1);env[k]=v
prefix=Path('/opt/asahi-m3-experiments/0ad-zink-present')
env.update(LD_LIBRARY_PATH=str(prefix/'lib')+':/opt/asahi-m3-wayland/lib',
    __EGL_VENDOR_LIBRARY_FILENAMES=str(prefix/'egl.json'),
    LIBGL_DRIVERS_PATH=str(prefix/'lib/dri'),VK_DRIVER_FILES=str(icd),
    HK_NATIVE_COALESCE_BROADCAST='true',HK_DEBUG_LOG='0',MESA_SHADER_CACHE_DISABLE='true')
for k in ('WAYLAND_DISPLAY','DISPLAY'):env.pop(k,None)
exe=Path('/root/m3-gpu-tools/desktop/gl-broadcast-probe')
lib=Path(json.loads(icd.read_text())['ICD']['library_path'])
(out/'identity.json').write_text(json.dumps(dict(environment={k:v for k,v in env.items() if k.startswith(('VK_','HK_','AGX_','MESA_','LD_','LIBGL_','GALLIUM_','__EGL_'))},sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (exe,lib,icd,Path(__file__))}),indent=2))
with (out/'probe.log').open('w') as log:
    start=time.monotonic();p=subprocess.run([str(exe)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=20)
(out/'probe-result.json').write_text(json.dumps(dict(returncode=p.returncode,seconds=time.monotonic()-start)))
p.check_returncode()
assert 'PASS:' in (out/'probe.log').read_text()
