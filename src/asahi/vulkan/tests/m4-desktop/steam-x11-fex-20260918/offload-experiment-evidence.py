#!/usr/bin/env python3
"""Archive completed target experiments to this workstation using verified rsync.

Only date-prefixed experiment directories older than --before are selected.
Seed the archive with hardlinks to already retained local evidence. Rsync's
whole-file transfer checksum verifies copies before --remove-source-files
removes target files. Never use --inplace: local evidence links must stay intact.
Empty target directories and all current experiments are retained.
"""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--before',required=True)
p.add_argument('--destination',type=Path,required=True)
a=p.parse_args()
assert len(a.before)==8 and a.before.isdecimal()
a.destination.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]
remote=json.loads(subprocess.check_output(['ssh','root@omarchy-m4', 'python3 -c '+__import__('shlex').quote('import json,os; print(json.dumps([n for n in os.listdir("/root/asahi-evidence") if os.path.isdir("/root/asahi-evidence/"+n)]))')]))
names=sorted(n for n in remote if n[:8].isdecimal() and n[:8]<a.before and '/' not in n)
for name in names:
 src=root/'_provenance/experiments'/name/'remote'
 dst=a.destination/name
 if not src.is_dir(): continue
 for folder,dirs,files in os.walk(src):
  d=dst/Path(folder).relative_to(src);d.mkdir(parents=True,exist_ok=True)
  for f in files:
   s=Path(folder)/f;t=d/f
   if not t.exists() and s.is_file() and not s.is_symlink():os.link(s,t)
manifest=a.destination.parent/'evidence-directories.txt'
manifest.write_text(''.join(n+'/\n' for n in names))
print(f'Archiving {len(names)} completed experiments',flush=True)
subprocess.run(['rsync','-arc','--compress','--compress-choice=zstd','--remove-source-files',f'--files-from={manifest}','root@omarchy-m4:/root/asahi-evidence/',str(a.destination)+'/'],check=True)
print('ARCHIVE_COMPLETE',flush=True)
