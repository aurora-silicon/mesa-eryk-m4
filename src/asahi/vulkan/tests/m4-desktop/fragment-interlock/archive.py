from pathlib import Path
import json,gzip,tarfile,subprocess,shutil
base=Path('build/directx-interlock');out=Path('mesa/m4-asahi/src/asahi/vulkan/tests/m4-desktop/fragment-interlock');out.mkdir(exist_ok=True)
def archive(name,paths):
 tar=base/(name+'.tar')
 with tarfile.open(tar,'w') as t:
  for src,arc in paths:t.add(src,arcname=arc)
 subprocess.run(['zstd','-q','-f','-6',str(tar),'-o',str(out/(name+'.tar.zst'))],check=True);tar.unlink()
records=[]
for name in ['probe-r'+str(i) for i in range(1,10)]+['full-r1','sample-default-full-r1','sample-default-regression']:
 d=base/name;summary=json.loads((d/'summary.json').read_text());ev=Path(summary['evidence'])
 shutil.copyfile(d/'summary.json',out/(name+'-summary.json'))
 (out/(name+'-results.json.gz')).write_bytes(gzip.compress((d/'results.json').read_bytes(),mtime=0))
 records.append((ev,name))
for stamp,name in [('20260916T184925705868Z-dx-interlock-sample-default-oracles-r1','sample-default-oracles'),('20260916T181552344502Z-dx-interlock-full-r1','pixel-failed-full')]:
 ev=Path('_provenance/experiments')/stamp
 if (ev,name) not in records:records.append((ev,name))
archive('hardware-evidence',[(p,n) for p,n in records])
inputs=[(base/x,x) for x in ['ordered-witness','ordinary-witness','ordered-msaa-witness','ordinary-msaa-witness','mask-msaa-witness','sample-oracles']]
inputs += [(p,p.name) for p in base.iterdir() if p.is_file() and p.suffix in ['.frag','.vert','.metal','.txt','.json','.md','.patch']]
inputs += [(base/x,x) for x in ['build-sample-oracles.py','host-tests-sample-r1.log','install-sample-r1.log','sample-oracles-hardware-r1.log']]
archive('inputs-native-reference',inputs)
(out/'experiments.json').write_text(json.dumps([dict(evidence=str(p),name=n) for p,n in records],indent=2)+'\n')
shutil.copyfile(__file__,out/'archive.py')
