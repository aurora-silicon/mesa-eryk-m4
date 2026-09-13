#!/usr/bin/env python3
"""Compare GFXReconstruct raw draw attachments without repeatedly viewing images.

Reports byte differences for complete attachments and reference-written bytes.
These are diagnostics, not a tolerance verdict for floating-point formats or
undefined attachment regions. Output includes input hashes and exact draw IDs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np


def entries(directory, allow_empty=False):
    output = {}
    for path in directory.glob('DrawCall_*_dr.json'):
        for draw in json.loads(path.read_text()):
            if 'drawIndex' not in draw:
                continue
            for attachment in draw.get('colorAttachments', []):
                for sub in attachment['subresources']:
                    key = (draw['drawIndex'], attachment['imageId'], attachment['location'],
                           sub['mipLevel'], sub['arrayLayer'], sub['depth'])
                    if key in output:
                        raise ValueError(f'Duplicate draw attachment {key}')
                    output[key] = (attachment, sub)
    if not output and not allow_empty:
        raise ValueError(f'No draw attachments found in {directory}')
    return output


def read(directory, sub, field):
    path = directory / Path(sub[field]).name
    data = path.read_bytes()
    return np.frombuffer(data, dtype=np.uint8), hashlib.sha256(data).hexdigest()


def descriptor_entries(directory):
    output = {}
    for path in directory.glob('*_dr.json'):
        for draw in json.loads(path.read_text()):
            index = draw.get('drawIndex', draw.get('dispatchIndex'))
            if index is None:
                continue
            for stage, descriptors in draw.get('descriptors', {}).items():
                for desc in descriptors:
                    key = (index, stage, desc['set'], desc['binding'], desc['arrayIndex'])
                    metadata = {k: v for k, v in desc.items() if k not in ('file', 'beforeFile', 'subresources')}
                    subs = desc.get('subresources', [desc] if 'file' in desc else [])
                    for sub in subs:
                        subkey = key + tuple(sub.get(k) for k in ('aspect', 'mipLevel', 'arrayLayer', 'depth'))
                        if subkey in output:
                            raise ValueError(f'Duplicate descriptor {subkey}')
                        output[subkey] = (metadata, sub)
    return output


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path)
    p.add_argument('actual', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--descriptors', action='store_true', help='Also compare dumped buffer/image descriptors by binding and subresource')
    a = p.parse_args()
    reference, actual = entries(a.reference, a.descriptors), entries(a.actual, a.descriptors)
    if reference.keys() != actual.keys():
        raise ValueError(f'Attachment keys differ: {reference.keys() ^ actual.keys()}')
    rows = []
    for key, (attachment, sub) in sorted(reference.items()):
        gpu_attachment, gpu_sub = actual[key]
        if (attachment['format'], sub['dimensions']) != (gpu_attachment['format'], gpu_sub['dimensions']):
            raise ValueError(f'Attachment metadata differs: {key}')
        ref, ref_hash = read(a.reference, sub, 'file')
        gpu, gpu_hash = read(a.actual, gpu_sub, 'file')
        if ref.shape != gpu.shape:
            raise ValueError(f'Byte sizes differ: {key}')
        delta = abs(ref.astype(np.int16) - gpu.astype(np.int16))
        row = dict(key=key, format=attachment['format'], dimensions=sub['dimensions'],
                   reference_sha256=ref_hash, actual_sha256=gpu_hash,
                   differing_bytes=int(np.count_nonzero(delta)), max_byte_error=int(delta.max()),
                   mean_byte_error=float(delta.mean()))
        if 'beforeFile' in sub and 'beforeFile' in gpu_sub:
            before, before_hash = read(a.reference, sub, 'beforeFile')
            gpu_before, gpu_before_hash = read(a.actual, gpu_sub, 'beforeFile')
            written = ref != before
            row.update(reference_before_sha256=before_hash, actual_before_sha256=gpu_before_hash,
                       reference_changed_bytes=int(written.sum()), actual_changed_bytes=int(np.count_nonzero(gpu != gpu_before)),
                       changed_byte_mismatches=int(np.count_nonzero(delta[written])),
                       changed_byte_max_error=int(delta[written].max()) if written.any() else 0)
        rows.append(row)
        print(json.dumps({k: row[k] for k in ('key','format','differing_bytes','max_byte_error','reference_changed_bytes','actual_changed_bytes','changed_byte_mismatches') if k in row}))
    report = dict(reference=str(a.reference), actual=str(a.actual), attachments=rows)
    if a.descriptors:
        ref_desc, gpu_desc = descriptor_entries(a.reference), descriptor_entries(a.actual)
        if not ref_desc and not rows:
            raise ValueError('No attachments or descriptors found')
        if ref_desc.keys() != gpu_desc.keys():
            raise ValueError(f'Descriptor keys differ: {ref_desc.keys() ^ gpu_desc.keys()}')
        report['descriptors'] = []
        for key, (metadata, sub) in sorted(ref_desc.items(), key=lambda item: repr(item[0])):
            actual_metadata, actual_sub = gpu_desc[key]
            if metadata != actual_metadata:
                raise ValueError(f'Descriptor metadata differs: {key}')
            ref, ref_hash = read(a.reference, sub, 'file')
            gpu, gpu_hash = read(a.actual, actual_sub, 'file')
            if ref.shape != gpu.shape:
                raise ValueError(f'Descriptor byte sizes differ: {key}')
            row = dict(key=key, metadata=metadata, reference_sha256=ref_hash, actual_sha256=gpu_hash,
                       bytes=len(ref), differing_bytes=int(np.count_nonzero(ref != gpu)),
                       reference_nonzero_bytes=int(np.count_nonzero(ref)), actual_nonzero_bytes=int(np.count_nonzero(gpu)))
            if 'beforeFile' in sub and 'beforeFile' in actual_sub:
                before, before_hash = read(a.reference, sub, 'beforeFile')
                gpu_before, gpu_before_hash = read(a.actual, actual_sub, 'beforeFile')
                if before.shape != ref.shape or gpu_before.shape != gpu.shape:
                    raise ValueError(f'Before/after sizes differ: {key}')
                written = ref != before
                row.update(reference_before_sha256=before_hash, actual_before_sha256=gpu_before_hash,
                           before_differing_bytes=int(np.count_nonzero(before != gpu_before)),
                           reference_changed_bytes=int(written.sum()),
                           actual_changed_bytes=int(np.count_nonzero(gpu != gpu_before)),
                           changed_byte_mismatches=int(np.count_nonzero((ref != gpu)[written])))
            report['descriptors'].append(row)
            if row['differing_bytes']:
                print(json.dumps({k: row[k] for k in ('key', 'metadata', 'bytes', 'differing_bytes',
                                                      'reference_nonzero_bytes', 'actual_nonzero_bytes')}))
    with a.output.open('x') as out:
        json.dump(report, out, indent=2)
        out.write('\n')


if __name__ == '__main__':
    main()
