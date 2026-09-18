#!/usr/bin/env python3
"""Move the software cursor under bounded GPU load; use gpu-experiment.py."""
import argparse,json,math,os,pathlib,socket,subprocess,time
p=argparse.ArgumentParser();p.add_argument('output',type=pathlib.Path);a=p.parse_args();a.output.mkdir(exist_ok=True,parents=True)
paths=list(pathlib.Path('/run/user/1000/hypr').glob('*/.socket.sock'));assert len(paths)==1
history={p.resolve() for p in pathlib.Path('/sys/kernel/debug/dri').glob('*/m4_present_history')};assert len(history)==1;history=history.pop()
def ipc(cmd):
 with socket.socket(socket.AF_UNIX) as s:
  s.settimeout(3);s.connect(str(paths[0]));s.sendall(cmd.encode());parts=[]
  while x:=s.recv(65536):parts.append(x)
 return b''.join(parts).decode()
def move(x,y):
 reply=ipc(f'/dispatch hl.dsp.cursor.move({{x={x},y={y}}})');assert reply.strip()=='ok',reply
original=json.loads(ipc('j/cursorpos'));rows={};moves=[]
env=os.environ.copy();env.update(VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json',ASAHI_M4_EXPERIMENTAL='1',ASAHI_MESA_DEBUG='nocompress')
ready=a.output/'ready';go=a.output/'go';log=open(a.output/'load.log','w')
child=subprocess.Popen(['/root/vk-priority-probe','/root/load.spv','1','10000','32','128',str(ready),str(go),str(a.output/'load.jsonl')],env=env,stdout=log,stderr=subprocess.STDOUT)
try:
 deadline=time.monotonic()+10
 while not ready.exists():
  if child.poll() is not None or time.monotonic()>deadline:raise RuntimeError('load startup failed')
  time.sleep(.01)
 go.touch();start=time.monotonic();last=0
 while time.monotonic()-start<6:
  t=time.monotonic();phase=t-start;move(round(700+120*math.sin(phase*5)),round(500+120*math.cos(phase*5)))
  moves.append({'start':t,'end':time.monotonic()})
  if t-last>.1:
   for line in history.read_text().splitlines()[1:]:
    values=list(map(int,line.split()));rows[values[0]]=dict(zip(['sequence','submitted_ns','completed_ns','framebuffer','status'],values))
   last=t
  time.sleep(max(0,1/120-(time.monotonic()-t)))
 end=time.monotonic();child.wait(timeout=18);assert child.returncode==0
 data=dict(start=start,end=end,rows=sorted(rows.values(),key=lambda r:r['sequence']))
 (a.output/'frames.json').write_text(json.dumps(data,indent=2)+'\n');(a.output/'motion.json').write_text(json.dumps(moves,indent=2)+'\n')
 active=[r for r in data['rows'] if start*1e9<=r['completed_ns']<=end*1e9]
 print(json.dumps(dict(start=start,end=end,moves=len(moves),frames=len(active),frames_per_second=len(active)/(end-start),max_ipc_ms=max(m['end']-m['start'] for m in moves)*1000)),flush=True)
 print('PASS: desktop motion under normal GPU load; load readbacks correct',flush=True)
finally:
 if child.poll() is None:child.kill()
 child.wait();log.close();move(original['x'],original['y'])
