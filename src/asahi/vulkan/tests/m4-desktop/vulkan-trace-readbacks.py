#!/usr/bin/env python3
"""Select primary-command-buffer image-to-buffer copies from gfxrecon JSONL.

Emits a resource-dump config for comparing replay readback bytes without images.
Only handles direct primary submissions, and rejects secondary command buffers.
"""
import argparse
import collections
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('jsonl', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
buffers = {}
used = set()
config = dict(BeginCommandBuffer=[], Transfer=[], QueueSubmit=[],
              DumpResourcesOptions=dict(DumpRawImages=True, BinaryFileCompressionType='none'))
counts = collections.Counter()
with a.jsonl.open() as source:
    for line in source:
        obj = json.loads(line)
        f = obj.get('function', {})
        name, args = f.get('name'), f.get('args', {})
        if not name:
            continue
        index = obj['index']
        counts[name] += 1
        if name == 'vkCmdExecuteCommands':
            raise ValueError('Secondary command buffers need explicit dump selection')
        if name == 'vkBeginCommandBuffer':
            buffers[args['commandBuffer']] = dict(begin=index, copies=[])
        elif name in ('vkCmdCopyImageToBuffer', 'vkCmdCopyImageToBuffer2', 'vkCmdCopyImageToBuffer2KHR'):
            buffers[args['commandBuffer']]['copies'].append(index)
        elif name in ('vkQueueSubmit', 'vkQueueSubmit2', 'vkQueueSubmit2KHR'):
            for submit in args['pSubmits'] or []:
                submitted = submit.get('pCommandBuffers') or [c['commandBuffer'] for c in submit.get('pCommandBufferInfos') or []]
                for cb in submitted:
                    entry = buffers[cb]
                    if not entry['copies']:
                        continue
                    if entry['begin'] in used:
                        raise ValueError('Repeated command-buffer submission needs explicit dump selection')
                    used.add(entry['begin'])
                    config['BeginCommandBuffer'].append(entry['begin'])
                    config['Transfer'].append(entry['copies'])
                    config['QueueSubmit'].append(index)
if not config['Transfer']:
    raise ValueError('No submitted image-to-buffer readbacks in trace')
with a.output.open('x') as out:
    json.dump(config, out, indent=2); out.write('\n')
print(json.dumps(dict(readbacks=sum(map(len, config['Transfer'])), submissions=len(set(config['QueueSubmit'])),
                     vulkan_calls=sum(counts.values())), indent=2))
