#!/usr/bin/env python3
"""Make an explicitly adapted Factorio diagnostic replay; never alter the source.

Bake invariant vertex layouts, expand multidraw, replace unsupported statistics
with occlusion queries. Requires a matching gfxrecon-convert JSONL. Every adapted
call is logged; all other binary blocks (including shaders/textures) are copied.
Not an unmodified-application compatibility test. Validate pixels on every ICD.
"""
import argparse, collections, copy, hashlib, json, re, struct
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parent.parent
G=ROOT/'reference/gfxreconstruct'
def u32(v):return struct.pack('<I',int(v)&0xffffffff)
def u64(v):return struct.pack('<Q',int(v))
def pre(v,kind):return u32(kind | (1 if v is None else 128))
class Encoder:
 def __init__(self):
  src=(G/'framework/generated/generated_vulkan_struct_encoders.cpp').read_text()
  self.bodies=dict(re.findall(r'void EncodeStruct\(ParameterEncoder\* encoder, const (\w+)& value\)\n\{\n(.*?)\n\}',src,re.S))
  self.enums=dict(re.findall(r'\b(VK_\w+)\s*=\s*([\w+-]+)',(G/'external/Vulkan-Headers/include/vulkan/vulkan_core.h').read_text()))
  self.members={};self.stypes={}
  for t in ET.parse(G/'external/Vulkan-Headers/registry/vk.xml').getroot().findall('./types/type'):
   name=t.get('name'); self.members[name]={m.findtext('name'):m.findtext('type') for m in t.findall('member')}
   for m in t.findall('member'):
    if m.findtext('name')=='sType' and m.get('values'): self.stypes[m.get('values')]=name
 def num(self,v):
  if isinstance(v,(int,float)):return v
  if v in self.enums:return self.num(self.enums[v])
  return int(v.rstrip('UuLl'),0)
 def scalar(self,kind,v):
  f={'UInt16':'H','UInt8':'B','UInt64':'Q','SizeT':'Q','Int32':'i','Float':'f','UInt32':'I','Enum':'I','Flags':'I','SampleMask':'I'}[kind]
  return struct.pack('<'+f,self.num(v))
 def ptr(self,t,v):return pre(v,34)+(self.obj(t,v) if v is not None else b'')
 def arr(self,t,v):return pre(v,36)+(u64(len(v))+b''.join(self.obj(t,x) for x in v) if v is not None else b'')
 def obj(self,t,v):
  out=b''
  for line in self.bodies[t].strip().splitlines():
   line=line.strip(); m=re.search(r'value\.(\w+)',line); assert m,(t,line);key=m[1];x=v[key]
   if 'EncodePNextStruct' in line:out+=self.ptr(self.stypes[x['sType']] if x else '',x)
   elif 'EncodeStructArray(' in line:out+=self.arr(self.members[t][key],x)
   elif 'EncodeStructPtr(' in line:out+=self.ptr(self.members[t][key],x)
   elif 'EncodeStruct(' in line:out+=self.obj(self.members[t][key],x)
   elif 'EncodeVulkanHandleValue<' in line:out+=u64(x)
   elif 'EncodeString(' in line:out+=pre(x,10)+(u64(len(x.encode()))+x.encode() if x is not None else b'')
   else:
    m=re.search(r'Encode(\w+)(Value|Array)\(',line);assert m,(t,line)
    kind,mode=m.groups()
    out+=self.scalar(kind,x) if mode=='Value' else pre(x,4)+(u64(len(x))+b''.join(self.scalar(kind,y) for y in x) if x is not None else b'')
  return out

