#!/usr/bin/env python3
"""Freeze an ordered CTS catalog and select zero-based, end-exclusive ranges.

Offline only. Range lists feed cts-bulk.py with its existing watchdog, thermal,
timeout and evidence guards. Capability skips remain indexed; results never
change membership. See README-cts.md.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def destination(path):
    if path.exists():
        raise ValueError(f'Refusing to overwrite {path}')
    path.parent.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(prefix=f'.{path.name}-', dir=path.parent)


def freeze(args):
    source_hash = hashlib.sha256()
    with destination(args.output) as temporary:
        stage = Path(temporary)
        canonical = stage / 'source.txt'
        # Use an external sort so millions of names do not require a Python
        # dictionary containing the entire population and its metadata.
        with args.source.open('rb') as source, canonical.open('wb') as out:
            for line in source:
                source_hash.update(line)
                case = line.rstrip(b'\r\n')
                if not re.fullmatch(rb'dEQP-VK\.[A-Za-z0-9_.-]+', case):
                    raise ValueError(f'Invalid CTS case: {case[:120]!r}')
                out.write(case + b'\n')
        ordered = stage / 'sorted.txt'
        subprocess.run(['sort', '-T', temporary, '-o', str(ordered), str(canonical)],
                       env={**os.environ, 'LC_ALL': 'C'}, check=True)
        canonical.unlink()
        digest = hashlib.sha256()
        previous = None
        total = count = excluded = 0
        with ordered.open('rb') as source, (stage / 'catalog.txt').open('wb') as out, \
                (stage / 'excluded.txt').open('wb') as exclusions:
            for line in source:
                if line == previous:
                    raise ValueError(f'Duplicate CTS case: {line[:120]!r}')
                previous = line
                total += 1
                if args.linux_only and (b'win32' in line or b'.android.' in line):
                    exclusions.write(line)
                    excluded += 1
                else:
                    out.write(line)
                    digest.update(line)
                    count += 1
        if not count:
            raise ValueError('Empty catalog')
        ordered.unlink()
        manifest = dict(schema=1, cts_revision=args.cts_revision,
                        source=str(args.source.resolve()),
                        source_sha256=source_hash.hexdigest(),
                        catalog_sha256=digest.hexdigest(), count=count,
                        source_count=total, excluded_count=excluded,
                        order='ASCII lexicographic (LC_ALL=C)',
                        linux_only=args.linux_only,
                        exclusion_rule='win32 or .android. in case name' if args.linux_only else None,
                        capability_filter=False, result_filter=False,
                        index_convention='zero-based; start inclusive, end exclusive')
        write_json(stage / 'catalog.json', manifest)
        stage.rename(args.output)
    print(json.dumps(manifest, indent=2))


def select_range(args):
    manifest = json.loads((args.catalog / 'catalog.json').read_text())
    if manifest.get('schema') != 1:
        raise ValueError('Unsupported catalog schema')
    if not 0 <= args.start < args.end <= manifest['count']:
        raise ValueError(f"Require 0 <= start < end <= {manifest['count']}; no implicit truncation")
    digest = hashlib.sha256()
    count = 0
    chosen = []
    previous = None
    with (args.catalog / 'catalog.txt').open('rb') as source:
        for index, line in enumerate(source):
            digest.update(line)
            if (not re.fullmatch(rb'dEQP-VK\.[A-Za-z0-9_.-]+\n', line)
                    or (previous is not None and line <= previous)):
                raise ValueError('Catalog must contain unique, sorted CTS names')
            previous = line
            count += 1
            if args.start <= index < args.end:
                chosen.append(line.decode('ascii').rstrip('\n'))
    if count != manifest['count'] or digest.hexdigest() != manifest['catalog_sha256']:
        raise ValueError('Catalog checksum/count mismatch; do not renumber an existing campaign')
    raw = ('\n'.join(chosen) + '\n').encode('ascii')
    provenance = dict(catalog=str(args.catalog.resolve()),
                      catalog_sha256=digest.hexdigest(),
                      cts_revision=manifest['cts_revision'], start=args.start,
                      end=args.end, index_convention=manifest['index_convention'])
    summary = dict(schema=1, method='fixed catalog range', count=len(chosen),
                   catalog_count=count, cases_sha256=hashlib.sha256(raw).hexdigest(),
                   **provenance)
    with destination(args.output) as temporary:
        stage = Path(temporary)
        (stage / 'cases.txt').write_bytes(raw)
        write_json(stage / 'cases.meta.json', dict(
            schema=1, cases_sha256=summary['cases_sha256'], provenance=provenance,
            cases=[dict(case=case, catalog_index=args.start + i,
                        classification='unclassified', requirements=[],
                        rule='Frozen broad catalog; audit capability skips separately')
                   for i, case in enumerate(chosen)]))
        write_json(stage / 'selection.json', summary)
        stage.rename(args.output)
    print(json.dumps(summary, indent=2))


def annotate(args):
    """Attach catalog identities to an existing targeted list without changing it."""
    manifest = json.loads((args.catalog / 'catalog.json').read_text())
    if manifest.get('schema') != 1:
        raise ValueError('Unsupported catalog schema')
    raw = args.cases.read_bytes()
    names = raw.decode('ascii').splitlines()
    if not names or len(set(names)) != len(names):
        raise ValueError('Expected unique, nonempty case list')
    selected = dict.fromkeys(names)
    digest = hashlib.sha256()
    previous = None
    count = 0
    with (args.catalog / 'catalog.txt').open('rb') as source:
        for index, line in enumerate(source):
            digest.update(line)
            if (not re.fullmatch(rb'dEQP-VK\.[A-Za-z0-9_.-]+\n', line)
                    or (previous is not None and line <= previous)):
                raise ValueError('Catalog must contain unique, sorted CTS names')
            previous = line
            count += 1
            name = line.decode('ascii').rstrip('\n')
            if name in selected:
                selected[name] = index
    if count != manifest['count'] or digest.hexdigest() != manifest['catalog_sha256']:
        raise ValueError('Catalog checksum/count mismatch; do not renumber the campaign')
    absent = [name for name, index in selected.items() if index is None]
    if absent:
        raise ValueError(f'Cases absent from the pinned catalog: {absent[:5]}')
    meta = dict(schema=1, cases_sha256=hashlib.sha256(raw).hexdigest(),
                provenance=dict(catalog=str(args.catalog.resolve()),
                    catalog_sha256=digest.hexdigest(), cts_revision=manifest['cts_revision'],
                    method='Existing targeted selection; catalog identities attached without reordering'),
                cases=[dict(case=name, catalog_index=selected[name],
                    classification='unclassified', requirements=[],
                    rule='Targeted frozen-catalog cases; audit capability skips separately')
                    for name in names])
    sidecar = args.cases.with_suffix('.meta.json')
    with sidecar.open('x') as out:
        out.write(json.dumps(meta, indent=2) + '\n')
    print(f'Indexed {len(names)} cases: {sidecar}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    catalog = commands.add_parser('freeze', help='Create a new immutable-by-workflow catalog')
    catalog.add_argument('--source', type=Path, required=True, help='Expanded CTS mustpass list')
    catalog.add_argument('--cts-revision', required=True, help='Pinned CTS tag/commit; recorded verbatim')
    catalog.add_argument('--linux-only', action='store_true', help='Exclude Windows/Android platform cases once')
    catalog.add_argument('--output', type=Path, required=True)
    catalog.set_defaults(run=freeze)
    selection = commands.add_parser('range', help='Select [start, end); never shuffle or redraw')
    selection.add_argument('--catalog', type=Path, required=True)
    selection.add_argument('--start', type=int, required=True)
    selection.add_argument('--end', type=int, required=True)
    selection.add_argument('--output', type=Path, required=True)
    selection.set_defaults(run=select_range)
    annotation = commands.add_parser('annotate', help='Attach catalog indices to a targeted case list')
    annotation.add_argument('--catalog', type=Path, required=True)
    annotation.add_argument('--cases', type=Path, required=True)
    annotation.set_defaults(run=annotate)
    args = parser.parse_args()
    args.run(args)


if __name__ == '__main__':
    main()
