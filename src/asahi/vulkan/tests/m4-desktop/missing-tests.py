#!/usr/bin/env python3
"""Manage missing_tests.json: every catalog case that is not Pass on the M4,
grouped by the reason (NotSupported message or failure family).

  build            rebuild missing_tests.json + missing_tests/cases/*.json from the campaign
                   results, the range records and the targeted reruns
  next [GROUP]     print the next open group (or GROUP) as JSON
  check [GROUP] [--batch N]
                   run up to N not-yet-passing cases of the group on the M4 (guarded);
                   record passes in missing_tests/progress/; print the first case that is
                   still not Pass (exit 1) or report the batch passed (exit 0)
  complete GROUP [--wontfix REASON | --force]
                   run every remaining case of the group; mark completed only if all pass
  status           one-line summary
"""
import argparse, collections, glob, hashlib, json, os, re, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INDEX = ROOT / 'missing_tests.json'
CASES_DIR = ROOT / 'missing_tests' / 'cases'
PROGRESS_DIR = ROOT / 'missing_tests' / 'progress'
RUNS_DIR = ROOT / 'build' / 'missing-tests'
CATALOG = ROOT / 'build' / 'cts-vulkan14-ordered' / 'catalog' / 'catalog.txt'
RUNNER = ROOT / 'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner'
BINARY = '/root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit'
RECORDS = ROOT / 'mesa/m4-asahi/src/asahi/vulkan/tests/m4-desktop/cts-vulkan14-ordered'
BATCHES = ['build/cts-vulkan14-ordered-perf/run4/0000000-1000000',
           'build/cts-vulkan14-ordered-perf/run3/1000000-1200000',
           'build/cts-vulkan14-ordered-perf/run5/1200000-2000000',
           'build/cts-vulkan14-ordered-perf/run6/2000000-3000000',
           'build/cts-vulkan14-ordered-perf/run7/3000000-3206030']
# Targeted reruns of this and earlier sessions whose results supersede the campaign
# (chronological; later wins). Sparse (steam-sparse-r1) is excluded on purpose: the
# feature is not advertised, so the campaign's NotSupported state is the truth.
OVERLAYS = ['build/claude-fix/steam-sparse2-r1', 'build/claude-fix/steam-rb3-r1', 'build/claude-fix/steam-rb4-r1',
            'build/claude-fix/steam-ps2-r1', 'build/claude-fix/steam-ps4',
            'build/claude-fix/steam-xfb-r1', 'build/claude-fix/steam-eds3-r1',
            'build/claude-fix/r7-ycc-val2', 'build/claude-fix/r7-wsi-final',
            'build/claude-fix/r7-wsi-cs-basic', 'build/claude-fix/r7-wsi-cs-hdr'] + \
           ['build/claude-fix/r7-wsi-final-%d' % i for i in range(10)] + \
           sorted(glob.glob(str(ROOT / 'build/missing-tests/*/results.json')), key=os.path.getmtime)  # the loop's own runs
OVERLAYS += ['build/directx-sparse/full-r1', 'build/directx-sparse/reg1000']

CLASSES = [  # (regex on the reason, class, priority); first match wins
    (r'sparse|Sparse', 'feature-implementable', 2),
    (r'VK_EXT_primitives_generated_query', 'feature-implementable', 3),
    (r'fragment_shading_rate|acceleration_structure|ray_tracing|ray_query|mesh_shader|Mesh shaders|'
     r'fragment_shader_barycentric|shader_image_atomic_int|conservative_rasterization|'
     r'fragment_shader_interlock|sampler_filter_minmax|filterMinmax|descriptor_buffer|'
     r'conditional_rendering|image_sliced_view|depthBounds|shaderOutputLayer|shaderOutputViewportIndex|'
     r'multisampled_render_to_single_sampled|attachment_feedback_loop_dynamic_state|'
     r'shader_tile_image|Int64Atomics|maintenance1[01]|copy_memory_indirect|memory_priority|'
     r'pageable_device_local', 'feature-p2', 4),
    (r'Exclusive compute queue|No matching queue|queue famil|queue pairs|transfer queue|'
     r'device group|1 physical device|display type|No displays|No DRM|FUCHSIA|video|'
     r'data_graph|ARM_data_graph|cooperative|longVector|Protected Memory|astc|filter_cubic|'
     r'NV_|AMD_|QCOM|HUAWEI|VALVE_|MESA_|Xlib|xcb|headless|direct_drm|wayland|shader_object.*attachments|'
     r'maxColorAttachments|subgroup partitioned|not supported with 1|maintenance9', 'out-of-scope', 9),
    (r'[Ff]ormat', 'format', 6),
]

def classify(reason):
    for pat, cls, prio in CLASSES:
        if re.search(pat, reason):
            return cls, prio
    return 'unclassified', 5

