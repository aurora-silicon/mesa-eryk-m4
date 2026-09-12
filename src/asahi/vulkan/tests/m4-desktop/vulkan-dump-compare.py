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


def entries(directory):
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
    if not output:
        raise ValueError(f'No draw attachments found in {directory}')
    return output


def read(directory, sub, field):
    path = directory / Path(sub[field]).name
    data = path.read_bytes()
    return np.frombuffer(data, dtype=np.uint8), hashlib.sha256(data).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path)
    p.add_argument('actual', type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    reference, actual = entries(a.reference), entries(a.actual)
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
    with a.output.open('x') as out:
        json.dump(dict(reference=str(a.reference), actual=str(a.actual), attachments=rows), out, indent=2)
        out.write('\n')


if __name__ == '__main__':
    main()
