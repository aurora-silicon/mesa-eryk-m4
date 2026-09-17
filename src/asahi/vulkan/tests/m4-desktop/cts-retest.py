#!/usr/bin/env python3
"""Resume a full frozen-catalog retest; preserve every first-attempt failure.

Only cts-bulk executes hardware, under its existing watchdog/thermal guards.
Fatal/incomplete runs require an explicit recorded review before continuation.
No driver/test patches, retries of terminal results, or automatic recovery.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shlex
import sqlite3
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
INCOMPLETE = {'Unrun', 'Interrupted'}


def write(path, value):
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(value, indent=2) + '\n')
    tmp.replace(path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--catalog', type=Path, default=ROOT/'build/cts-vulkan14-ordered/catalog')
    p.add_argument('--runner', type=Path, default=ROOT/'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner')
    p.add_argument('--binary', required=True,
                   help='Explicit CTS baseline: use the same executable as the comparison run')
    p.add_argument('--run', action='store_true', help='Execute remaining cases; otherwise only collect/report')
    p.add_argument('--review', action='append', default=[], metavar='RUN_DIRECTORY=REASON')
    a = p.parse_args()
    a.output = a.output.resolve()
    a.output.mkdir(parents=True, exist_ok=True)
    # Avoid two controllers launching GPU work or modifying the ledger together.
    import fcntl
    lock = (a.output/'controller.lock').open('w')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    catalog = json.loads((a.catalog/'catalog.json').read_text())
    raw = (a.catalog/'catalog.txt').read_bytes()
    assert hashlib.sha256(raw).hexdigest() == catalog['catalog_sha256']
    names = raw.decode('ascii').splitlines()
    assert len(names) == catalog['count'] == len(set(names))
    del raw
    db = sqlite3.connect(a.output/'results.sqlite3')
    db.execute('CREATE TABLE IF NOT EXISTS runs (path TEXT PRIMARY KEY, summary TEXT NOT NULL)')
    db.execute('CREATE TABLE IF NOT EXISTS outcomes (idx INTEGER PRIMARY KEY, name TEXT NOT NULL, status TEXT NOT NULL, detail TEXT NOT NULL, run TEXT NOT NULL)')
    db.execute('CREATE TABLE IF NOT EXISTS reviews (path TEXT PRIMARY KEY, reason TEXT NOT NULL, time REAL NOT NULL)')
    for item in a.review:
        path, reason = item.split('=', 1)
        assert reason.strip()
        path = str(Path(path).resolve())
        assert Path(path).is_relative_to(a.output)
        db.execute('INSERT OR REPLACE INTO reviews VALUES (?, ?, ?)', (path, reason, time.time()))
    db.commit()

    def ingest():
        for result in sorted(a.output.glob('segments/**/run/results.json')):
            run = str(result.parent)
            if db.execute('SELECT 1 FROM runs WHERE path=?', (run,)).fetchone():
                continue
            summary = json.loads(result.with_name('summary.json').read_text())
            evidence = Path(summary['evidence'])
            identity = json.loads((evidence/'target-before.json').read_text())
            keys = ['/opt/asahi-m4-port/lib/libvulkan_asahi.so',
                    '/opt/asahi-m4-port/lib/libgallium-26.1.4.so', a.binary,
                    f"/lib/modules/{identity['kernel']}/kernel/drivers/gpu/drm/asahi/asahi.ko"]
            pinned = dict(kernel=identity['kernel'], files={k: identity['files'][k] for k in keys},
                          catalog_sha256=catalog['catalog_sha256'], count=len(names),
                          runner_sha256=hashlib.sha256(a.runner.read_bytes()).hexdigest())
            pin = a.output/'identity.json'
            if pin.exists():
                assert json.loads(pin.read_text()) == pinned, 'Retest identity changed'
            else:
                write(pin, pinned)
            for row in json.loads(result.read_text()):
                idx = row['eligibility']['catalog_index']
                assert names[idx] == row['case']
                if row['status'] in INCOMPLETE:
                    continue  # Raw segment evidence retains all interrupted attempts.
                db.execute('INSERT INTO outcomes VALUES (?, ?, ?, ?, ?)',
                           (idx, row['case'], row['status'], json.dumps(row), run))
            db.execute('INSERT INTO runs VALUES (?, ?)', (run, json.dumps(summary)))
            db.commit()

    def report(state, active=None):
        counts = dict(db.execute('SELECT status, COUNT(*) FROM outcomes GROUP BY status'))
        n = sum(counts.values())
        output = dict(state=state, total=len(names), completed=n, remaining=len(names)-n,
                      counts=counts, updated_unix=time.time(), active=active,
                      catalog_sha256=catalog['catalog_sha256'],
                      note='Raw first-attempt outcomes. NotSupported is not Pass. Timeouts/crashes remain recorded.')
        write(a.output/'progress.json', output)
        tmp = a.output/'failures.tsv.tmp'
        with tmp.open('w') as stream:
            writer = csv.writer(stream, delimiter='\t')
            writer.writerow(['catalog_index', 'status', 'case', 'message', 'results_file', 'qpa'])
            for idx, name, status, detail, run in db.execute("SELECT * FROM outcomes WHERE status NOT IN ('Pass','NotSupported') ORDER BY idx"):
                row = json.loads(detail)
                writer.writerow([idx, status, name, row.get('message','').replace('\n',' '),
                                 str(Path(run)/'results.json'), row.get('qpa','')])
        tmp.replace(a.output/'failures.tsv')
        print(json.dumps(output), flush=True)
        return output

    def archive():
        # rsync verifies transferred data before removing source files. Keep the
        # canonical host evidence, including passing cases; never delete it.
        for run, raw_summary in db.execute('SELECT * FROM runs'):
            marker = Path(run)/'target-archived.json'
            if marker.exists():
                continue
            summary = json.loads(raw_summary)
            ev = Path(summary['evidence'])
            report_path = ev/'report.json'
            if not json.loads(report_path.read_text()).get('evidence_fetched'):
                raise RuntimeError('Cannot archive unfetched evidence')
            with (Path(run)/'archive.log').open('a') as log:
                subprocess.run(['rsync', '-azc', '--remove-source-files',
                                'root@omarchy-m4:/root/asahi-evidence/'+ev.name+'/', str(ev/'remote')+'/'],
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
            write(marker, dict(host_evidence=str(ev/'remote'), time=time.time()))

    def check_identity():
        pinned = json.loads((a.output/'identity.json').read_text())
        script = ('import hashlib,json,os,shutil; from pathlib import Path; '
                  'keys='+repr(list(pinned['files']))+'; '
                  'print(json.dumps(dict(kernel=os.uname().release, '
                  'files={k:hashlib.sha256(Path(k).read_bytes()).hexdigest() for k in keys}, '
                  'free_bytes=shutil.disk_usage("/root").free)))')
        current = json.loads(subprocess.check_output(['ssh','-o','ConnectTimeout=10',
                              'root@omarchy-m4','python3 -c '+shlex.quote(script)], timeout=30))
        assert current['kernel'] == pinned['kernel'] and current['files'] == pinned['files'], 'Target changed during retest'
        assert hashlib.sha256(a.runner.read_bytes()).hexdigest() == pinned['runner_sha256']
        assert current['free_bytes'] > 1024**3, 'Archive evidence before disk fills'

    while True:
        ingest()
        progress = report('collecting')
        if not a.run:
            break
        fatal = []
        for run, summary in db.execute('SELECT * FROM runs'):
            s = json.loads(summary)
            if (s['guard_verdict'] != 'pass' or s['command_returncode'] or s['completed'] != s['selected']):
                if not db.execute('SELECT 1 FROM reviews WHERE path=?', (run,)).fetchone():
                    fatal.append(run)
        if fatal:
            report('needs-review', fatal)
            return 2
        archive()
        if progress['remaining'] == 0:
            report('complete')
            break
        check_identity()
        done = bytearray(len(names))
        for (idx,) in db.execute('SELECT idx FROM outcomes'):
            done[idx] = 1
        start = done.index(0)
        wsi = names[start].startswith('dEQP-VK.wsi.')
        end = min(((start//10000)+1)*10000, len(names), start+(100 if wsi else 10000))
        # Keep presentation pacing separate and record its environment.
        for i in range(start+1, end):
            if names[i].startswith('dEQP-VK.wsi.') != wsi:
                end = i
                break
        indices = [i for i in range(start, end) if not done[i]]
        segment = a.output/'segments'/f'{start:07d}-{end:07d}'
        attempt = 0
        while segment.exists():
            attempt += 1
            segment = a.output/'segments'/f'{start:07d}-{end:07d}-remaining{attempt}'
        segment.mkdir(parents=True)
        raw = ''.join(names[i]+'\n' for i in indices).encode()
        (segment/'cases.txt').write_bytes(raw)
        write(segment/'cases.meta.json', dict(schema=1, cases_sha256=hashlib.sha256(raw).hexdigest(),
              provenance=dict(catalog_sha256=catalog['catalog_sha256'], cts_revision=catalog['cts_revision'],
                              start=start, end=end, method='Remaining uncompleted indices; no terminal retries'),
              cases=[dict(case=names[i],catalog_index=i,classification='unclassified',requirements=[],
                          rule='Frozen complete Linux catalog') for i in indices]))
        argv = [sys.executable,str(ROOT/'tools/cts-bulk.py'),'--cases',str(segment/'cases.txt'),
                '--runner',str(a.runner),'--binary',a.binary,'--output',str(segment/'run'),
                '--name',f'vk14-retest-{segment.name}','--jobs','1' if wsi else '20',
                '--cpu-max-khz','4104000','--runner-seconds','900','--case-timeout','60']
        if wsi:
            argv += ['--headless']
        report('running',dict(start=start,end=end,selected=len(indices),headless=wsi,path=str(segment)))
        with (segment/'controller.log').open('w') as log:
            rc = subprocess.run(argv,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT).returncode
        if not (segment/'run/results.json').exists():
            report('infrastructure-error',dict(path=str(segment),returncode=rc))
            return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())
