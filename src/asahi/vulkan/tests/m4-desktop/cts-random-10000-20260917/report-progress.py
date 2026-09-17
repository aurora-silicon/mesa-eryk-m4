from pathlib import Path
import collections,datetime,hashlib,json
root=Path('/home/council/dev/dirtyroom_m4'); batch=Path(__file__).resolve().parent
meta=json.loads((batch/'cases.meta.json').read_text()); selection=json.loads((batch/'selection.json').read_text())
rows={r['case']:{'case':r['case'],'catalog_index':r['catalog_index'],'status':'Unrun','attempts':[]} for r in meta['cases']}
paths=sorted((batch/'segments').glob('*/run/summary.json'),key=lambda p:json.loads(p.read_text())['evidence'])
segments=[]; times=[]; boots=set(); identities=[]
for p in paths:
 s=json.loads(p.read_text());e=Path(s['evidence']);segments.append(s)
 before=json.loads((e/'target-before.json').read_text());boots.add(before['boot_id'])
 identities.append({k:before['files'].get(k) for k in ['/opt/asahi-m4-port/lib/libvulkan_asahi.so','/root/cts-1.4.6.2/deqp-vk','/lib/modules/7.1.9-dirtyroom-asahi2+/kernel/drivers/gpu/drm/asahi/asahi.ko']})
 t=e/'remote/cts/timing.json'
 if t.exists():times.append(json.loads(t.read_text()))
 for x in json.loads((p.parent/'results.json').read_text()):
  row=rows[x['case']];row['attempts'].append({**x,'evidence':str(e)})
  if x['status'] not in ('Unrun','Interrupted'):
   assert row['status']=='Unrun',('Unexpected repeat terminal result',x['case'])
   row.update(status=x['status'],message=x.get('message',''))
assert all(i==identities[0] for i in identities),'Driver/kernel/CTS changed during timing run'
counts=collections.Counter(r['status'] for r in rows.values());complete=counts['Unrun']==0
summary=dict(selected=len(rows),completed=len(rows)-counts['Unrun'],counts=dict(counts),seed=selection['seed'],cases_sha256=selection['cases_sha256'],catalog_sha256=selection['catalog_sha256'],same_binary_identity=identities[0] if identities else None,boots=sorted(boots),segments=len(segments),runner_seconds=sum(t['elapsed_seconds'] for t in times),guarded_segment_wall_seconds=sum(s['elapsed_seconds'] for s in segments),staging_seconds=sum(s['preparation_seconds'] for s in segments),first_runner_start_unix=min([t['started_unix'] for t in times],default=None),last_runner_finish_unix=max([t['finished_unix'] for t in times],default=None),peak_temperature_c=max([s.get('peak_temperature_c') or 0 for s in segments],default=None),complete=complete,limitation='Same binaries across interrupted segments and recovery boots; not an uninterrupted throughput benchmark. Skips are not passes.')
if times:summary['first_to_last_runner_wall_seconds']=summary['last_runner_finish_unix']-summary['first_runner_start_unix']
(batch/'results.json').write_text(json.dumps(list(rows.values()),indent=2)+'\n');(batch/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
for name,chosen in [('failures.tsv',[r for r in rows.values() if r['status'] not in ('Pass','NotSupported','Unrun')]),('not-supported.tsv',[r for r in rows.values() if r['status']=='NotSupported'])]:
 with (batch/name).open('w') as f:
  f.write('catalog_index\tstatus\tcase\tmessage\n')
  for r in chosen:f.write(f"{r['catalog_index']}\t{r['status']}\t{r['case']}\t"+r.get('message','').replace('\n',' ').replace('\t',' ')+'\n')
print(json.dumps(summary,indent=2))
