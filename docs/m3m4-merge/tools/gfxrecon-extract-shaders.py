#!/usr/bin/env python3
"""Extract SPIR-V from indexed module or inline pipeline blocks, without replaying.

Input metadata is gfxrecon-convert JSONL retaining the desired shader module
and pipeline creation calls, including inline shader-module pNext data.
Supports uncompressed/LZ4
function blocks. Rejects ambiguous magic or invalid SPIR-V; never modifies trace.
"""
import argparse,ctypes,hashlib,json,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('trace',type=Path);p.add_argument('metadata',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
wanted={}
for line in a.metadata.open():
 d=json.loads(line);f=d['function'];v=f['args'];shaders=[]
 if f['name']=='vkCreateShaderModule':
  shaders.append({'module':v['pShaderModule'],'size':v['pCreateInfo']['codeSize'],
                  'name':f"sh{v['pShaderModule']}"})
 elif f['name'] in ('vkCreateGraphicsPipelines','vkCreateComputePipelines'):
  for pipeline,info in zip(v['pPipelines'],v['pCreateInfos'],strict=True):
   stages=[info['stage']] if 'stage' in info else (info.get('pStages') or [])
   for stage in stages:
    node=stage.get('pNext')
    while node:
     if node['sType']=='VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO':
      shaders.append({'pipeline':pipeline,'stage':stage['stage'],'size':node['codeSize'],
                      'name':f"pipeline{pipeline}-{stage['stage']}"})
     node=node.get('pNext')
 if shaders:wanted[d['index']]=shaders
assert wanted, 'Metadata contains no shader modules or inline pipeline shaders'
a.output.mkdir(parents=True,exist_ok=False)
lz4=ctypes.CDLL('liblz4.so.1');lz4.LZ4_decompress_safe.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int];lz4.LZ4_decompress_safe.restype=ctypes.c_int
records=[]
with a.trace.open('rb') as f:
 magic,major,minor,n=struct.unpack('<4sIII',f.read(16));assert magic==b'GFXR' and major==0
 options=dict(struct.unpack('<II',f.read(8)) for _ in range(n))
 i=0
 while wanted:
  h=f.read(12);assert len(h)==12, 'Missing selected block'
  size,kind=struct.unpack('<QI',h)
  if i not in wanted:f.seek(size,1);i+=1;continue
  info=wanted.pop(i);raw=f.read(size);assert len(raw)==size
  if kind==0x80000004:
   assert options.get(1)==1, 'Only LZ4 compression is supported'
   length=struct.unpack_from('<Q',raw,12)[0];assert 0<length<64*1024*1024
   dst=ctypes.create_string_buffer(length);src=raw[20:]
   assert lz4.LZ4_decompress_safe(src,dst,len(src),length)==length
   raw=dst.raw
  else:assert kind==4;raw=raw[12:]
  end=0
  for shader in info:
   start=raw.find(b'\x03\x02\x23\x07',end);assert start>=0, 'Missing SPIR-V magic'
   end=start+shader['size'];blob=raw[start:end];assert len(blob)==shader['size']
   path=a.output/shader['name'];path.write_bytes(blob)
   subprocess.run(['spirv-val','--scalar-block-layout','--target-env','vulkan1.3',str(path)],check=True)
   subprocess.run(['spirv-dis','--raw-id',str(path),'-o',str(path.with_suffix('.spvasm'))],check=True)
   records.append(dict(shader,index=i,sha256=hashlib.sha256(blob).hexdigest(),path=path.name))
  assert raw.find(b'\x03\x02\x23\x07',end)<0, 'Unexpected additional SPIR-V module'
  i+=1
(a.output/'manifest.json').write_text(json.dumps({'trace':str(a.trace),'trace_sha256':hashlib.file_digest(a.trace.open('rb'),'sha256').hexdigest(),'shaders':records},indent=2)+'\n')
print('Extracted and SPIR-V validated',len(records),'shader modules')
