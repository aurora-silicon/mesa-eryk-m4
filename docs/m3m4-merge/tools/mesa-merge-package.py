#!/usr/bin/env python3
"""Stage an immutable shared ARM64 Mesa package and record its exact identity.

Local only. Does not install on either target or switch desktop configuration.
Run from the workspace root after a successful full ARM64 build.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,default=Path('mesa/m3m4-merged'))
p.add_argument('--build',type=Path,default=Path('build/mesa-m3m4-arm64'))
p.add_argument('--evidence',type=Path,default=Path('build/m3m4-merge-20260923'))
a=p.parse_args()
assert not subprocess.check_output(['git','-C',str(a.source),'status','--porcelain']).strip(),'Commit the source first'
revision=subprocess.check_output(['git','-C',str(a.source),'rev-parse','HEAD'],text=True).strip()
build_info=json.loads((a.build/'meson-info/meson-info.json').read_text())
assert Path(build_info['directories']['source']).resolve()==a.source.resolve()
built_revision=re.search(r'git-([0-9a-f]+)',(a.build/'src/git_sha1.h').read_text())[1]
assert revision.startswith(built_revision),'Rebuild after committing before packaging'
short=revision[:11];prefix='/opt/asahi-m3m4-'+short
stage=(a.evidence/('stage-'+short)).resolve();assert not stage.exists()
options=json.loads((a.build/'meson-info/intro-buildoptions.json').read_text())
assert next(o['value'] for o in options if o['name']=='prefix')=='/opt/asahi-m3m4'
with (a.evidence/('install-'+short+'.log')).open('w') as log:
 subprocess.run(['meson','install','-C',str(a.build),'--no-rebuild','--destdir',str(stage)],check=True,stdout=log,stderr=subprocess.STDOUT)
package=stage/'opt/asahi-m3m4'
for name,library in [('asahi.json','libvulkan_asahi.so'),('egl.json','libEGL_mesa.so.0')]:
 icd={'library_path':prefix+'/lib/'+library}
 if name=='asahi.json':icd['api_version']='1.4.0'
 (package/name).write_text(json.dumps({'file_format_version':'1.0.0','ICD':icd},indent=2)+'\n')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
libraries={str(p.relative_to(package)):sha(p) for p in sorted((package/'lib').rglob('*')) if p.is_file() and not p.is_symlink() and ('.so.' in p.name or p.name.endswith('.so'))}
assert 'lib/libvulkan_asahi.so' in libraries and 'lib/libgallium-26.1.4.so' in libraries
manifest=dict(revision=revision,prefix=prefix,libraries=libraries,build_options=options,
              probes={p.name:sha(p) for p in sorted((a.evidence/'probes').iterdir()) if p.is_file()},
              note='Identical complete library tree for M3 and M4; device-specific runtime opt-in only.')
(a.evidence/('candidate-'+short+'.json')).write_text(json.dumps(manifest,indent=2)+'\n')
for chip in ['m3','m4']:
 env=dict(LD_LIBRARY_PATH=prefix+'/lib',LIBGL_DRIVERS_PATH=prefix+'/lib/dri',
          VK_DRIVER_FILES=prefix+'/asahi.json',__EGL_VENDOR_LIBRARY_FILENAMES=prefix+'/egl.json',
          MESA_LOADER_DRIVER_OVERRIDE='zink',GALLIUM_DRIVER='zink',AGX_MESA_DEBUG='nopromote',HK_DEBUG_LOG='0')
 env['ASAHI_'+chip.upper()+'_EXPERIMENTAL']='1'
 (a.evidence/(chip+'-'+short+'-environment.json')).write_text(json.dumps(env,indent=2)+'\n')
print(json.dumps(dict(revision=revision,prefix=prefix,stage=str(package),libraries=len(libraries))))
