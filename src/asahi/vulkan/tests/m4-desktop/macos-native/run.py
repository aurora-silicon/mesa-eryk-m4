#!/usr/bin/env python3
"""Run a bounded native M4 probe/capture using the staged, exact-build tools."""
import argparse, datetime, json, pathlib, shlex, subprocess, tarfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('kind', choices=['fill', 'fragment', 'multisample', 'image-array', 'layer-depth'])
p.add_argument('--capture', action='store_true')
p.add_argument('--color', action='store_true', help='Layer-depth probe: render RGBA32Float instead of depth')
p.add_argument('--firmware-trace', action='store_true', help='Enable bounded native ktrace firmware subclasses 0x8590..0x8599')
p.add_argument('--active', action='store_true')
p.add_argument('--render-active', action='store_true', help='Capture finite repeated render passes without a compute dwell')
p.add_argument('--repeat', type=int, default=1)
p.add_argument('--dispatches', type=int, choices=[1, 2], default=1,
               help='Fill: separate compute encoders in one command buffer, writing disjoint halves')
p.add_argument('--width', type=int, choices=[8, 32, 128, 1024])
p.add_argument('--iterations', type=int, default=20)
p.add_argument('--layers', type=int, choices=[1,4], default=4)
p.add_argument('--samples', type=int, choices=[1,2,4], default=2)
p.add_argument('--mask', type=lambda x:int(x,0), default=1)
p.add_argument('--resolve-compute', action='store_true')
p.add_argument('--workspace-root', type=pathlib.Path)
p.add_argument('--host', default='root@mac-m4')
a = p.parse_args()
if a.dispatches != 1 and a.kind != 'fill': p.error('--dispatches requires fill')
if a.color and a.kind != 'layer-depth': p.error('--color requires layer-depth')
if a.width is None: a.width = 8 if a.kind == "image-array" else 1024
if (a.kind == "image-array") != (a.width in (8, 32)):
    p.error("image-array widths are 8/32; other probes use 128/1024")
if (not 0 <= a.iterations <= 4096 or not 1 <= a.repeat <= (2048 if a.kind == "multisample" else 256)
    or (a.repeat != 1 and a.kind not in ("fragment", "multisample", "layer-depth"))
    or (a.active and (not a.capture or a.kind in ('image-array','layer-depth')))
    or (a.render_active and (not a.capture or a.active or a.kind not in ("fragment", "multisample", "layer-depth")))):
    p.error('invalid iterations or active capture')
if a.kind == 'multisample' and (a.active or not 0 <= a.mask < (1 << a.samples)):
    p.error('multisample uses finite render capture, with an in-range mask')
if a.resolve_compute and a.kind != 'multisample': p.error('--resolve-compute needs multisample')
root = (a.workspace_root or pathlib.Path(__file__).resolve().parents[2]).resolve()
remote = '/private/var/tmp/m4-native-traces-20260910'
name = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + a.kind
if a.kind == 'fill': name += f'-dispatches{a.dispatches}'
if a.kind == 'image-array': name += f'-{a.width}'
if a.kind == 'layer-depth': name += f'-{a.width}-layers{a.layers}' + ('-color' if a.color else '')
if a.kind == 'fragment': name += f'-{a.width}-loop{a.iterations}'
if a.kind == 'multisample': name += f'-{a.width}-samples{a.samples}-mask{a.mask:x}'
name += f'-repeat{a.repeat}' if a.repeat != 1 else ''
name += '-render-active' if a.render_active else '-active' if a.active else '-capture' if a.capture else '-baseline'
if a.firmware_trace: name += '-fwtrace'
if a.resolve_compute: name += '-compute-resolve'
out = root / '_provenance/asahi-0ad-20260910-native-traces' / name
out.mkdir(parents=True, exist_ok=False)
probe = [remote + '/' + a.kind + '-probe', '--source', remote + '/' +
         ({'fill':'fill.metal','fragment':'fragment-loop.metal','multisample':'multisample.metal','image-array':'image-array.metal','layer-depth':'layer-depth.metal'}[a.kind]),
         '--output', remote + '/' + name + '-pixels']
