#!/usr/bin/env python3
"""Run one side of the paired full Vulkan regression campaign.

Start one process per machine concurrently. Frozen catalog indices, first
terminal outcomes and all interrupted evidence survive restarts. Fatal or
incomplete segments stop for explicit evidence review; nothing is auto-retried.
"""
import argparse
from collections import Counter
import fcntl
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import sqlite3
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT/'build/cts-vulkan14-ordered/catalog'
RUNNER = ROOT/'reference/deqp-runner/target/aarch64-unknown-linux-gnu/release/deqp-runner'
PREFIX = '/opt/asahi-m3m4-buffer-diagnostic-complete'
BINARY = '/root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit'
INCOMPLETE = {'Unrun', 'Interrupted'}

def save(path, value):
    tmp = path.with_suffix(path.suffix+'.tmp')
    tmp.write_text(json.dumps(value, indent=2)+'\n')
    tmp.replace(path)

def selection(names, indices, catalog):
    raw = ''.join(names[i]+'\n' for i in indices).encode()
    return raw, dict(schema=1, cases_sha256=hashlib.sha256(raw).hexdigest(),
        provenance=catalog, cases=[dict(case=names[i], catalog_index=i,
        classification='unclassified', requirements=[], rule='Full frozen Linux catalog') for i in indices])

