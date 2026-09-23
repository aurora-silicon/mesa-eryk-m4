#!/usr/bin/env python3
"""Bounded, unchanged-quality Acropolis Bay probe; run through M3 gpu/guard.py."""
import hashlib, json, os, re, signal, statistics, subprocess, sys, time, urllib.request
from pathlib import Path

out = Path(sys.argv[1])
backend = sys.argv[2] if len(sys.argv) > 2 else 'gl'
assert backend in ('gl', 'vulkan')
assert 'guard.py' in Path('/proc', str(os.getppid()), 'cmdline').read_text()
assert json.loads((out/'result.json').read_text())['phase'] == 'thermal-policy-ready'
assert not any(p.read_bytes().split(b'\0')[0].rsplit(b'/',1)[-1] == b'pyrogenesis'
               for p in Path('/proc').glob('[0-9]*/cmdline') if p.exists())
shell = subprocess.check_output(['pgrep','-u','eryk','-x','plasmashell'],text=True).split()[0]
env = dict(s.decode().split('=',1) for s in Path('/proc',shell,'environ').read_bytes().split(b'\0') if b'=' in s)
for key in ('NOTIFY_SOCKET','INVOCATION_ID','JOURNAL_STREAM','SYSTEMD_EXEC_PID'):
    env.pop(key,None)
env['HK_DEBUG_LOG'] = '0'
for option in sys.argv[3:]:
    if option.startswith('--environment='):
        assert '--launcher' not in sys.argv, 'Launcher overwrites Mesa selection'
        selected=json.loads(Path(option.split('=',1)[1]).read_text())
        assert all(isinstance(k,str) and isinstance(v,str) and k.startswith(
            ('HK_','AGX_','MESA_','VK_','ASAHI_','LD_LIBRARY_','LIBGL_',
             'GALLIUM_','GBM_','__EGL_')) for k,v in selected.items())
        env.update(selected)
if '--launcher' in sys.argv:
    settings=Path('/opt/asahi-m3-0ad/graphics.env')
    if settings.is_file():
        for line in settings.read_text().splitlines():
            if line and not line.startswith('#'):
                k,v=line.split('=',1);env[k]=v
if '--prefetch' in sys.argv:
    env.update(HK_M4_NATIVE_PREAMBLE='true', HK_M4_PREFETCH_UBOS='true',
               HK_M4_PREFETCH_VARIANT='true', HK_M4_PLAIN_SAMPLERS='true',
               HK_M4_IOTO_TEMPLATES='true')
if '--compression' in sys.argv:
    env.update(HK_M4_COMPRESSED_2D='true', HK_M4_COMPRESSED_FEEDBACK='true')
if '--capture' in sys.argv:
    capture=Path('/home/eryk/.cache/m3-0ad-captures')/out.name
    capture.mkdir(parents=True,exist_ok=False)
    os.chown(capture.parent,1000,1000);os.chown(capture,1000,1000)
    env.update(VK_LAYER_PATH='/opt/gfxreconstruct/share/vulkan/explicit_layer.d',
               VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',
               GFXRECON_CAPTURE_FILE=str(capture/'scene.gfxr'),
               GFXRECON_CAPTURE_FILE_TIMESTAMP='false',
               GFXRECON_CAPTURE_FILE_FLUSH='true',GFXRECON_CAPTURE_FRAMES='300-302')
for option in sys.argv[3:]:
    if option.startswith('--env='):
        key,value=option[6:].split('=',1)
        assert key.startswith(('HK_','AGX_','ZINK_','VK_','MESA_','SDL_'))
        if '--launcher' in sys.argv:
            assert key not in ('VK_DRIVER_FILES','AGX_MESA_DEBUG','HK_DEBUG_LOG',
                'HK_M4_NATIVE_PREAMBLE','HK_M4_PREFETCH_UBOS','HK_M4_PREFETCH_VARIANT',
                'HK_M4_PLAIN_SAMPLERS','HK_M4_IOTO_TEMPLATES'), 'Launcher overrides this option; use --prefetch --cpu-performance'
        env[key]=value
