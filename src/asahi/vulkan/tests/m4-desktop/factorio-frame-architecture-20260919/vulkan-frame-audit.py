#!/usr/bin/env python3
"""Audit a GFXReconstruct JSONL trace's submitted rendering work, offline.

Counts recording separately from execution: trimmed captures may restore
command buffers which are never submitted. No trace edits or hardware access.
"""
import argparse
import collections
import copy
import hashlib
import json
from pathlib import Path


def audit(path):
    images, views, buffers, active, pipelines = {}, {}, {}, {}, {}
    submits = []
    frame = 1
    calls = collections.Counter()
    for line in path.read_text().splitlines():
        record = json.loads(line)
        f = record.get('function', {})
        name, a = f.get('name'), f.get('args', {})
        if not name:
            continue
        calls[name] += 1
        cb = a.get('commandBuffer')
        if name == 'vkCreateImage':
            images[a['pImage']] = a['pCreateInfo']
        elif name == 'vkCreateImageView':
            views[a['pView']] = a['pCreateInfo']
        elif name == 'vkBeginCommandBuffer':
            buffers[cb] = dict(passes=[], operations=collections.Counter())
            active.pop(cb, None)
            pipelines.pop(cb, None)
        elif name == 'vkCmdBeginRendering':
            info = a['pRenderingInfo']
            attachments = []
            for att in info.get('pColorAttachments') or []:
                view = views.get(att['imageView'], {})
                image = images.get(view.get('image'), {})
                attachments.append(dict(view=att['imageView'], image=view.get('image'),
                    format=view.get('format'), samples=image.get('samples'),
                    load=att['loadOp'], store=att['storeOp']))
            p = dict(trace_index=record['index'], area=info['renderArea'],
                     attachments=attachments, draws=0, pipelines=[])
            buffers[cb]['passes'].append(p)
            active[cb] = p
        elif name == 'vkCmdEndRendering':
            active.pop(cb, None)
        elif name == 'vkCmdBindPipeline' and a['pipelineBindPoint'] == 'VK_PIPELINE_BIND_POINT_GRAPHICS':
            pipelines[cb] = a['pipeline']
        elif name.startswith('vkCmdDraw') and cb in active:
            active[cb]['draws'] += 1
            pipe = pipelines.get(cb)
            if pipe not in active[cb]['pipelines']:
                active[cb]['pipelines'].append(pipe)
        elif name in ('vkQueueSubmit', 'vkQueueSubmit2', 'vkQueueSubmit2KHR'):
            infos = a['pSubmits']
            # gfxrecon-convert emits a singleton structure as an object in
            # some versions, whereas multi-submit arrays remain lists.
            if isinstance(infos, dict):
                infos = [infos]
            for info in infos or []:
                cbs = (info.get('pCommandBuffers') or []) if name == 'vkQueueSubmit' else [
                    c['commandBuffer'] for c in info.get('pCommandBufferInfos') or []]
                if cbs:
                    submits.append(dict(frame=frame, trace_index=record['index'],
                        queue=a['queue'], command_buffers=[dict(handle=c, **copy.deepcopy(buffers[c])) for c in cbs]))
        elif name == 'vkQueuePresentKHR':
            frame += 1
        if cb is not None and name.startswith('vkCmd'):
            buffers[cb]['operations'][name] += 1
    return dict(source=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        scope='Recorded Vulkan API work at submission; restored but unsubmitted buffers excluded. Frame numbers follow present calls, including any capture setup.',
        recorded_calls=calls, submissions=submits)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace_jsonl', type=Path)
    a = p.parse_args()
    print(json.dumps(audit(a.trace_jsonl), indent=2))
