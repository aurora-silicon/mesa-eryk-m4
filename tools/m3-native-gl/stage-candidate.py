#!/usr/bin/env python3
"""Stage a hash-named M3 Vulkan or Gallium candidate without desktop selection.

Reuse a qualified prefix's other libraries, record the local revision/patch,
and replace only the selected library. This is not a complete Mesa package or validation.
All execution must subsequently use the existing watchdog/thermal guard.
For Zink: --component gallium; all qualified Vulkan/EGL/GLX/GBM libraries stay.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

def source_patch(source):
    """Include new build sources without mutating the developer's Git index."""
    patch = subprocess.check_output(['git', '-C', str(source), 'diff', 'HEAD', '--binary'])
    names = subprocess.check_output(['git', '-C', str(source), 'ls-files',
        '--others', '--exclude-standard', '-z'], text=True).split('\0')
    extra = []
    for name in filter(None, names):
        path = Path(name)
        if not (name == 'meson.build' or
                (path.parts[0] in ('src', 'include') and
                 (path.name == 'meson.build' or path.suffix in
                  ('.c', '.h', '.cpp', '.cxx', '.cl', '.py', '.glsl', '.vert', '.frag', '.comp',
                   '.xml', '.json', '.yaml', '.yml', '.inc', '.def', '.rs', '.S')))):
            continue
        result = subprocess.run(['git', 'diff', '--no-index', '--binary', '--',
            '/dev/null', name], cwd=source, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=False)
        if result.returncode != 1:
            raise RuntimeError('Cannot record new build source: ' + name)
        patch += result.stdout
        extra.append(dict(path=name, sha256=hashlib.sha256((source/path).read_bytes()).hexdigest()))
    return patch, extra

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--label', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--base', default='/opt/asahi-m3m4-e6a74082f1d')
p.add_argument('--component', choices=('vulkan', 'gallium'), default='vulkan')
p.add_argument('--library', type=Path, help='Already-built library; omit for an incremental build')
p.add_argument('--dri-shim', type=Path, help='Matching DRIL shim when adding a Gallium driver')
p.add_argument('--source', type=Path, default=Path('mesa/m3m4-merged'))
a = p.parse_args()
assert a.label and all(c.islower() or c.isdigit() or c == '-' for c in a.label)
# The cross Ninja graph cannot rebuild executables in its separate host graph.
# A stale vtn_bindgen2 serializes old intrinsic numbers into libpoly/libagx,
# which can crash NIR deserialization after an intrinsic-table change.
relative = ('src/asahi/vulkan/libvulkan_asahi.so' if a.component == 'vulkan'
            else 'src/gallium/targets/dri/libgallium-26.1.4.so')
library_name = Path(relative).name
if a.library is None:
    a.library = Path('build/mesa-m3m4-arm64') / relative
    for build, targets in (
        ('build/mesa-m3m4-host', ['src/compiler/clc/mesa_clc',
          'src/compiler/spirv/vtn_bindgen2', 'src/asahi/clc/asahi_clc']),
        ('build/mesa-m3m4-arm64', [relative]),
    ):
        subprocess.run(['ninja', '-j12', '-C', build, *targets], check=True)
a.output.mkdir(parents=True, exist_ok=False)
digest = hashlib.file_digest(a.library.open('rb'), 'sha256').hexdigest()
prefix = '/opt/asahi-m3-' + a.label + '-' + digest[:12]
revision = subprocess.check_output(['git', '-C', str(a.source), 'rev-parse', 'HEAD'], text=True).strip()
patch, extra_sources = source_patch(a.source)
(a.output/'source.patch').write_bytes(patch)

def remote(code, *args):
    return subprocess.check_output(['ssh', '-oConnectTimeout=10', 'root@fedora-m3',
        shlex.join(['python3', '-c', code, *args])], text=True, timeout=60)

remote('''from pathlib import Path
import shutil,sys
s,d=map(Path,sys.argv[1:]);assert not d.exists()
assert s.is_dir() and not (s/'CANDIDATE_INCOMPLETE').exists()
d.mkdir();(d/'CANDIDATE_INCOMPLETE').touch()
shutil.copytree(s,d,symlinks=True,dirs_exist_ok=True)
for name in ('asahi.json','egl.json'):
 p=d/name;p.write_text(p.read_text().replace(str(s),str(d)))
''', a.base, prefix)
subprocess.run(['scp', str(a.library), 'root@fedora-m3:'+prefix+'/lib/'+library_name],
               check=True, timeout=90)
if a.dri_shim:
    subprocess.run(['scp', str(a.dri_shim), 'root@fedora-m3:'+prefix+'/lib/dri/libdril_dri.so'], check=True, timeout=90)
    remote('''from pathlib import Path
import sys
p=Path(sys.argv[1]);(p/'lib/dri/asahi_dri.so').symlink_to('libdril_dri.so')
''', prefix)
libraries = json.loads(remote('''from pathlib import Path
import hashlib,json,sys
p=Path(sys.argv[1]);libs={str(f.relative_to(p)):hashlib.file_digest(f.open('rb'),'sha256').hexdigest()
 for f in (p/'lib').rglob('*.so*') if f.is_file() and not f.is_symlink()}
assert libs['lib/'+sys.argv[3]]==sys.argv[2]
(p/'CANDIDATE_INCOMPLETE').unlink();print(json.dumps(libs))
''', prefix, digest, library_name))
result = dict(prefix=prefix, sha256=digest, revision=revision, base=a.base,
              patch_sha256=hashlib.sha256(patch).hexdigest(), libraries=libraries,
              untracked_build_sources=extra_sources,
              component=a.component,
              gallium_sha256=libraries['lib/libgallium-26.1.4.so'],
              vulkan_sha256=libraries['lib/libvulkan_asahi.so'],
              scope='Isolated '+a.component+' candidate, no desktop configuration change')
(a.output/'candidate.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result))
