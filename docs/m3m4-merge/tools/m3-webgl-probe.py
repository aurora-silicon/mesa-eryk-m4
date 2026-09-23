#!/usr/bin/env python3
"""Run real Firefox WebGL1/2 pixel checks under the bounded M3 guard."""
import hashlib,http.server,json,os,pwd,re,signal,subprocess,sys,threading,time
from pathlib import Path
out=Path(sys.argv[1]);assert 'guard.py' in Path('/proc',str(os.getppid()),'cmdline').read_text()
uid=pwd.getpwnam('eryk').pw_uid;env={}
pid=subprocess.check_output(['pgrep','-u',str(uid),'-x','plasmashell'],text=True).strip()
for item in Path('/proc',pid,'environ').read_bytes().split(b'\0'):
 if b'=' in item:
  k,v=item.split(b'=',1);env[k.decode()]=v.decode()
for k in ['NOTIFY_SOCKET','INVOCATION_ID','JOURNAL_STREAM','SYSTEMD_EXEC_PID']:env.pop(k,None)
library=Path(json.loads(Path(env['VK_DRIVER_FILES']).read_text())['ICD']['library_path'])
assert library.is_absolute() and library.is_file()
mesa_prefix=str(library.parent)+'/'
page=Path(__file__).with_name('webgl.html').read_bytes();received=threading.Event();report=None
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.end_headers();self.wfile.write(page)
 def do_POST(self):
  global report
  assert self.path=='/result'
  count=int(self.headers['Content-Length']);assert count<65536
  report=json.loads(self.rfile.read(count));(out/'webgl-result.json').write_text(json.dumps(report,indent=2)+'\n')
  self.send_response(200);self.end_headers();self.wfile.write(b'OK');received.set()
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
url=f'http://127.0.0.1:{server.server_port}/';(out/'url.txt').write_text(url+'\n')
before=subprocess.check_output(['dmesg','--color=never'],text=True);child=None;complete=False
try:
 with (out/'firefox.log').open('w') as log:
  child=subprocess.Popen(['runuser','-u','eryk','--','firefox','--new-window',url],env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  assert received.wait(18),'Browser did not report WebGL results within 18 seconds'
 assert report['pass'],json.dumps(report)
 assert {t['api'] for t in report['tests']}=={'webgl','webgl2'}
 assert all(len(t['draws'])==16 for t in report['tests'])
 maps={}
 for proc in Path('/proc').iterdir():
  if not proc.name.isdecimal():continue
  try:
   if proc.stat().st_uid!=uid:continue
   command=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
   if 'firefox' not in command:continue
   libs=sorted(set(line.split()[-1] for line in (proc/'maps').read_text().splitlines() if mesa_prefix in line))
   if libs:maps[proc.name]={'command':command,'libraries':libs}
  except (FileNotFoundError,PermissionError,ProcessLookupError):pass
 (out/'firefox-gpu-maps.json').write_text(json.dumps(maps,indent=2)+'\n')
 assert any(str(library) in v['libraries'] for v in maps.values()),'Firefox did not map the selected native Vulkan driver'
 (out/'mesa-identity.json').write_text(json.dumps(dict(library=str(library),sha256=hashlib.sha256(library.read_bytes()).hexdigest()),indent=2)+'\n')
 time.sleep(1)
 shot=Path('/home/eryk/.cache/m3-webgl-validation.png');shot.unlink(missing_ok=True)
 with (out/'screenshot.log').open('w') as log:
  subprocess.run(['runuser','-u','eryk','--','spectacle','-b','-n','-f','-o',str(shot)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=6)
 (out/'desktop.png').write_bytes(shot.read_bytes());shot.unlink()
 after=subprocess.check_output(['dmesg','--color=never'],text=True);new=after[len(before):] if after.startswith(before) else after
 assert not re.search(r'M3 .*rejected|IOCTL: submit failed|execution failed|firmware error',new),new[-3000:]
 log=(out/'firefox.log').read_text();assert not re.search(r'VK_ERROR|CONTEXT_LOST|DEVICE LOST|failed to compile',log),log[-3000:]
 complete=True
 print('PASS: Firefox WebGL 1 and WebGL 2, 16 pixel-checked draws each; browser left open',flush=True)
finally:
 server.shutdown();server.server_close()
 if not complete and child is not None and child.poll() is None:
  os.killpg(child.pid,signal.SIGTERM)