if a.kind == 'fill': probe += ['--dispatches', str(a.dispatches)]
if a.kind == 'layer-depth': probe += ['--width',str(a.width),'--layers',str(a.layers),'--repeat',str(a.repeat)]
if a.color: probe += ['--color']
if a.kind == 'image-array': probe += ['--width', str(a.width)]
if a.kind == 'fragment': probe += ['--width', str(a.width), '--iterations', str(a.iterations), '--repeat', str(a.repeat)]
if a.kind == 'multisample': probe += ['--width',str(a.width),'--samples',str(a.samples),'--mask',str(a.mask),'--repeat',str(a.repeat)]
if a.resolve_compute: probe += ['--resolve-compute']
command = probe
if a.capture:
    command = ['python3', remote + '/g16state.py', '--probe', probe[0],
               '--output', remote + '/' + name, '--expected-output-bytes',
               str(4194304 if a.kind in ('fill','image-array') else a.width * a.width * 4 * (a.layers * (4 if a.color else 1) if a.kind == 'layer-depth' else 1)),
               '--capture-physical-gpu-state', '--probe-args'] + probe[1:] + [
               '--workload', 'compute-direct' if a.kind in ('fill','image-array') else 'render-draw', '--gate-submit']
    if a.active: command += ['--gate-shader']
    if a.render_active: command += ['--capture-render']
(out / 'command.json').write_text(json.dumps({'host': a.host, 'command': command}, indent=2) + '\n')
script = '''import json,pathlib,subprocess,sys,signal,time
request=json.load(sys.stdin)
assert subprocess.check_output(['sw_vers','-buildVersion'],text=True).strip()=='25G83'
assert subprocess.check_output(['sysctl','-n','hw.model'],text=True).strip()=='Mac16,12'
assert subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip()=='Apple M4'
root=pathlib.Path(request['root']); name=request['name']
boot=subprocess.check_output(['sysctl','-n','kern.bootsessionuuid'],text=True).strip()
trace=None
trace_log=None
try:
    if request['trace']:
        td=root/(name+'-trace'); td.mkdir()
        before=subprocess.check_output(['ktrace','info'],text=True)
        (td/'before.txt').write_text(before)
        if ('Kernel tracing is disabled' not in before or
            not ('Not owned by any process' in before or 'Tracing is in the background:' in before)):
            raise RuntimeError('Refusing to replace existing trace session: '+before)
        trace_log=(td/'ktrace.log').open('w')
        trace=subprocess.Popen(['ktrace','dump','-T','180','-b','64','-f',
            ','.join('S0x%04x'%i for i in range(0x8590,0x859a)),
            str(td/'firmware.ktrace')],stdout=trace_log,stderr=subprocess.STDOUT)
        for attempt in range(50):
            if trace.poll() is not None: raise RuntimeError('ktrace exited before probe')
            info=subprocess.check_output(['ktrace','info'],text=True)
            if 'Kernel tracing is enabled' in info: break
            time.sleep(.1)
        else: raise RuntimeError('ktrace did not enable')
        (td/'enabled.txt').write_text(info)
    with (root/(name+'.log')).open('w') as log:
        log.write('boot_id='+boot+'\\n');log.flush()
        r=subprocess.run(request['command'],stdout=log,stderr=subprocess.STDOUT,timeout=170)
finally:
    if trace is not None:
        if trace.poll() is None: trace.send_signal(signal.SIGINT)
        try: trace.wait(timeout=15)
        except subprocess.TimeoutExpired:
            trace.kill();trace.wait()
        (td/'after.txt').write_text(subprocess.check_output(['ktrace','info'],text=True))
    if trace_log is not None: trace_log.close()
print((root/(name+'.log')).read_text())
print('returncode='+str(r.returncode))
'''
print('Evidence:', out, flush=True)
r = subprocess.run(['ssh', '-o', 'ConnectTimeout=10', a.host,
                    shlex.join(['python3', '-c', script])],
                   input=json.dumps({'root': remote, 'name': name, 'command': command, 'trace': a.firmware_trace}),
                   text=True, capture_output=True, timeout=210)
(out / 'driver.log').write_text(r.stdout + r.stderr)
print(r.stdout + r.stderr)
suffixes = (['', '-pixels'] if a.capture else ['-pixels']) + (['-trace'] if a.firmware_trace else [])
with (out / 'capture.tar.gz').open('wb') as archive:
    fetch = subprocess.run(['ssh', '-o', 'ConnectTimeout=10', a.host,
                            shlex.join(['tar', '-C', remote, '-czf', '-'] + [name+s for s in suffixes])],
                           stdout=archive, stderr=subprocess.PIPE, timeout=120)
if fetch.returncode:
    print('Fetch failed; partial archive retained:', fetch.stderr.decode())
else:
    with tarfile.open(out / 'capture.tar.gz') as archive:
        archive.extractall(out, filter='data')
fetch.check_returncode()
r.check_returncode()
if 'returncode=0' not in r.stdout: raise SystemExit(1)
