#!/usr/bin/env python3
"""Select a verified versioned Mesa prefix for the normal desktop, or restore.

Target-side, configuration only: no GPU work, service restart or watchdog access.
Usage: mesa-merge-select.py install m3|m4|m3-game CANDIDATE_JSON NEW_BACKUP_DIRECTORY
       mesa-merge-select.py restore BACKUP_DIRECTORY
Restart/validate the desktop separately under the machine's existing guard.
"""
import hashlib,json,os,stat,sys
from pathlib import Path

def digest(data):return hashlib.sha256(data).hexdigest()
def replace(path,data,mode,uid,gid):
    temp=path.with_name(path.name+'.mesa-merge-tmp')
    with temp.open('xb') as f:
        f.write(data);f.flush();os.fsync(f.fileno())
    os.chmod(temp,mode);os.chown(temp,uid,gid);temp.replace(path)

if sys.argv[1]=='restore':
    backup=Path(sys.argv[2]);manifest=json.loads((backup/'selection.json').read_text())
    # Validate all records before changing any file, preserving later user edits.
    for record in manifest['files']:
        path=Path(record['path']);assert not path.is_symlink()
        # An interrupted install may have changed only part of the set.
        assert digest(path.read_bytes()) in (record['selected_sha256'],
                                            record['original_sha256']),str(path)
        assert digest((backup/record['backup']).read_bytes())==record['original_sha256']
    for record in manifest['files']:
        replace(Path(record['path']),(backup/record['backup']).read_bytes(),
                record['mode'],record['uid'],record['gid'])
    print('Restored recorded desktop configuration; restart under the existing guard.')
    raise SystemExit(0)

assert sys.argv[1]=='install' and sys.argv[2] in ('m3','m4','m3-game')
chip=sys.argv[2];candidate=json.loads(Path(sys.argv[3]).read_text());backup=Path(sys.argv[4])
prefix=Path(candidate['prefix']);assert prefix.is_absolute() and str(prefix).startswith('/opt/asahi-m3m4-')
for relative,sha in candidate['libraries'].items():
    assert not Path(relative).is_absolute() and '..' not in Path(relative).parts
    assert digest((prefix/relative).read_bytes())==sha,relative
for name,library in [('asahi.json','libvulkan_asahi.so'),('egl.json','libEGL_mesa.so.0')]:
    assert Path(json.loads((prefix/name).read_text())['ICD']['library_path']).resolve()==(prefix/'lib'/library).resolve()
values=dict(LD_LIBRARY_PATH=str(prefix/'lib'),LIBGL_DRIVERS_PATH=str(prefix/'lib/dri'),
            VK_DRIVER_FILES=str(prefix/'asahi.json'),
            __EGL_VENDOR_LIBRARY_FILENAMES=str(prefix/'egl.json'),
            GBM_BACKENDS_PATH=str(prefix/'lib/gbm'))
paths={'m3':[Path('/root/m3-gpu-tools/desktop/graphics.env'),Path('/run/m3-gpu/graphics.env')],
       'm4':[Path('/etc/environment.d/90-omarchy-j713-rendering.conf')],
       'm3-game':[Path('/opt/asahi-m3-0ad/graphics.env')]}[chip]
# The M3 game-specific prefix remains separate until its replay/real game gate
# passes; select it in a separately recorded deployment, never silently here.
backup.mkdir(parents=True,exist_ok=False)
manifest=dict(candidate=candidate,chip=chip,files=[])
for i,path in enumerate(paths):
    assert path.is_file() and not path.is_symlink()
    s=path.stat();original=path.read_bytes();lines=original.decode().splitlines();found=set();new=[]
    for line in lines:
        key=line.split('=',1)[0]
        if key in values:
            line=key+'='+values[key];found.add(key)
        elif key=='VK_ICD_FILENAMES':line=key+'='+values['VK_DRIVER_FILES']
        new.append(line)
    assert found >= set(values)-{'GBM_BACKENDS_PATH'},(path,found)
    # Relocated libgbm still has its build-prefix backend path compiled in.
    # M3's original install did not need this override; the versioned tree does.
    if 'GBM_BACKENDS_PATH' not in found:
        new.append('GBM_BACKENDS_PATH='+values['GBM_BACKENDS_PATH'])
    data=('\n'.join(new)+'\n').encode();name=f'{i:02d}-{path.name}'
    (backup/name).write_bytes(original);(backup/(name+'.selected')).write_bytes(data)
    manifest['files'].append(dict(path=str(path),backup=name,mode=stat.S_IMODE(s.st_mode),
                                 uid=s.st_uid,gid=s.st_gid,original_sha256=digest(original),
                                 selected_sha256=digest(data)))
(backup/'selection.json').write_text(json.dumps(manifest,indent=2)+'\n')
# All validation and rollback evidence is durable before the first change.
os.sync()
for record in manifest['files']:
    replace(Path(record['path']),(backup/(record['backup']+'.selected')).read_bytes(),
            record['mode'],record['uid'],record['gid'])
print('Selected '+str(prefix)+'; restart and verify mappings under the existing guard.')
