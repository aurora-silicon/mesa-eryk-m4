#!/usr/bin/env python3
"""Validate the active local KDE session; run under the M3 guard."""
import hashlib,json,os,pwd,re,signal,subprocess,sys,time
from pathlib import Path
out=Path(sys.argv[1]);assert 'guard.py' in Path('/proc',str(os.getppid()),'cmdline').read_text()
user='eryk';uid=pwd.getpwnam(user).pw_uid
started=time.time();before=subprocess.check_output(['dmesg','--color=never'],text=True)
env=dict(os.environ,DBUS_SESSION_BUS_ADDRESS=f'unix:path=/run/user/{uid}/bus',XDG_RUNTIME_DIR=f'/run/user/{uid}',WAYLAND_DISPLAY='wayland-0')
for line in Path('/run/m3-gpu/graphics.env').read_text().splitlines():
 if line and not line.startswith('#'):
  key,value=line.split('=',1);env[key]=value
prefix=['runuser','-u',user,'--'];query=prefix+['busctl','--user','--json=short','call','org.kde.KWin','/KWin','org.kde.KWin','supportInformation']
app=None
try:
 if '--existing' not in sys.argv:
  # Restarting only the display manager leaves the systemd user compositor
  # alive on its old VT. Clear that session before starting normal autologin.
  subprocess.run(['systemctl','stop','plasmalogin'],check=True,timeout=8)
  subprocess.run(['loginctl','terminate-user',user],check=True,timeout=8)
  for _ in range(40):
   if subprocess.run(['pgrep','-u',str(uid),'-x','kwin_wayland'],capture_output=True).returncode:break
   time.sleep(.1)
  else:raise RuntimeError('Old KWin survived session termination')
  subprocess.run(['systemctl','start','plasmalogin'],check=True,timeout=8)
 end=time.monotonic()+16
 while time.monotonic()<end:
  try:r=subprocess.run(query,env=env,capture_output=True,text=True,timeout=2)
  except subprocess.TimeoutExpired:continue
  if r.returncode==0:
   info=json.loads(r.stdout)['data'][0]
   shell=subprocess.run(prefix+['pgrep','-u',str(uid),'plasmashell'],capture_output=True)
   if 'OpenGL renderer string: zink' in info and 'Apple M3' in info and shell.returncode==0:break
  time.sleep(.5)
 else:raise RuntimeError('Real KDE GPU session did not become ready')
 # The user bus alone can still belong to a stale, invisible compositor.
 pids=subprocess.check_output(['pgrep','-u',str(uid),'-x','kwin_wayland'],text=True).split()
 assert len(pids)==1,pids
 library=Path(json.loads(Path(env['VK_DRIVER_FILES']).read_text())['ICD']['library_path'])
 assert library.is_absolute() and library.is_file()
 mappings=Path('/proc',pids[0],'maps').read_text()
 required_libraries=[library,library.parent/'libgallium-26.1.4.so',
                     library.parent/'libEGL_mesa.so.0',library.parent/'libgbm.so.1',
                     library.parent/'gbm/dri_gbm.so']
 for required in required_libraries:
  assert any(line.endswith(' '+str(required.resolve())) for line in mappings.splitlines()),str(required)
 (out/'compositor-maps.txt').write_text(mappings)
 (out/'mesa-identity.json').write_text(json.dumps(dict(library=str(library),sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
  mapped_libraries={str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in required_libraries}),indent=2)+'\n')
 raw=dict(x.split(b'=',1) for x in Path('/proc',pids[0],'environ').read_bytes().split(b'\0') if b'=' in x)
 session=raw[b'XDG_SESSION_ID'].decode()
 properties=subprocess.check_output(['loginctl','show-session',session,'-p','Active','-p','State','-p','VTNr','-p','Type'],text=True)
 props=dict(line.split('=',1) for line in properties.splitlines())
 assert props['Active']=='yes' and props['State']=='active' and props['Type']=='wayland',props
 assert Path('/sys/class/tty/tty0/active').read_text().strip()=='tty'+props['VTNr'],props
 (out/'active-session.txt').write_text('KWin PID='+pids[0]+' session='+session+'\n'+properties)
 (out/'kwin-support.txt').write_text(info)
 subprocess.run(prefix+['pgrep','-u',str(uid),'plasmashell'],check=True,capture_output=True)
 if '--app' in sys.argv:
  app=subprocess.Popen(prefix+['konsole','--separate','-e','sh','-c',
   "printf 'KDE Plasma on Apple M3 Pro\\nKWin compositor: Zink / Vulkan / native M3 GPU\\n'; sleep 20"],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
  time.sleep(2)
 # Spectacle is the installed, permitted KWin screenshot client.
 shot=Path(f'/home/{user}/m3-kde-validation.png');shot.unlink(missing_ok=True)
 with (out/'screenshot.log').open('w') as log:
  subprocess.run(prefix+['spectacle','-b','-n','-f','-o',str(shot)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=6)
 (out/'desktop.png').write_bytes(shot.read_bytes());shot.unlink()
 time.sleep(3)
 r=subprocess.run(query,env=env,capture_output=True,text=True,timeout=2);assert r.returncode==0,r.stderr
 journal=subprocess.check_output(['journalctl','-b','--since',f'@{int(started)}','_UID='+str(uid),'--no-pager'],text=True)
 (out/'plasma-journal.log').write_text(journal)
 assert not re.search(r'VK_ERROR_DEVICE_LOST|query timeout|ZINK:.*failed|crashing',journal),journal[-4000:]
 log=subprocess.check_output(['dmesg','--color=never'],text=True)
 new=log[len(before):] if log.startswith(before) else log
 assert not re.search(r'M3 .*rejected|IOCTL: submit failed|M3 scheduler: execution failed|M3 completion events',new),new[-3000:]
 # Normal boots log only every 128th retirement. An idle desktop can finish
 # this probe between summaries; screenshot completion and the post-capture
 # KWin query above are the liveness checks, not printk frequency.
 draws=re.findall(r'M3_RETIRED draw=(\d+)',new)
 (out/'plasma-result.json').write_text(json.dumps({'user':user,'renderer':'Zink on Apple M3','draw_records':len(draws),'screenshot':'desktop.png'},indent=2)+'\n')
 print('Real KDE Plasma and screenshot completed on M3 Zink',flush=True)
finally:
 if app is not None and app.poll() is None:
  os.killpg(app.pid,signal.SIGTERM)