mesa_libraries=[]
for option in sys.argv[3:]:
    if option.startswith('--mesa-prefix='):
        assert '--launcher' not in sys.argv, 'Launcher overwrites Mesa selection'
        prefix=Path(option.split('=',1)[1])
        assert prefix.is_absolute() and prefix.is_dir()
        mesa_libraries=[prefix/'lib'/name for name in
            ('libgallium-26.1.4.so','libEGL_mesa.so.0','libgbm.so.1')]
        assert all(p.is_file() for p in mesa_libraries)
        env['LD_LIBRARY_PATH']=str(prefix/'lib')+':'+env['LD_LIBRARY_PATH']
        env['LIBGL_DRIVERS_PATH']=str(prefix/'lib/dri')
        env['GBM_BACKENDS_PATH']=str(prefix/'lib/gbm')
        env['__EGL_VENDOR_LIBRARY_FILENAMES']=str(prefix/'egl.json')
args = ['/usr/bin/pyrogenesis','-autostart=skirmishes/acropolis_bay_2p',
        '-autostart-player=1','-autostart-civ=1:cart','-autostart-civ=2:gaul',
        '-autostart-seed=200002407','-autostart-aiseed=2993941790',
        '-conf=view.scroll.mouse.detectdistance:0','-conf=view.scroll.speed:0',
        '-conf=profiler2.autoenable:'+('false' if '--present-cadence' in sys.argv else 'true'),
        '-conf=profiler2.gpu.enable:'+('false' if '--no-gpu-profiler' in sys.argv else 'true'),
        '-conf=overlay.fps:true',
        '-conf=rendererbackend:'+backend]
if '--present-cadence' in sys.argv:
    assert '--no-gpu-profiler' in sys.argv and '--ten-seconds' in sys.argv
    env['HK_PRESENT_TIMING']='true'
if '--uncapped' in sys.argv:
    args += ['-conf=adaptivefps.session:0','-conf=vsync:false']
if '--traditional-descriptors' in sys.argv:
    assert backend == 'vulkan'
    args += ['-conf=renderer.backend.vulkan.disabledescriptorindexing:true']
if '--cpu-skinning' in sys.argv:
    # An explicit algorithm comparison, with identical geometry/detail settings.
    # Record the choice in command/settings evidence; never change user config.
    args += ['-conf=gpuskinning:false']
if '--invalidate-framebuffers' in sys.argv:
    assert backend == 'gl'
    args += ['-conf=renderer.backend.gl.enableframebufferinvalidating:true']
if '--launcher' in sys.argv:
    assert '--cpu-performance' not in sys.argv
    args[0]='/usr/local/bin/0ad'
for option in sys.argv[3:]:
    if option.startswith('--binary='):
        assert '--launcher' not in sys.argv
        binary=Path(option.split('=',1)[1])
        assert binary.is_absolute() and binary.is_file() and binary.name == 'pyrogenesis'
        args[0]=str(binary)
icd=Path(env['VK_DRIVER_FILES'])
library=Path(json.loads(icd.read_text())['ICD']['library_path'])
assert library.is_absolute() and library.is_file()
(out/'binary-identity.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest()
    for p in (Path(__file__),Path(args[0]),Path('/usr/bin/pyrogenesis'),icd,library,*mesa_libraries)},indent=2))
(out/'settings.json').write_text(json.dumps({'command':args,'graphics_environment':{
    k:v for k,v in env.items() if k.startswith(('VK_','HK_','MESA_','AGX_','LD_','GALLIUM','ASAHI','GFXRECON_','ZINK_','SDL_','GBM_','LIBGL_','__EGL_'))}},indent=2))