def family(name):
    # A render/compute family followed by CPU-only object-query tests can
    # exhaust a GPU-retirement lease while CTS is still progressing. Start a
    # fresh bounded lease at each family boundary instead of weakening renewal.
    return '.'.join(name.split('.')[:3])

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--machine', choices=['m3','m4'], required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--run', action='store_true')
    p.add_argument('--review', action='append', default=[], metavar='SEGMENT=REASON')
    a = p.parse_args()
    a.output = a.output.resolve()
    a.output.mkdir(parents=True, exist_ok=True)
    lock = (ROOT/'build'/('cts-paired-'+a.machine+'.lock')).open('w')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    host = 'root@fedora-m3' if a.machine == 'm3' else 'root@omarchy-m4'
    ssh = ['ssh','-oConnectTimeout=10',host]
    def remote(argv):
        return subprocess.check_output(ssh+[shlex.join(argv)], text=True, timeout=40)
    catalog = json.loads((CATALOG/'catalog.json').read_text())
    raw = (CATALOG/'catalog.txt').read_bytes()
    assert hashlib.sha256(raw).hexdigest() == catalog['catalog_sha256']
    names = raw.decode().splitlines()
    assert len(names) == len(set(names)) == catalog['count']
    env = dict(VK_DRIVER_FILES=PREFIX+'/asahi.json', VK_ICD_FILENAMES=PREFIX+'/asahi.json',
        LD_LIBRARY_PATH=PREFIX+'/lib', DRIRC_CONFIGDIR=PREFIX+'/share/drirc.d',
        ASAHI_M3_EXPERIMENTAL='1', ASAHI_M4_EXPERIMENTAL='1', ASAHI_MESA_DEBUG='nocompress',
        HK_DEBUG_LOG='0', AGX_MESA_DEBUG='', MESA_SHADER_CACHE_DIR='/root/cts-paired-cache/mesa',
        VK_LAYER_PATH='/var/empty', VK_IMPLICIT_LAYER_PATH='/var/empty')
    config = dict(machine=a.machine, catalog=catalog, environment=env, binary=BINARY,
        runner_sha256=hashlib.sha256(RUNNER.read_bytes()).hexdigest(),
        case_timeout=60, jobs=1, mesa_commit='3c7ec4838f232f77dd9d4ce77c7c077ecf5e5d0d',
        note='Default compiler settings; no Satisfactory opt-in profile. No result/capability filtering.')
    if (a.output/'campaign.json').exists():
        assert json.loads((a.output/'campaign.json').read_text()) == config, 'Campaign changed'
    else:
        save(a.output/'campaign.json', config)
    save(a.output/'environment.json', env)
    db = sqlite3.connect(a.output/'results.sqlite3')
    db.execute('CREATE TABLE IF NOT EXISTS outcomes (idx INTEGER PRIMARY KEY, name TEXT, status TEXT, detail TEXT, segment TEXT)')
    db.execute('CREATE TABLE IF NOT EXISTS segments (path TEXT PRIMARY KEY, healthy INTEGER, summary TEXT)')
    db.execute('CREATE TABLE IF NOT EXISTS reviews (path TEXT PRIMARY KEY, reason TEXT)')
    for review in a.review:
        path, reason = review.split('=',1)
        assert reason.strip() and db.execute('SELECT 1 FROM segments WHERE path=?',(path,)).fetchone()
        db.execute('INSERT OR REPLACE INTO reviews VALUES (?,?)',(path,reason))
    db.commit()
    history_path=ROOT/'build/cts-full-confirm3-20260920/results.sqlite3'
    history=sqlite3.connect('file:'+str(history_path)+'?mode=ro',uri=True)
    def report(state, active=None):
        counts = dict(db.execute('SELECT status,COUNT(*) FROM outcomes GROUP BY status'))
        save(a.output/'progress.json', dict(state=state, machine=a.machine, total=len(names),
            completed=sum(counts.values()), counts=counts, updated_unix=time.time(), active=active))
        with (a.output/'failures.tsv').open('w') as f:
            f.write('index\tstatus\tcase\tsegment\n')
            for row in db.execute("SELECT idx,status,name,segment FROM outcomes WHERE status NOT IN ('Pass','NotSupported') ORDER BY idx"):
                f.write('\t'.join(map(str,row))+'\n')
        print(json.dumps(dict(machine=a.machine,state=state,completed=sum(counts.values()),counts=counts,active=active)),flush=True)
    def pin():
        code = '''import hashlib,json,os
from pathlib import Path
module='/root/m3-gpu-tools/asahi.ko' if MACHINE=='m3' else '/lib/modules/'+os.uname().release+'/kernel/drivers/gpu/drm/asahi/asahi.ko'
paths=[BINARY,PREFIX+'/lib/libvulkan_asahi.so',module]
params={p.name:p.read_text().strip() for p in Path('/sys/module/asahi/parameters').glob('*')}
print(json.dumps(dict(kernel=os.uname().release,files={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in paths},parameters=params)))
'''.replace('MACHINE',repr(a.machine)).replace('BINARY',repr(BINARY)).replace('PREFIX',repr(PREFIX))
        current = json.loads(remote(['python3','-c',code]))
        path = a.output/'identity.json'
        if path.exists(): assert json.loads(path.read_text()) == current, 'Installed identity changed'
        else: save(path,current)
        assert current['files'][BINARY] == '36bcc144d4b3c0edc5b638f20331fbc4e1be388d89da652890af870b8e5697dc'
        assert current['files'][PREFIX+'/lib/libvulkan_asahi.so'] == '372e6351a85b681e7d42731f4dd1c988e0fc7a640a6859a82ebcfc07c791999f'
    # Ingest complete host evidence before scheduling, including after a host interruption.
    while True:
        for path in sorted((a.output/'segments').glob('*/summary.json')):
            segment=path.parent; key=segment.name
            if db.execute('SELECT 1 FROM segments WHERE path=?',(key,)).fetchone(): continue
            summary=json.loads(path.read_text()); rows=json.loads((segment/'results.json').read_text())
            for row in rows:
                idx=row['eligibility']['catalog_index']; assert names[idx] == row['case']
                if row['status'] not in INCOMPLETE:
                    db.execute('INSERT INTO outcomes VALUES (?,?,?,?,?)',(idx,row['case'],row['status'],json.dumps(row),key))
            db.execute('INSERT INTO segments VALUES (?,?,?)',(key,summary['healthy'],json.dumps(summary)))
            db.commit()
        blocked=[r[0] for r in db.execute('SELECT path FROM segments WHERE healthy=0 AND path NOT IN (SELECT path FROM reviews)')]
        if blocked: report('needs-review',blocked); return 2
        orphaned=[p.name for p in (a.output/'segments').glob('*') if p.is_dir() and not (p/'summary.json').exists()]
        if orphaned:
            report('infrastructure-error',dict(orphaned=orphaned,
                reason='Collect and review unfinished segment evidence before any continuation'))
            return 3
        done={r[0] for r in db.execute('SELECT idx FROM outcomes')}
        if len(done)==len(names): report('complete'); return 0
        if not a.run: report('ready'); return 0
        if (a.output/'STOP').exists(): report('stopped-at-boundary'); return 0
        if shutil.disk_usage(a.output).free < 2*1024**3:
            report('host-disk-reserve'); return 3
        pin()
        start=next(i for i in range(len(names)) if i not in done)
        wsi=names[start].startswith('dEQP-VK.wsi.')
        size=16 if a.machine=='m3' else (100 if wsi else 10000)
        if a.machine=='m3' and not wsi:
            # CPU-only API cases cannot renew a GPU-retirement lease. Estimate
            # a 15-second batch from the last successful SAME-family segment;
            # start new families small, and grow by at most twofold. All cases
            # retain their 60-second bound and original catalog indices.
            previous=db.execute('SELECT summary FROM segments WHERE healthy=1 ORDER BY rowid DESC LIMIT 1').fetchone()
            if previous:
                prior=json.loads(previous[0])
                if prior.get('family')==family(names[start]):
                    n=prior['selected'];elapsed=max(.1,prior['runner_seconds'])
                    size=max(1,min(4096,n*2,int(n*15/elapsed)))
        if a.machine=='m4' and not wsi:
            # A slower family may outlast the 900-second segment bound even
            # though individual cases keep passing. Target half that bound
            # using the most recent measured rate, including interrupted runs.
            previous=db.execute('SELECT summary FROM segments ORDER BY rowid DESC LIMIT 1').fetchone()
            if previous:
                prior=json.loads(previous[0])
                elapsed=prior.get('runner_seconds',0)
                completed=prior.get('completed',0)
                if elapsed>0 and completed>0:
                    size=min(size,max(1,int(completed*450/elapsed)))
        budget,scale=(15,4) if a.machine=='m3' else (450,2)
        indices=[]
        historical_seconds={idx:json.loads(detail).get('seconds') or 0 for idx,detail in
            history.execute('SELECT idx,detail FROM outcomes WHERE idx>=? AND idx<?',(start,start+size))} if history else {}
        estimated=0.0
        for i in range(start,min(len(names),start+size)):
            if names[i].startswith('dEQP-VK.wsi.')!=wsi:break
            if a.machine=='m3' and family(names[i]) != family(names[start]):break
            if i not in done:
                # Prior durations affect grouping only, never coverage or verdict.
                # Leave room for slower M3 uncached sparse-VA scans; one long
                # case remains a singleton with the original case/guard bounds.
                cost=max(.001,historical_seconds.get(i,0))*scale
                if history and indices and estimated+cost>budget:break
                indices.append(i);estimated+=cost
        key=f'{start:07d}-{indices[-1]+1:07d}'
        attempt=0
        while (a.output/'segments'/key).exists():
            attempt+=1;key=f'{start:07d}-{indices[-1]+1:07d}-attempt{attempt}'
        segment=a.output/'segments'/key;segment.mkdir(parents=True)
        raw,metadata=selection(names,indices,catalog)
        if history:
            metadata['scheduling']=dict(historical_campaign=str(history_path.parent),
                estimated_seconds=estimated,budget_seconds=budget,scale=scale,
                scope='Grouping only; all original indices and outcomes retained')
        (segment/'cases.txt').write_bytes(raw);save(segment/'cases.meta.json',metadata)
        report('running',dict(segment=key,start=start,end=indices[-1]+1,headless=wsi))
        if a.machine=='m4':
            cmd=[sys.executable,str(ROOT/'tools/cts-bulk.py'),'--cases',str(segment/'cases.txt'),
                '--output',str(segment/'run'),'--name','paired-m4-'+key,'--runner',str(RUNNER),
                '--binary',BINARY,'--mesa-source',str(ROOT/'mesa/m3m4-merged'),'--jobs','1',
                '--cpu-max-khz','4104000','--runner-seconds','900','--case-timeout','60']
            for k,v in env.items():cmd+=['--env',k+'='+v]
            if wsi:cmd+=['--headless']
            with (segment/'controller.log').open('w') as log:
                rc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT).returncode
            if not (segment/'run/summary.json').exists():report('infrastructure-error',key);return 3
            summary=json.loads((segment/'run/summary.json').read_text())
            summary['healthy']=not rc and summary['guard_verdict']=='pass' and summary['completed']==len(indices)
            rows=json.loads((segment/'run/results.json').read_text())
            evidence=Path(summary['evidence'])
            if json.loads((evidence/'report.json').read_text()).get('evidence_fetched'):
                subprocess.run(['rsync','-azc','--remove-source-files',host+':/root/asahi-evidence/'+evidence.name+'/',str(evidence/'remote')+'/'],check=True,timeout=180)
        else:
            base='/root/cts-paired/'+a.output.name+'/'+key
            remote(['mkdir','-p',base])
            subprocess.run(['scp',str(segment/'cases.txt'),str(segment/'cases.meta.json'),str(a.output/'environment.json'),host+':'+base+'/'],check=True)
            command=['python3','/root/m3-gpu-tools/gpu/guard.py','--gpu-progress-seconds','120','--keep-cpu-clocks',base+'/run','--',
                'python3','/root/m3-gpu-tools/gpu/vk-cts-bulk.py',base+'/run','--cases',base+'/cases.txt',
                '--environment',base+'/environment.json','--binary',BINARY,'--runner','/root/cts-paired/deqp-runner','--cache','/root/cts-paired-cache/deqp']
            with (segment/'controller.log').open('w') as log:
                rc=subprocess.run(ssh+[shlex.join(command)],stdout=log,stderr=subprocess.STDOUT).returncode
            subprocess.run(['rsync','-az',host+':'+base+'/run/',str(segment/'run')+'/'],check=True,timeout=180)
            out=segment/'run'
            if not (out/'results.json').exists():report('infrastructure-error',key);return 3
            rows=json.loads((out/'results.json').read_text())
            guard=json.loads((out/'result.json').read_text())
            batch=json.loads((out/'batch-result.json').read_text()) if (out/'batch-result.json').exists() else {}
            summary=dict(healthy=not rc and guard['phase']=='complete' and batch.get('status')=='complete',
                guard=guard,batch=batch,counts=Counter(r['status'] for r in rows),
                family=family(names[start]), selected=len(indices),
                runner_seconds=json.loads((out/'timing.json').read_text())['elapsed_seconds'])
        save(segment/'results.json',rows);save(segment/'summary.json',summary)

if __name__ == '__main__':
    sys.exit(main())
