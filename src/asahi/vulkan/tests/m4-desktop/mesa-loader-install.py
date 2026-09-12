#!/usr/bin/env python3
"""Give private Mesa DSOs relative RUNPATHs for capability-bearing clients.

KWin's file capability clears LD_LIBRARY_PATH. Patch only private Mesa's EGL,
DRI and GBM loaders, retain byte-exact backups, verify checksums and install
atomically. Existing processes retain their mappings. Reapply after replacing
these loaders. No system library search priority is changed.
"""
import hashlib,json,shlex,subprocess
from pathlib import Path
p=Path('_provenance/mesa-loader-runpath-20260912');p.mkdir(exist_ok=False)
files={'lib/libEGL_mesa.so.0':'$ORIGIN','lib/gbm/dri_gbm.so':'$ORIGIN/..','lib/dri/libdril_dri.so':'$ORIGIN/..'}
records=[]
for i,(relative,rpath) in enumerate(files.items()):
 remote='/opt/asahi-m4-port/'+relative
 real=subprocess.check_output(['ssh','root@omarchy-m4','readlink -f '+shlex.quote(remote)],text=True).strip()
 before=p/f'{i}-before.so';after=p/f'{i}-after.so'
 subprocess.run(['scp','root@omarchy-m4:'+real,str(before)],check=True)
 after.write_bytes(before.read_bytes())
 subprocess.run(['patchelf','--set-rpath',rpath,str(after)],check=True)
 old=hashlib.sha256(before.read_bytes()).hexdigest();new=hashlib.sha256(after.read_bytes()).hexdigest()
 staged='/root/mesa-loader-'+new+'.so'
 subprocess.run(['scp',str(after),'root@omarchy-m4:'+staged],check=True)
 code='''import hashlib,os,pathlib,shutil,sys
p,s,old,new=map(str,sys.argv[1:]);p=pathlib.Path(p);s=pathlib.Path(s)
assert hashlib.sha256(p.read_bytes()).hexdigest()==old
assert hashlib.sha256(s.read_bytes()).hexdigest()==new
backup=p.with_name(p.name+'.before-runpath-'+old)
if not backup.exists():shutil.copy2(p,backup)
tmp=p.with_name(p.name+'.runpath-new');shutil.copyfile(s,tmp);tmp.chmod(0o755)
with tmp.open('rb') as f:os.fsync(f.fileno())
os.replace(tmp,p);os.sync()
'''
 subprocess.run(['ssh','root@omarchy-m4',shlex.join(['python3','-c',code,real,staged,old,new])],check=True)
 records.append(dict(path=real,before=old,after=new,runpath=rpath))
 (p/'manifest.json').write_text(json.dumps(records,indent=2)+'\n')
# A privileged client must not load a private DSO from a user-writable prefix.
subprocess.run(['ssh','root@omarchy-m4','chown -R root:root /opt/asahi-m4-port'],check=True)
print('Installed verified private Mesa RUNPATHs; retained evidence:',p)

# The secure Vulkan loader also ignores VK_DRIVER_FILES. Preserve the package
# ICD and point its normal registration at the same qualified private driver.
subprocess.run(['ssh','root@omarchy-m4', 'cp --no-clobber /usr/share/vulkan/icd.d/asahi_icd.json /root/asahi_icd.before-private-loader.json && cp /opt/asahi-m4-port/asahi_icd.json /usr/share/vulkan/icd.d/asahi_icd.json'],check=True)
