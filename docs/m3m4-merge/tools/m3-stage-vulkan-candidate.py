#!/usr/bin/env python3
"""Stage a hash-named M3 Vulkan candidate without changing desktop selection.

Reuse a qualified prefix's other libraries, record the local revision/patch,
and replace only Vulkan. This is not a complete Mesa package or validation.
All execution must subsequently use the existing watchdog/thermal guard.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--label', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--base', default='/opt/asahi-m3m4-e6a74082f1d')
p.add_argument('--library', type=Path, default=Path('build/mesa-m3m4-arm64/src/asahi/vulkan/libvulkan_asahi.so'))
p.add_argument('--source', type=Path, default=Path('mesa/m3m4-merged'))
a = p.parse_args()
assert a.label and all(c.islower() or c.isdigit() or c == '-' for c in a.label)
# The cross Ninja graph cannot rebuild executables in its separate host graph.
# A stale vtn_bindgen2 serializes old intrinsic numbers into libpoly/libagx,
# which can crash NIR deserialization after an intrinsic-table change.
if a.library == Path('build/mesa-m3m4-arm64/src/asahi/vulkan/libvulkan_asahi.so'):
    for build, targets in (
        ('build/mesa-m3m4-host', ['src/compiler/clc/mesa_clc',
          'src/compiler/spirv/vtn_bindgen2', 'src/asahi/clc/asahi_clc']),
        ('build/mesa-m3m4-arm64', ['src/asahi/vulkan/libvulkan_asahi.so']),
    ):
        subprocess.run(['ninja', '-j12', '-C', build, *targets], check=True)
a.output.mkdir(parents=True, exist_ok=False)
digest = hashlib.file_digest(a.library.open('rb'), 'sha256').hexdigest()
prefix = '/opt/asahi-m3-' + a.label + '-' + digest[:12]
revision = subprocess.check_output(['git', '-C', str(a.source), 'rev-parse', 'HEAD'], text=True).strip()
patch = subprocess.check_output(['git', '-C', str(a.source), 'diff', 'HEAD', '--binary'])
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
subprocess.run(['scp', str(a.library), 'root@fedora-m3:'+prefix+'/lib/libvulkan_asahi.so'],
               check=True, timeout=90)
libraries = json.loads(remote('''from pathlib import Path
import hashlib,json,sys
p=Path(sys.argv[1]);libs={str(f.relative_to(p)):hashlib.file_digest(f.open('rb'),'sha256').hexdigest()
 for f in (p/'lib').glob('*.so*') if f.is_file() and not f.is_symlink()}
assert libs['lib/libvulkan_asahi.so']==sys.argv[2]
(p/'CANDIDATE_INCOMPLETE').unlink();print(json.dumps(libs))
''', prefix, digest))
result = dict(prefix=prefix, sha256=digest, revision=revision, base=a.base,
              patch_sha256=hashlib.sha256(patch).hexdigest(), libraries=libraries,
              scope='Isolated Vulkan candidate, no desktop configuration change')
(a.output/'candidate.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result))