child = None
klog = None
clocks = None
perf = None
governors = {}
try:
    if '--clocks' in sys.argv:
        from thermal_clock import Sampler
        # Parent guard remains the sole all-channel thermal reader.
        clocks = Sampler(out/'clocks.jsonl', temperatures=False)
    with (out/'kernel-live.log').open('w') as log:
        klog = subprocess.Popen(['dmesg','--follow-new','--color=never'],stdout=log,stderr=log)
    # The parent guard owns the qualified 0.5s all-channel thermal monitor.
    # Do not add an independent, unsynchronised reader of firmware records.
    policies=list(Path('/sys/devices/system/cpu/cpufreq').glob('policy*'))
    (out/'cpu-policies.json').write_text(json.dumps({p.name:{n:(p/n).read_text().strip()
        for n in ('scaling_governor','scaling_cur_freq','scaling_max_freq')}
        for p in policies},indent=2))
    if '--cpu-performance' in sys.argv:
        for p in policies:
            node=p/'scaling_governor'
            governors[node]=node.read_text().strip()
            node.write_text('performance')
    with (out/'game.log').open('w') as log:
        child = subprocess.Popen(['runuser','-u','eryk','--',*args],env=env,
                                 stdout=log,stderr=log,start_new_session=True)
        # Abort promptly on a known driver failure instead of waiting through
        # the measurement interval and then a compositor screenshot timeout.
        deadline=time.monotonic()+24
        while time.monotonic()<deadline:
            time.sleep(.5)
            if '--perf' in sys.argv and perf is None and deadline-time.monotonic()<8:
                # Diagnostic run only: seven seconds of system-wide user-space
                # samples include the game worker threads. Keep data private;
                # sampling overhead makes its FPS unsuitable as a baseline.
                perf=subprocess.Popen(['perf','record','-q','-e','cpu-clock:u',
                    '-F','99','--call-graph','dwarf,8192','-a',
                    '-o',str(out/'perf.data'),'--','sleep','7'],
                    stdout=log,stderr=log,start_new_session=True)
            kernel=(out/'kernel-live.log').read_text(errors='replace')
            game=(out/'game.log').read_text(errors='replace')
            assert not any(x in kernel for x in ('execution failed','GPU fault','firmware crashed','Oops:','BUG:')), 'Kernel GPU failure; retained kernel-live.log'
            assert 'DEVICE LOST' not in game, 'Application device loss; retained game.log'
            if child.poll() is not None:break
        (out/'cpu-policies-during.json').write_text(json.dumps({p.name:{n:(p/n).read_text().strip()
            for n in ('scaling_governor','scaling_cur_freq','scaling_max_freq')}
            for p in policies},indent=2))
        if child.poll() is not None:
            (out/'application-result.json').write_text(json.dumps({'exit':child.returncode}))
            raise RuntimeError('Game exited before the bounded scene check')
        else:
            mapped={}
            for proc in Path('/proc').glob('[0-9]*'):
                try:
                    if proc.joinpath('cmdline').read_bytes().split(b'\0')[0]!=b'/usr/bin/pyrogenesis':continue
                    maps=proc.joinpath('maps').read_text()
                    (out/'game-maps.txt').write_text(maps)
                    for line in maps.splitlines():
                        fields=line.split(maxsplit=5)
                        if len(fields)!=6:continue
                        path=Path(fields[5])
                        if str(path) not in mapped and path.is_file() and any(n in path.name for n in ('libgallium','libEGL_mesa','libvulkan_asahi','libgbm','libdril')):
                            mapped[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
                except FileNotFoundError:continue
            (out/'mapped-graphics-binaries.json').write_text(json.dumps(mapped,indent=2))
            assert str(library.resolve()) in mapped, 'Selected Vulkan library was not loaded'
            if backend=='gl':
                gallium=next((Path(directory)/'libgallium-26.1.4.so' for directory in
                              env['LD_LIBRARY_PATH'].split(':') if
                              (Path(directory)/'libgallium-26.1.4.so').is_file()),None)
                assert gallium and str(gallium.resolve()) in mapped, 'Selected Zink library was not loaded'
            queries=[] if '--present-cadence' in sys.argv else [('overview','overview'),('main','query?thread=main'),('gpu','query?thread=gpu')]
            for name,uri in queries:
                try:
                    with urllib.request.urlopen('http://127.0.0.1:8000/'+uri,timeout=3) as response:
                        (out/(name+'.json')).write_bytes(response.read())
                except Exception as error:
                    (out/(name+'-error.txt')).write_text(repr(error))
            if '--present-cadence' in sys.argv:
                # Read before the screenshot, whose compositor work disturbs
                # cadence. This is application present throughput, not the
                # number of physical panel refreshes or a GPU timestamp.
                records=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in
                         (out/'game.log').read_text().splitlines() if line.startswith('HK_PRESENT ')]
                assert len({r['queue'] for r in records})==1
                assert all(int(r['result'])==0 for r in records)
                starts=[int(r['end_ns'])/1e9 for r in records]
                starts=[t for t in starts if t>=starts[-1]-10]
                intervals=[b-a for a,b in zip(starts,starts[1:])]
                assert len(intervals)>=2 and min(intervals)>0 and sum(intervals)>9
                (out/'frame-summary.json').write_text(json.dumps({
                    'frames':len(intervals),'seconds':sum(intervals),
                    'fps':len(intervals)/sum(intervals),
                    'median_frame_ms':statistics.median(intervals)*1000,
                    'maximum_frame_ms':max(intervals)*1000,
                    'scope':'Application present cadence over the last ten seconds; profiler HTTP output and GPU timing disabled; screenshot afterwards; not physical scanout rate'
                },indent=2))
            from screenshot import capture
            capture(env, out/'screen.png', log, timeout=6)
            # Use frame-boundary intervals, not nested scopes. The game's
            # timer can put adjacent scope events a nanosecond out of order.
            if (out/'main.json').exists():
                events=json.loads((out/'main.json').read_text())['events']
                starts=[e[1] for block in events for e in block
                        if e and e[0] == 1 and e[2] == '__framestart']
                if '--ten-seconds' in sys.argv:
                    starts=[t for t in starts if t>=starts[-1]-10]
                else:
                    starts=starts[-121:]
                intervals=[b-a for a,b in zip(starts,starts[1:])]
                assert len(intervals)>=2 and min(intervals)>0
                (out/'frame-summary.json').write_text(json.dumps({
                    'frames':len(intervals),'seconds':sum(intervals),
                    'fps':len(intervals)/sum(intervals),
                    'median_frame_ms':statistics.median(intervals)*1000,
                    'maximum_frame_ms':max(intervals)*1000,
                    'scope':('Complete frame intervals in the last ten seconds' if '--ten-seconds' in sys.argv
                             else 'Last 120 main-thread frame intervals')+'; screenshot captured afterwards'
                },indent=2))
            info=Path('/home/eryk/.local/state/0ad/log/system_info.txt')
            (out/'system-info.txt').write_bytes(info.read_bytes())
finally:
    if perf is not None:
        if perf.poll() is None:
            os.killpg(perf.pid,signal.SIGINT)
        try: perf.wait(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(perf.pid,signal.SIGKILL);perf.wait(timeout=2)
        (out/'perf-result.json').write_text(json.dumps({'returncode':perf.returncode,
            'scope':'Diagnostic CPU sampling; do not use this run as an FPS baseline'}))
    if child is not None and child.poll() is None:
        os.killpg(child.pid,signal.SIGTERM)
        try: child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid,signal.SIGKILL); child.wait(timeout=2)
    for node,value in governors.items(): node.write_text(value)
    if klog is not None:
        klog.terminate(); klog.wait(timeout=2)
    if clocks is not None:
        clocks.close()
    if '--launcher' in sys.argv:
        time.sleep(6)  # GameMode's reaper runs every five seconds.
    (out/'cpu-policies-after.json').write_text(json.dumps({p.name:{n:(p/n).read_text().strip()
        for n in ('scaling_governor','scaling_cur_freq','scaling_max_freq')}
        for p in policies},indent=2))
