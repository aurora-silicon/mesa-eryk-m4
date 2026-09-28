#!/usr/bin/env python3
"""Archive completed target experiments to this workstation using verified rsync.

Only dated experiment directories older than --before with a final guard
result are selected. --before accepts YYYYMMDD or YYYYMMDDTHHMMSS (UTC).
Seed the archive with hardlinks to already retained local evidence. Rsync's
whole-file transfer checksum verifies copies before --remove-source-files
removes target files. Never use --inplace: local evidence links must stay intact.
Empty target directories and all current experiments are retained.
"""
import argparse,json,os,re,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--before',required=True)
p.add_argument('--destination',type=Path,required=True)
p.add_argument('--dry-run',action='store_true')
a=p.parse_args()
assert re.fullmatch(r'\d{8}(?:T\d{6})?',a.before)
a.destination.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]
selection = r"""import json,re,sys
from pathlib import Path
root=Path('/root/asahi-evidence');before=sys.argv[1];names=[]
for p in root.iterdir():
 if not p.is_dir() or p.is_symlink() or not re.match(r'^\d{8}T\d{6}',p.name) or p.name[:len(before)]>=before: continue
 try: result=json.loads((p/'result.json').read_text())
 except (OSError,ValueError): continue
 if isinstance(result,dict) and isinstance(result.get('returncode'),int): names.append(p.name)
print(json.dumps(sorted(names)))
"""
names=json.loads(subprocess.check_output(['ssh','root@omarchy-m4',shlex.join(['python3','-c',selection,a.before])]))
if a.dry_run:
 print(json.dumps(dict(before=a.before,count=len(names),names=names),indent=2))
 raise SystemExit(0)

for name in names:
 src=root/'_provenance/experiments'/name/'remote'
 dst=a.destination/name
 if not src.is_dir(): continue
 for folder,dirs,files in os.walk(src):
  d=dst/Path(folder).relative_to(src);d.mkdir(parents=True,exist_ok=True)
  for f in files:
   s=Path(folder)/f;t=d/f
   if not t.exists() and s.is_file() and not s.is_symlink():os.link(s,t)
manifest=a.destination.parent/(a.destination.name+'-directories.txt')
manifest.write_text(''.join(n+'/\n' for n in names))
print(f'Archiving {len(names)} completed experiments',flush=True)
subprocess.run(['rsync','-arc','--compress','--compress-choice=zstd','--remove-source-files',f'--files-from={manifest}','root@omarchy-m4:/root/asahi-evidence/',str(a.destination)+'/'],check=True)
print('ARCHIVE_COMPLETE',flush=True)
