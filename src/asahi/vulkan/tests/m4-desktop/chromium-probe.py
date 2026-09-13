#!/usr/bin/env python3
"""Bounded Chromium WebGL/CDP capture on Zink, requires gpu-experiment guard.

Software reference uses the same browser/Zink with lavapipe. Retains page and
scanout screenshots, CDP GPU diagnostics, process renderer maps and captures.
"""
import argparse, base64, hashlib, importlib.util, json, os, pwd, shutil, signal, stat, subprocess, time, urllib.request
from pathlib import Path
import websocket
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--guard-output',type=Path,required=True)
p.add_argument('--renderer',choices=['asahi','lavapipe'],default='asahi')
p.add_argument('--capture',action='store_true')
p.add_argument('--browser-flag',action='append',default=[])
p.add_argument('--normal-launch',action='store_true',help='Use /usr/bin/chromium and user launcher flags instead of explicit test GPU flags')
p.add_argument('--angle',choices=['gl','gles','vulkan'],default='gl')
p.add_argument('--ozone',choices=['wayland','x11'],default='wayland')
p.add_argument('--capture-profile',type=Path)
p.add_argument('--url',default='https://mdn.github.io/dom-examples/webgl-examples/tutorial/sample6/')
p.add_argument('--observe-seconds', type=int, default=5, choices=range(1,21))
p.add_argument('--start-script', type=Path, help='Retained page action before bounded observation')
p.add_argument('--page-script', type=Path, help='Retained page diagnostics; pixelsBase64 is saved as pixels.rgba')
a=p.parse_args()
spec=importlib.util.spec_from_file_location('fw',Path(__file__).with_name('firmware-snapshot.py'));fw=importlib.util.module_from_spec(spec);spec.loader.exec_module(fw)
boot=Path('/proc/sys/kernel/random/boot_id').read_text().strip()
def check():fw.check_guard(a.guard_output,boot)
check();out=a.guard_output/'chromium';out.mkdir()
u=pwd.getpwnam('e');profile=Path(u.pw_dir)/'.cache/m4-chromium-probes'/a.guard_output.name;profile.mkdir(parents=True)
for f in [profile.parent,profile]:os.chown(f,u.pw_uid,u.pw_gid)
env=dict(os.environ)
for k in list(env):
    if k.startswith(('VK_','AGX_','GFXRECON_')):env.pop(k)
for line in Path('/etc/environment.d/90-omarchy-j713-rendering.conf').read_text().splitlines():
    if line and not line.startswith('#') and '=' in line:k,v=line.split('=',1);env[k]=v