def norm(msg):
    m = (msg or '').strip()
    m = re.sub(r'\d+', 'N', m)
    return m[:110]

def group_id(kind, reason):
    return kind + '-' + hashlib.sha1(reason.encode()).hexdigest()[:10]

def load_results(path):
    try:
        return json.load(open(path))
    except Exception:
        return []

EARLY_SOURCES = [  # 0-1M range runs that preceded the run4 batch (run4 overrides them)
    str(RECORDS / 'perf400k-partial/results.json.gz'),
    str(RECORDS / 'batch1/final-results.json.gz'),
    str(RECORDS / 'rest70k/results.json.gz'),
    'build/cts-vulkan14-ordered-perf/run2/0000000-0400000/segments/*/run*/results.json',
    'build/cts-vulkan14-ordered-perf/run4/0000000-1000000/segments/*/run-attempt*/results.json',
    'build/cts-vulkan14-ordered-perf/run3/1000000-1200000/segments/*/run-attempt*/results.json',
] + [d + '/results.json' for d in (['build/claude-fix/seg24-recover-run2', 'build/claude-fix/seg27-recover-run1', 'build/claude-fix/seg77-recover-run2', 'build/claude-fix/seg77-recover-run3', 'build/claude-fix/seg95-recover-run1', 'build/claude-fix/s95-bisect', 'build/claude-fix/seg100-recover-run1'] + ['build/claude-fix/final-recheck-run1', 'build/claude-fix/bufstore-run1', 'build/claude-fix/texelview-run1', 'build/claude-fix/bufall-run1', 'build/claude-fix/gs-run2', 'build/claude-fix/cubeq-proj', 'build/claude-fix/lclear-run1'])]  # 0-1M recovery and recheck runs (build/claude-fix/tally.py, final-tally.py)

def iter_records(path):
    import gzip
    opener = gzip.open if path.endswith('.gz') else open
    try:
        d = json.load(opener(path))
    except Exception:
        return []
    if isinstance(d, dict):
        return [{'case': k, 'status': v if isinstance(v, str) else v.get('status'),
                 'message': '' if isinstance(v, str) else v.get('message', '')} for k, v in d.items()]
    return d

def build(args):
    catalog = CATALOG.read_text().split()
    status, message = {}, {}
    for pat in EARLY_SOURCES:
        for f in sorted(glob.glob(pat if pat.startswith('/') else f'{ROOT}/{pat}'), key=os.path.getmtime):
            for x in iter_records(f):
                if not x.get('status') or x['status'] in ('Unrun', 'Interrupted'):
                    continue
                status[x['case']] = x['status']; message[x['case']] = x.get('message') or ''
        print('early source', pat, 'statuses', len(status), flush=True)
    for b in BATCHES:
        files = sorted(glob.glob(f'{ROOT}/{b}/segments/*/run*/results.json'), key=os.path.getmtime)
        for f in files:
            for x in load_results(f):
                if x['status'] in ('Unrun', 'Interrupted'):
                    continue
                status[x['case']] = x['status']; message[x['case']] = x.get('message') or ''
        print('batch', b, 'files', len(files), 'statuses', len(status), flush=True)
    # Range records: a Fail/Timeout that a record no longer lists was fixed by a targeted rerun.
    remaining = set()
    for f in list(RECORDS.glob('*/remaining.txt')) + list(RECORDS.glob('*/failures.txt')):
        for line in f.read_text().splitlines():
            if line.startswith('dEQP-VK.'):
                remaining.add(line.split('\t')[0].split()[0])
    for c, s in list(status.items()):
        if s not in ('Pass', 'NotSupported') and c not in remaining:
            status[c] = 'Pass'
    for d in OVERLAYS:
        path = d if d.endswith('results.json') else f'{ROOT}/{d}/results.json'
        for x in load_results(path):
            if x['status'] in ('Unrun', 'Interrupted'):
                continue
            status[x['case']] = x['status']; message[x['case']] = x.get('message') or ''
    groups = collections.defaultdict(list)
    reasons = {}
    for c in catalog:
        s = status.get(c)
        if s is None:
            s, msg = 'NotRun', ''
        else:
            msg = message.get(c, '')
        if s == 'Pass':
            continue
        if s == 'NotSupported':
            reason = norm(msg) or 'NotSupported (no message)'
            gid = group_id('ns', reason)
        else:
            parts = c.split('.')
            fam = '.'.join(parts[1:4] if len(parts) > 5 else parts[1:3])
            reason = f'{s}: {fam}'
            gid = group_id(s.lower(), reason)
        groups[gid].append(c); reasons[gid] = (s, reason)
    old = {}
    if INDEX.exists():
        for g in json.load(open(INDEX))['groups']:
            old[g['id']] = g
    CASES_DIR.mkdir(parents=True, exist_ok=True); PROGRESS_DIR.mkdir(parents=True, exist_ok=True)
    out = []
    for gid, cases in groups.items():
        s, reason = reasons[gid]
        cls, prio = ('bug', 0) if s != 'NotSupported' else classify(reason)
        feat = re.search(r'(VK_[A-Z]+_[a-z0-9_]+)', reason)
        entry = {
            'id': gid, 'status': old.get(gid, {}).get('status', 'open'),
            'kind': 'NotSupported' if s == 'NotSupported' else s,
            'reason': reason, 'feature': feat.group(1) if feat else None,
            'class': cls, 'priority': prio, 'count': len(cases),
            'cases_file': f'missing_tests/cases/{gid}.json',
            'example': cases[0],
            'notes': old.get(gid, {}).get('notes', ''),
            'completed': old.get(gid, {}).get('completed'),
        }
        out.append(entry)
        json.dump({'id': gid, 'reason': reason, 'cases': cases}, open(CASES_DIR / f'{gid}.json', 'w'))
    out.sort(key=lambda g: (g['priority'], -g['count']))
    doc = {
        'generated': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'catalog': str(CATALOG.relative_to(ROOT)), 'catalog_cases': len(catalog),
        'missing_cases': sum(len(v) for v in groups.values()),
        'by_kind': dict(collections.Counter(g['kind'] for g in out)),
        'cases_by_kind': dict(collections.Counter({k: sum(g['count'] for g in out if g['kind'] == k) for k in set(g['kind'] for g in out)})),
        'groups': out,
    }
    json.dump(doc, open(INDEX, 'w'), indent=1)
    print('groups', len(out), 'missing cases', doc['missing_cases'], '->', INDEX)

