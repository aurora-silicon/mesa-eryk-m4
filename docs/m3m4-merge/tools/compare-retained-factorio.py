#!/usr/bin/env python3
"""Explicit host-only migration of a retained capture's nonserialized ABI hash.
The only changed hash input is a callback in nir_lower_tex_options, which is
pass configuration, not serialized shader state. Original captures untouched.
"""
import hashlib,json,pathlib,subprocess,re,time
root=pathlib.Path.cwd(); base=root/'build/m3m4-merge-20260923'; out=base/'factorio-compiler-adapted';out.mkdir()
old=root/'mesa/m4-asahi';new=root/'mesa/m3m4-merged'
files=['src/asahi/compiler/agx_compile.h','src/compiler/nir/nir_serialize.c','src/compiler/nir/nir.h','src/compiler/nir/nir_shader_compiler_options.h','src/compiler/nir/nir_intrinsics.py','src/compiler/nir/nir_opcodes.py']
addition=b'   /* Optional filter for operations whose sampler bias is already handled. */\n   nir_instr_filter_cb lower_sampler_lod_bias_filter;\n'
for name in files:
 a=(old/name).read_bytes();b=(new/name).read_bytes()
 if name.endswith('/nir.h'):
  assert b.count(addition)==1;b=b.replace(addition,b'')
 assert a==b,name
hashes=[]
for build in ['mesa-m4-asahi-host','mesa-m3m4-host']:
 h=re.search(r'"([0-9a-f]{64})"',(root/'build'/build/'src/asahi/compiler/agx_replay_abi.h').read_text())[1].encode()+b'\0';hashes.append(h)
report=dict(adaptation='Replace capture ABI hash only after proving every hashed source identical except the nonserialized nir_lower_tex_options callback. This is an adapted host replay, not native cache interchangeability.',hashed_sources=files,old_abi=hashes[0].decode().strip('\0'),new_abi=hashes[1].decode().strip('\0'),results=[])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
exe=[root/'build'/build/'src/asahi/compiler/agx_shader_replay' for build in ['mesa-m4-asahi-host','mesa-m3m4-host']]
report['executables']={str(p):sha(p) for p in exe}
try:
 for i,name in enumerate(json.loads((root/'build/agx3-slot-cost-20260922/inputs.json').read_text())):
  original=pathlib.Path(name);raw=original.read_bytes();assert raw[12:77]==hashes[0]
  translated=out/f'{i:04d}-adapted-input.bin';translated.write_bytes(raw[:12]+hashes[1]+raw[77:]);outputs=[]
  row=dict(original=str(original),original_sha256=sha(original),adapted_sha256=sha(translated),executions=[])
  for side,(program,source) in enumerate(zip(exe,[original,translated])):
   output=out/f'{i:04d}-{side}.bin';start=time.monotonic()
   r=subprocess.run([str(program),str(source),'--binary-output='+str(output)],capture_output=True,timeout=30)
   (out/f'{i:04d}-{side}.log').write_bytes(r.stdout+r.stderr)
   row['executions'].append(dict(returncode=r.returncode,seconds=time.monotonic()-start))
   assert r.returncode==0,(i,side)
   outputs.append(output.read_bytes())
  row['identical']=outputs[0]==outputs[1];row['binary_sha256']=[hashlib.sha256(x).hexdigest() for x in outputs]
  report['results'].append(row)
  assert row['identical'],i
 report['status']='pass'
except BaseException as e:
 report.update(status='needs-review',error=str(e));raise
finally:
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(len(report['results']),'adapted compiler replays identical')