runtime=Path('/run/user/1000');sockets=[f for f in runtime.glob('wayland-*') if stat.S_ISSOCK(f.stat().st_mode)];assert len(sockets)==1
env.update(HOME=u.pw_dir,USER='e',LOGNAME='e',XDG_RUNTIME_DIR=str(runtime),WAYLAND_DISPLAY=sockets[0].name,DBUS_SESSION_BUS_ADDRESS=f'unix:path={runtime}/bus',VK_DRIVER_FILES='/opt/asahi-m4-port/asahi_icd.json' if a.renderer=='asahi' else '/usr/share/vulkan/icd.d/lvp_icd.json',LIBGL_ALWAYS_SOFTWARE='0' if a.renderer=='asahi' else '1',LP_NUM_THREADS='4',AGX_COMPILER_CAPTURE_DIR=str(profile/'compiler'),MESA_SHADER_CACHE_DIR=str(profile/'mesa-cache'))
(profile/'compiler').mkdir();os.chown(profile/'compiler',u.pw_uid,u.pw_gid)
if a.capture:env.update(VK_LAYER_PATH='/opt/gfxreconstruct/share/vulkan/explicit_layer.d',VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',GFXRECON_CAPTURE_FILE=str(profile/'chromium-${ProcessId}.gfxr'),GFXRECON_CAPTURE_FILE_TIMESTAMP='false',GFXRECON_CAPTURE_FILE_FLUSH='true')
if a.capture_profile:
    assert a.capture and a.renderer=='lavapipe'
    report=json.loads(a.capture_profile.with_suffix('.report.json').read_text())
    shutil.copyfile(a.capture_profile,out/'capture-profile.json')
    shutil.copyfile(a.capture_profile.with_suffix('.report.json'),out/'capture-profile.report.json')
    # The browser runs as e and cannot traverse /root to read the profile.
    # Retain the exact reference separately and use an accessible private copy.
    profile_copy=profile/'capture-profile.json'
    shutil.copyfile(a.capture_profile,profile_copy)
    os.chown(profile_copy,u.pw_uid,u.pw_gid)
    env.update(VK_LAYER_PATH=env['VK_LAYER_PATH']+':/usr/share/vulkan/explicit_layer.d',
        VK_INSTANCE_LAYERS=env['VK_INSTANCE_LAYERS']+':VK_LAYER_KHRONOS_profiles',
        VK_KHRONOS_PROFILES_PROFILE_FILE=str(profile_copy),
        VK_KHRONOS_PROFILES_SIMULATE_CAPABILITIES='SIMULATE_API_VERSION_BIT,SIMULATE_FEATURES_BIT,SIMULATE_EXTENSIONS_BIT,SIMULATE_PROPERTIES_BIT,SIMULATE_FORMATS_BIT',
        VK_KHRONOS_PROFILES_DEFAULT_FEATURE_VALUES='DEFAULT_FEATURE_VALUES_FALSE',
        VK_KHRONOS_PROFILES_EXCLUDE_DEVICE_EXTENSIONS=','.join(report['removed_reference_extensions']))
cmd=['/usr/lib/chromium/chromium','--user-data-dir='+str(profile),'--no-first-run','--no-default-browser-check','--ozone-platform='+a.ozone,'--use-gl=angle','--use-angle='+a.angle,'--ignore-gpu-blocklist','--enable-gpu-rasterization','--remote-debugging-port=0',a.url]
cmd += ['--enable-logging=stderr','--vmodule=gpu_init=2,gl_factory=2,gl_surface_egl=2'] + a.browser_flag
if a.ozone=='x11':env['DISPLAY']=':0'
if a.angle=='vulkan':cmd.append('--enable-features=Vulkan,VulkanFromANGLE,DefaultANGLEVulkan')
if a.normal_launch:
    assert a.angle=='gl' and a.ozone=='wayland', 'Normal launch uses configured GPU/platform flags'
    cmd=['/usr/bin/chromium','--user-data-dir='+str(profile),'--no-first-run',
         '--no-default-browser-check','--remote-debugging-port=0',a.url,*a.browser_flag]
    flags=Path(u.pw_dir)/'.config/chromium-flags.conf'
    if flags.exists():shutil.copyfile(flags,out/'chromium-flags.conf')
manifest=dict(renderer=a.renderer,angle=a.angle,normal_launch=a.normal_launch,observe_seconds=a.observe_seconds,command=cmd,environment={k:v for k,v in env.items() if k.startswith(('VK_','GFXRECON_','MESA_','GALLIUM_','LIBGL_','LD_LIBRARY_','ASAHI_','AGX_','GBM_','__EGL_'))},boot_id=boot)
manifest['hashes']={f:hashlib.sha256(Path(f).read_bytes()).hexdigest() for f in ['/usr/lib/chromium/chromium','/usr/lib/chromium/libEGL.so','/usr/lib/chromium/libGLESv2.so','/opt/asahi-m4-port/lib/libvulkan_asahi.so','/opt/asahi-m4-port/lib/libgallium-26.1.4.so']}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
child=None;sessions=[];seq=0
def call(ws,method,params=None):
    global seq
    check();seq+=1;ws.send(json.dumps(dict(id=seq,method=method,params=params or {})))
    while True:
        r=json.loads(ws.recv())
        if r.get('id')==seq:
            if 'error' in r:raise RuntimeError(r['error'])
            return r['result']
def interrupt(signum,frame):raise InterruptedError(f'signal {signum}')
signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt)
try:
    with (out/'browser.log').open('w') as log:
        child=subprocess.Popen(['runuser','-u','e','--',*cmd],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        deadline=time.monotonic()+15
        while not (profile/'DevToolsActivePort').exists():
            check()
            if child.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Chromium debugging endpoint unavailable')
            time.sleep(.2)
        port,browser_path=(profile/'DevToolsActivePort').read_text().splitlines()[:2]
        ws=websocket.create_connection('ws://127.0.0.1:'+port+browser_path,timeout=8,suppress_origin=True);sessions.append(ws)
        gpu=call(ws,'SystemInfo.getInfo');(out/'gpu.json').write_text(json.dumps(gpu,indent=2)+'\n')
        print('Chromium GPU:',json.dumps(gpu['gpu']['devices']),flush=True)
        tabs=json.load(urllib.request.urlopen('http://127.0.0.1:'+port+'/json/list',timeout=5))
        tab=next(t for t in tabs if t['type']=='page')
        page=websocket.create_connection(tab['webSocketDebuggerUrl'],timeout=8,suppress_origin=True);sessions.append(page)
        call(page,'Page.bringToFront')
        def page_script(path):
            source=path.read_text()
            response=call(page,'Runtime.evaluate',{'expression':'(function(){'+source+'\n})()', 'returnByValue':True, 'awaitPromise':True})
            (out/path.name).write_text(source)
            if 'exceptionDetails' in response:raise RuntimeError(response['exceptionDetails'])
            return response['result'].get('value')
        if a.start_script:
            (out/'page-start.json').write_text(json.dumps(page_script(a.start_script),indent=2)+'\n')
        until=time.monotonic()+a.observe_seconds
        while time.monotonic()<until:check();time.sleep(.2)
        state=call(page,'Runtime.evaluate',{'expression':'JSON.stringify({url:location.href,title:document.title,ready:document.readyState,visibility:document.visibilityState,probe:JSON.parse(document.documentElement.dataset.m4Probe||"null"),canvases:Array.from(document.querySelectorAll("canvas")).map(c=>({width:c.width,height:c.height}))})','returnByValue':True})
        (out/'page.json').write_text(json.dumps(state,indent=2)+'\n')
        webgl=call(page,'Runtime.evaluate',{'expression':"""new Promise(resolve => {
          const timeout=setTimeout(() => resolve({animationFrameTimeout:true,visibility:document.visibilityState}),3000);
          requestAnimationFrame(() => { clearTimeout(timeout);
          resolve(Array.from(document.querySelectorAll('canvas')).map(c => {
            const gl=c.getContext('webgl2') || c.getContext('webgl') || c.getContext('experimental-webgl');
            if (!gl) return {context:false};
            const ext=gl.getExtension('WEBGL_debug_renderer_info');
            const width=Math.min(64,gl.drawingBufferWidth),height=Math.min(64,gl.drawingBufferHeight);
            const pixels=new Uint8Array(width*height*4);
            gl.readPixels(Math.max(0,(gl.drawingBufferWidth-width)>>1),Math.max(0,(gl.drawingBufferHeight-height)>>1),width,height,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
            let nonblack=0,hash=2166136261;
            for(let i=0;i<pixels.length;i++){hash=Math.imul(hash^pixels[i],16777619)>>>0;if(i%4!==3 && pixels[i]) nonblack++;}
            return {context:true,lost:gl.isContextLost(),version:gl.getParameter(gl.VERSION),
              renderer:ext?gl.getParameter(ext.UNMASKED_RENDERER_WEBGL):gl.getParameter(gl.RENDERER),
              error:gl.getError(),width,height,nonblack,hash};
          }));
        }); })""",'returnByValue':True,'awaitPromise':True})
        (out/'webgl.json').write_text(json.dumps(webgl,indent=2)+'\n')
        if a.page_script:
            diagnostics=page_script(a.page_script)
            if isinstance(diagnostics,dict) and 'pixelsBase64' in diagnostics:
                pixels=base64.b64decode(diagnostics.pop('pixelsBase64'),validate=True)
                (out/'pixels.rgba').write_bytes(pixels)
                diagnostics.update(pixels_file='pixels.rgba',pixels_bytes=len(pixels))
            (out/'diagnostics.json').write_text(json.dumps(diagnostics,indent=2)+'\n')
            print('Page diagnostics:',json.dumps(diagnostics),flush=True)
        shot=call(page,'Page.captureScreenshot',{'format':'png'});(out/'page.png').write_bytes(base64.b64decode(shot['data']))
        nodes={f.resolve() for f in Path('/sys/kernel/debug/dri').glob('*/m4_scanout')};assert len(nodes)==1
        (out/'desktop.m4fb').write_bytes(nodes.pop().read_bytes())
        for proc in Path('/proc').glob('[0-9]*'):
            try:
                if proc.stat().st_uid==u.pw_uid and 'chromium' in (proc/'exe').resolve().name:
                    (out/f'{proc.name}-maps.txt').write_text((proc/'maps').read_text())
                    (out/f'{proc.name}-argv.json').write_text(json.dumps((proc/'cmdline').read_bytes().decode(errors='replace').split('\0')))
            except OSError:pass
finally:
    for ws in sessions:ws.close()
    if child and child.poll() is None:
        os.killpg(child.pid,signal.SIGTERM)
        try:child.wait(timeout=3)
        except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
    shutil.copytree(profile/'compiler',out/'compiler')
    for f in profile.glob('*.gfxr'):shutil.copyfile(f,out/f.name)
    print((out/'browser.log').read_text(errors='replace')[-4000:]);os.sync()
webgl_value=webgl.get('result',{}).get('value')
assert not (isinstance(webgl_value,dict) and webgl_value.get('animationFrameTimeout')), 'animation frame timeout; retained screenshot and diagnostics'
assert isinstance(webgl_value,list) and webgl_value, 'No WebGL canvas was tested'
assert all(v.get('context') and not v.get('lost') and not v.get('error') for v in webgl_value), 'WebGL context failed or was lost; retained diagnostics'
if a.page_script and isinstance(diagnostics,dict) and 'report' in diagnostics:
    report=diagnostics['report']
    assert report.get('ready') and not report.get('error') and not report.get('glError') and not diagnostics.get('readbackError'), 'Local shader/readback failed; retained diagnostics'
print('PASS: Chromium probe completed; inspect GPU diagnostics and screenshots')