def load_index():
    return json.load(open(INDEX))

def save_index(doc):
    json.dump(doc, open(INDEX, 'w'), indent=1)

def progress(gid):
    p = PROGRESS_DIR / f'{gid}.json'
    return json.load(open(p)) if p.exists() else {'passing': [], 'last': None}

def save_progress(gid, prog):
    PROGRESS_DIR.mkdir(parents=True, exist_ok=True)
    json.dump(prog, open(PROGRESS_DIR / f'{gid}.json', 'w'), indent=1)

def pick(doc, gid):
    if gid:
        g = next((g for g in doc['groups'] if g['id'] == gid), None)
        if not g:
            sys.exit(f'no group {gid}')
        return g
    for g in doc['groups']:
        if g['status'] == 'open':
            return g
    return None

def run_cases(cases, name, jobs):
    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime('%Y%m%dT%H%M%S')
    lst = RUNS_DIR / f'{name}-{stamp}.txt'; lst.write_text('\n'.join(cases) + '\n')
    out = RUNS_DIR / f'{name}-{stamp}'
    cmd = ['python3', str(ROOT / 'tools/cts-bulk.py'), '--cases', str(lst), '--runner', str(RUNNER),
           '--binary', BINARY, '--output', str(out), '--name', re.sub(r'[^a-z0-9-]', '-', f'mt-{name}-{stamp}'.lower()),
           '--jobs', str(jobs), '--tests-per-group', '50', '--runner-seconds', '1700', '--case-timeout', '60']
    log = out.with_suffix('.log')
    with open(log, 'w') as lf:
        subprocess.run(cmd, cwd=ROOT, stdout=lf, stderr=subprocess.STDOUT)
    res = load_results(out / 'results.json')
    if not res:
        reason = ''
        try:
            evidence = json.load(open(out / 'summary.json')).get('evidence')
        except Exception:
            evidence = None
        for f in ([evidence + '/remote/command.log', evidence + '/guard.log'] if evidence else []) + [str(log)]:
            try:
                lines = [l for l in open(f, errors='replace').read().splitlines() if 'RuntimeError' in l or 'refused' in l or 'FATAL' in l]
                if lines:
                    reason = lines[-1].strip(); break
            except Exception:
                pass
        print(f'run produced no results: {reason or "see " + str(log)}', file=sys.stderr)
    return res, out

