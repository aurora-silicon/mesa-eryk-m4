#!/usr/bin/env python3
"""Select a bounded range of primary draws from gfxrecon JSONL for resource comparison.

No GPU execution. Pass the emitted JSON to vulkan-trace.py --dump-config under
its usual guard. Indices include render-pass boundaries and the actual submit.
"""
import argparse, json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('jsonl',type=Path)
p.add_argument('--first',required=True,type=int)
p.add_argument('--last',required=True,type=int)
p.add_argument('--output',required=True,type=Path)
p.add_argument('--inputs',action='store_true')
a=p.parse_args()
if a.first>a.last:p.error('Reversed draw range')
commands={}; records=[]; used=set()
for line in a.jsonl.open():
 d=json.loads(line); f=d.get('function',{}); n=f.get('name',''); v=f.get('args',{}); i=d.get('index')
 if n=='vkBeginCommandBuffer':commands[v['commandBuffer']]=dict(begin=i,draws=[],passes=[],current=None)
 elif n=='vkCmdExecuteCommands':raise ValueError('Secondary buffers need explicit selection')
 elif n.startswith(('vkCmdBeginRenderPass','vkCmdBeginRendering')):
  c=commands[v['commandBuffer']];c['current']=[i];c['passes'].append(c['current'])
 elif n.startswith(('vkCmdEndRenderPass','vkCmdEndRendering','vkCmdNextSubpass')):
  commands[v['commandBuffer']]['current'].append(i)
 elif n.startswith('vkCmdDraw') and a.first<=i<=a.last:
  if n.startswith('vkCmdDrawMulti') and v['drawCount'] != 1:
   raise ValueError(f'Draw {i}: this dump build supports multi-draw checkpoints only for drawCount=1')
  commands[v['commandBuffer']]['draws'].append(i)
 elif n in ('vkQueueSubmit','vkQueueSubmit2','vkQueueSubmit2KHR'):
  for submit in v['pSubmits'] or []:
   cbs=submit.get('pCommandBuffers') or [x['commandBuffer'] for x in submit.get('pCommandBufferInfos') or []]
   for cb in cbs:
    c=commands[cb]
    if not c['draws']:continue
    if c['begin'] in used:raise ValueError('Repeated selected command buffer needs explicit handling')
    used.add(c['begin']);records.append(dict(c,submit=i))
if not records:raise ValueError('No submitted draws selected')
config=dict(BeginCommandBuffer=[c['begin'] for c in records],Draw=[c['draws'] for c in records],
 RenderPass=[[q for q in c['passes'] if any(q[0]<d<q[-1] for d in c['draws'])] for c in records],
 QueueSubmit=[c['submit'] for c in records],DumpResourcesOptions=dict(DumpRawImages=True,
 DumpAllDescriptors=a.inputs,DumpVertexIndexBuffer=a.inputs,DumpBeforeCommand=True,
 BinaryFileCompressionType='none',JsonOutputPerCommand=True))
with a.output.open('x') as f:json.dump(config,f,indent=2);f.write('\n')
print(f"Selected {sum(len(c['draws']) for c in records)} draws in {len(records)} command buffers")