def adapt(trace,jsonl,output):
 assert trace.resolve()!=output.resolve()
 records={r['index']:r for l in jsonl.open() if 'index' in (r:=json.loads(l))}
 enc=Encoder(); layouts=collections.defaultdict(set);states={}
 for r in records.values():
  f=r.get('function',{});n=f.get('name');a=f.get('args',{});s=states.setdefault(a.get('commandBuffer'),{})
  if n=='vkBeginCommandBuffer':s.clear()
  if n=='vkCmdBindPipeline':s['pipe']=a['pipeline']
  if n=='vkCmdSetVertexInputEXT':s['vertex']=json.dumps({k:v for k,v in a.items() if k!='commandBuffer'},sort_keys=True)
  if n and n.startswith('vkCmdDraw'):layouts[s['pipe']].add(s['vertex'])
 assert all(len(v)==1 for v in layouts.values()),'Need per-draw pipeline variants for changing vertex input'
 ids={n:0x10000+int(v,16) for n,v in re.findall(r'ApiCall_(vk\w+)\s*= MakeApiCallId\(ApiFamily_Vulkan, (0x\w+)\)',(G/'framework/format/api_call_id.h').read_text())}
 changes=[]
 def edit(index):
  f=records.get(index,{}).get('function',{});n=f.get('name');a=copy.deepcopy(f.get('args',{})); data=None;newn=n
  if n=='vkCreateGraphicsPipelines':
   for p,h in zip(a['pCreateInfos'],a['pPipelines']):
    v=json.loads(next(iter(layouts[h]))) if h in layouts else {'pVertexBindingDescriptions':[],'pVertexAttributeDescriptions':[]}
    assert all(b['divisor']==1 for b in v['pVertexBindingDescriptions'])
    b=[{k:x[k] for k in ('binding','stride','inputRate')} for x in v['pVertexBindingDescriptions']]
    attrs=[{k:x[k] for k in ('location','binding','format','offset')} for x in v['pVertexAttributeDescriptions']]
    p['pVertexInputState']=dict(sType='VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO',pNext=None,flags=0,vertexBindingDescriptionCount=len(b),pVertexBindingDescriptions=b,vertexAttributeDescriptionCount=len(attrs),pVertexAttributeDescriptions=attrs)
    ds=p['pDynamicState'];ds['pDynamicStates']=[x for x in ds['pDynamicStates'] if x not in ('VK_DYNAMIC_STATE_VERTEX_INPUT_EXT','VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT')];ds['dynamicStateCount']=len(ds['pDynamicStates'])
   data=u64(a['device'])+u64(a['pipelineCache'])+u32(a['createInfoCount'])+enc.arr('VkGraphicsPipelineCreateInfo',a['pCreateInfos'])+enc.ptr('',None)+pre(a['pPipelines'],4)+u64(len(a['pPipelines']))+b''.join(u64(x) for x in a['pPipelines'])+u32(0)
  elif n in ('vkCmdSetVertexInputEXT','vkCmdSetColorWriteEnableEXT'):
   if n=='vkCmdSetColorWriteEnableEXT':assert all(a['pColorWriteEnables'])
   data=b''
  elif n in ('vkCmdDrawMultiEXT','vkCmdDrawMultiIndexedEXT'):
   assert a['drawCount']==1
   v=a['pVertexInfo'] if n=='vkCmdDrawMultiEXT' else a['pIndexInfo'];v=v[0] if isinstance(v,list) else v
   if n=='vkCmdDrawMultiEXT':newn='vkCmdDraw';data=u64(a['commandBuffer'])+b''.join(u32(x) for x in (v['vertexCount'],a['instanceCount'],v['firstVertex'],a['firstInstance']))
   else:
    newn='vkCmdDrawIndexed';off=a.get('pVertexOffset');off=off[0] if isinstance(off,list) else off
    data=u64(a['commandBuffer'])+b''.join(u32(x) for x in (v['indexCount'],a['instanceCount'],v['firstIndex'],v['vertexOffset'] if off is None else off,a['firstInstance']))
  elif n=='vkCreateQueryPool':
   p=a['pCreateInfo'];p['flags']=0
   if p['queryType']=='VK_QUERY_TYPE_PIPELINE_STATISTICS':p.update(queryType='VK_QUERY_TYPE_OCCLUSION',pipelineStatistics=0)
   data=u64(a['device'])+enc.ptr('VkQueryPoolCreateInfo',p)+enc.ptr('',None)+pre(a['pQueryPool'],2)+u64(a['pQueryPool'])+u32(0)
  if data is None:return None
  changes.append(dict(index=index,original=n,replacement=newn if data else 'omitted',reason='explicit portable diagnostic adaptation'))
  if not data:return b''
  body=u32(ids[newn])+u64(f['thread'])+data
  return struct.pack('<QI',len(body),4)+body
 with trace.open('rb') as fi,output.open('xb') as fo:
  header=fi.read(16);assert header[:4]==b'GFXR';fo.write(header);fo.write(fi.read(struct.unpack_from('<I',header,12)[0]*8));index=0
  while h:=fi.read(12):
   size,typ=struct.unpack('<QI',h);body=fi.read(size);assert len(body)==size
   replacement=edit(index)
   if replacement is not None:
    assert typ&0x7fffffff==4
    fo.write(replacement)
   else:fo.write(h);fo.write(body)
   index+=1
 result=dict(source=str(trace),source_sha256=hashlib.file_digest(trace.open('rb'),'sha256').hexdigest(),output=str(output),output_sha256=hashlib.file_digest(output.open('rb'),'sha256').hexdigest(),blocks=index,changes=changes,limitations=['Statistics replaced by occlusion; query values are not equivalent.','Immutable vertex layout proven for every recorded draw.','Shaders and all unmodified binary blocks retained byte-for-byte.','Same adapted trace must be measured on all drivers; verify pixels.'])
 output.with_suffix('.adaptation.json').write_text(json.dumps(result,indent=2)+'\n');print(collections.Counter(c['original'] for c in changes))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('jsonl',type=Path);p.add_argument('output',type=Path);a=p.parse_args();adapt(a.trace,a.jsonl,a.output)