def check(args):
    doc = load_index(); g = pick(doc, args.group)
    if not g:
        print('no open groups'); return 0
    cases = json.load(open(ROOT / g['cases_file']))['cases']
    prog = progress(g['id']); passing = set(prog['passing'])
    todo = [c for c in cases if c not in passing][:args.batch]
    if not todo:
        g['status'] = 'completed'; g['completed'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()); save_index(doc)
        print(f'GROUP_COMPLETED {g["id"]} (all {len(cases)} cases recorded passing)'); return 0
    print(f'GROUP {g["id"]} class={g["class"]} kind={g["kind"]} count={g["count"]} remaining={len(cases)-len(passing)}')
    print(f'REASON {g["reason"]}')
    print(f'RUNNING {len(todo)} cases on the M4 ...', flush=True)
    res, out = run_cases(todo, g['id'], args.jobs)
    if not res:
        print(f'RUN_FAILED no results; see {out.with_suffix(".log")}'); return 2
    bad = None
    accepted = set(prog.get('accepted_ns', []))
    for x in res:
        if x['status'] == 'Pass':
            passing.add(x['case'])
        elif x['status'] == 'NotSupported':
            # Not this group's reason any more: the rebuild files it under the new reason.
            passing.add(x['case']); accepted.add(x['case'])
        elif x['status'] not in ('Unrun', 'Interrupted') and bad is None:
            bad = x
    prog['passing'] = sorted(passing); prog['accepted_ns'] = sorted(accepted); prog['last'] = str(out); save_progress(g['id'], prog)
    if accepted:
        print(f'NOTE {len(accepted)} cases of this group are NotSupported for another reason (kept in progress/, regrouped on rebuild)')
    if bad:
        current = ROOT / 'missing_tests' / 'current_case.txt'
        current.write_text(bad['case'] + '\n')
        print(f'STILL_BROKEN {bad["case"]}')
        print(f'CASE_FILE {current}  (full name; use --deqp-caselist-file={current} on the shim)')
        print(f'STATUS {bad["status"]}')
        print(f'MESSAGE {(bad.get("message") or "")[:300]}')
        print(f'EVIDENCE {out}')
        return 1
    left = len([c for c in cases if c not in passing])
    print(f'BATCH_PASSED {len(res)} cases; {left} cases of the group not yet verified'
          + ('' if left else ' -> run mark_as_completed.sh ' + g['id']))
    return 0

def complete(args):
    doc = load_index(); g = pick(doc, args.group)
    if args.wontfix:
        g['status'] = 'wontfix'; g['notes'] = args.wontfix; save_index(doc)
        print(f'WONTFIX {g["id"]}: {args.wontfix}'); return 0
    if not args.force:
        cases = json.load(open(ROOT / g['cases_file']))['cases']
        prog = progress(g['id']); passing = set(prog['passing'])
        todo = [c for c in cases if c not in passing]
        rounds = 0
        while todo and rounds < 6:
            rounds += 1
            print(f'VERIFYING {len(todo)} cases (round {rounds}) ...', flush=True)
            res, out = run_cases(todo, g['id'] + '-verify', args.jobs)
            accepted = set(prog.get('accepted_ns', []))
            for x in res:
                if x['status'] == 'Pass':
                    passing.add(x['case'])
                elif x['status'] == 'NotSupported':
                    passing.add(x['case']); accepted.add(x['case'])
            prog['passing'] = sorted(passing); prog['accepted_ns'] = sorted(accepted); prog['last'] = str(out); save_progress(g['id'], prog)
            bad = [x for x in res if x['status'] not in ('Pass', 'NotSupported', 'Unrun', 'Interrupted')]
            if bad:
                print(f'NOT_COMPLETE {g["id"]}: {len(bad)} cases not Pass, first: {bad[0]["case"]} {bad[0]["status"]} {(bad[0].get("message") or "")[:160]}')
                return 1
            todo = [c for c in cases if c not in passing]
        if todo:
            print(f'NOT_COMPLETE {g["id"]}: {len(todo)} cases never produced a result'); return 1
    g['status'] = 'completed'; g['completed'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    if args.note:
        g['notes'] = args.note
    ns = len(progress(g['id']).get('accepted_ns', []))
    save_index(doc); print(f'COMPLETED {g["id"]} ({g["count"]} cases; {ns} now NotSupported for another reason)'); return 0

def status(args):
    doc = load_index()
    c = collections.Counter(g['status'] for g in doc['groups'])
    n = collections.Counter()
    for g in doc['groups']:
        n[g['status']] += g['count']
    print(f'groups {dict(c)}  cases {dict(n)}  (generated {doc["generated"]})')

def next_(args):
    doc = load_index(); g = pick(doc, args.group)
    print(json.dumps(g, indent=1) if g else 'no open groups')

p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
sub = p.add_subparsers(dest='cmd', required=True)
sub.add_parser('build').set_defaults(fn=build)
s = sub.add_parser('next'); s.add_argument('group', nargs='?'); s.set_defaults(fn=next_)
s = sub.add_parser('check'); s.add_argument('group', nargs='?'); s.add_argument('--batch', type=int, default=50); s.add_argument('--jobs', type=int, default=12); s.set_defaults(fn=check)
s = sub.add_parser('complete'); s.add_argument('group'); s.add_argument('--wontfix'); s.add_argument('--force', action='store_true'); s.add_argument('--note'); s.add_argument('--jobs', type=int, default=12); s.set_defaults(fn=complete)
sub.add_parser('status').set_defaults(fn=status)
a = p.parse_args(); sys.exit(a.fn(a) or 0)
