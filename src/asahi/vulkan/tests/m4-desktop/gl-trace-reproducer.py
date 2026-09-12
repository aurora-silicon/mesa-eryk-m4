#!/usr/bin/env python3
"""Export this captured single-thread Hyprland GL stream as a GLES/GL diagnostic.

External XR24/AR24 DMA-BUF render targets become retained local RGBA8 storage.
Texture imports require apitrace's captured fake TexImage2D content. All captured
GL mutations are retained. EGL presentation, fences and context transitions are
not replayed: this tests ordered GL rendering in one sharing context, not WSI.
Hardware execution MUST use gpu-experiment.py and run-m4-gl.py.
Input is a trusted local apitrace capture; pickle is not an untrusted format.
"""
import argparse, hashlib, json, pickle, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('trace',type=Path);p.add_argument('--last-call',type=int,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--apitrace',type=Path,default=root/'build/apitrace-host/usr/bin/apitrace')
a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(a.apitrace.resolve().parents[1]/'lib/apitrace/scripts'))
proc=subprocess.Popen([str(a.apitrace),'pickle',f'--calls=0-{a.last_call}',str(a.trace)],stdout=subprocess.PIPE)
out=[];blobs={};images={};imports={};rb=0;program=0;threads=set();draws=[];adaptations=[];texture_import_pending=False
names={'shader':'shaders','program':'programs','texture':'textures','buffer':'buffers','array':'vaos','framebuffer':'framebuffers','renderbuffer':'renderbuffers'}
gen={'Buffers':'buffers','Textures':'textures','VertexArrays':'vaos','Framebuffers':'framebuffers','Renderbuffers':'renderbuffers'}
def ident(kind,value):
 assert isinstance(value,int) and 0<=value<4096, (no,fn,kind,value)
 return f'{kind}[{value}]'
def atom(value):
 if isinstance(value,str):return json.dumps(value)
 if isinstance(value,float):return value.hex()+'f'
 if isinstance(value,int):return str(value)
 if isinstance(value,bytes):
  h=hashlib.sha256(value).hexdigest();name=h+'.bin'
  if h not in blobs:(a.output/name).write_bytes(value);blobs[h]=len(value)
  return f'blob("{name}", {len(value)})'
 raise ValueError(type(value))
while True:
 try:no,thread,fn,pairs,ret,flags=pickle.load(proc.stdout)
 except EOFError:break
 args=dict(pairs)
 if fn=='eglCreateImageKHR':
  v=args['attrib_list'];d=dict(zip(v[:-1:2],v[1::2]));images[int(ret)]=d;continue
 if not fn.startswith('gl'):continue
 threads.add(thread);assert len(threads)==1,'multiple GL threads unsupported'
 if texture_import_pending:
  assert fn=='glTexImage2D' and flags&1,'texture import lacks captured content'
  texture_import_pending=False
 code=[]
 if fn=='glEGLImageTargetTexture2DOES':
  texture_import_pending=True;adaptations.append({'call':no,'kind':'texture import uses following captured TexImage2D'});continue
 if fn=='glEGLImageTargetRenderbufferStorageOES':
  d=images[args['image']];assert d[12913] in (875713112,875713089)
  width,height,fd=d[12375],d[12374],d[12914];assert 0<width<=8192 and 0<height<=8192
  key=fd
  if key in imports:
   code.append(f'{ident("renderbuffers",rb)}=retained_rb[{imports[key]}];glBindRenderbuffer(GL_RENDERBUFFER,{ident("renderbuffers",rb)});')
  else:
   imports[key]=len(imports);code.append(f'glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,{width},{height});retained_rb[{imports[key]}]={ident("renderbuffers",rb)};')
  adaptations.append({'call':no,'kind':'local RGBA8 import','fd':fd,'width':width,'height':height})
 elif fn in ('glCreateProgram','glCreateShader'):
  kind='programs' if fn=='glCreateProgram' else 'shaders'
  code.append(f'{ident(kind,ret)}={fn}('+','.join(atom(v) for _,v in pairs)+');')
 elif fn.startswith('glGen') and fn[5:] in gen:
  kind=gen[fn[5:]];ids=pairs[1][1];code.append(f'GLuint tmp[{len(ids)}];{fn}({len(ids)},tmp);')
  code.extend(f'{ident(kind,v)}=tmp[{i}];' for i,v in enumerate(ids))
 elif fn.startswith('glDelete') and fn[8:] in gen:
  kind=gen[fn[8:]];ids=pairs[1][1]
  if kind=='renderbuffers':
   adaptations.append({'call':no,'kind':'retain import backing until exit'})
  else:code.append('GLuint tmp[]={'+','.join(ident(kind,v) for v in ids)+'};'+f'{fn}({len(ids)},tmp);')
 elif fn in ('glGetUniformLocation','glGetAttribLocation'):
  if ret>=0:
   assert ret<512
   kind='uniforms' if fn=='glGetUniformLocation' else 'attributes'
   code.append(f'{kind}[{args["program"]}][{ret}]={fn}({ident("programs",args["program"])},{atom(args["name"])});')
 elif fn in ('glGetProgramiv','glGetShaderiv','glCheckFramebufferStatus','glGetGraphicsResetStatus'):
  if fn in ('glGetProgramiv','glGetShaderiv') and args['pname'] in (0x8b81,0x8b82):
   kind='program' if fn=='glGetProgramiv' else 'shader'
   code.append(f'GLint ok;{fn}({ident(names[kind],args[kind])},{args["pname"]},&ok);CHECK(ok);')
  # Queries do not mutate GL state. FBO validity is checked by each draw.
 elif fn.startswith('glGet'):
  continue
 elif fn=='glShaderSource':
  assert args['length']==0
  source=''.join(args['string']).encode()+b'\0';code.append(f'const char *src={atom(source)};glShaderSource({ident("shaders",args["shader"])},1,&src,NULL);')
 else:
  vals=[]
  for key,value in pairs:
   if key in names and fn not in ('glVertexAttribPointer','glClearBufferfv','glActiveTexture'):v=ident(names[key],value)
   elif key=='location' and fn.startswith('glUniform'):
    v='-1' if value==-1 else f'uniforms[{program}][{value}]'
   elif isinstance(value,list):
    typ='GLfloat' if fn in ('glUniform4fv','glUniformMatrix3fv','glClearBufferfv','glActiveTexture') else 'GLenum'
    v=f'(const {typ}[]){{'+','.join(atom(x) for x in value)+'}'
   elif key in ('pointer','pixels','data') and isinstance(value,int):v=f'(void *)(uintptr_t){value}'
   else:v=atom(value)
   vals.append(v)
  code.append(fn+'('+','.join(vals)+');')
  if fn=='glUseProgram':program=args['program']
  if fn=='glBindRenderbuffer':rb=args['renderbuffer']
  if fn=='glDrawArrays':
   draws.append(no)
   code=[f'if (!skip_draw({no})) for (unsigned repeat=0; repeat < (repeat_draw=={no}?2u:1u); repeat++) {{fprintf(stderr,"draw call {no} repeat=%u\\n",repeat);'+''.join(code)+'}']
 if code:out.append(f'/* trace call {no}: {fn} */\nif (finish_before=={no}) {{fprintf(stderr,"diagnostic finish before {no}\\n");glFinish();}}\n{{'+''.join(code)+f'CHECK_GL({no});'+'}')
assert proc.wait()==0 and not texture_import_pending
header=r'''#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define CHECK_GL(n) do {GLenum e=glGetError();if(e){fprintf(stderr,"FAIL call %u GL=%x\n",(unsigned)n,e);exit(1);}}while(0)
static GLuint shaders[4096],programs[4096],textures[4096],buffers[4096],vaos[4096],framebuffers[4096],renderbuffers[4096],retained_rb[4096];
static GLint uniforms[4096][512],attributes[4096][512];
static void *pending[16];static unsigned n_pending;
static void *blob(const char *name,size_t size){FILE*f=fopen(name,"rb");CHECK(f);void*p=malloc(size?size:1);CHECK(p);CHECK(fread(p,1,size,f)==size);CHECK(!fclose(f));CHECK(n_pending<16);pending[n_pending++]=p;return p;}
static void free_blobs(void){while(n_pending)free(pending[--n_pending]);}
static int skip_draw(unsigned call){const char*s=getenv("M4_GL_REPLAY_SKIP_DRAWS");while(s&&*s){char*end;unsigned n=strtoul(s,&end,10);CHECK(end!=s&&(*end==','||!*end));if(n==call){fprintf(stderr,"diagnostic skip draw %u\n",call);return 1;}s=*end?end+1:end;}return 0;}
int main(void){
const unsigned finish_before=getenv("M4_GL_REPLAY_FINISH_BEFORE") ? strtoul(getenv("M4_GL_REPLAY_FINISH_BEFORE"),0,10) : 0;
const unsigned repeat_draw=getenv("M4_GL_REPLAY_REPEAT_DRAW") ? strtoul(getenv("M4_GL_REPLAY_REPEAT_DRAW"),0,10) : 0;
EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);CHECK(eglInitialize(d,0,0));CHECK(eglBindAPI(EGL_OPENGL_API));
EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,ca);CHECK(c!=EGL_NO_CONTEXT);CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
const char*r=(const char*)glGetString(GL_RENDERER);fprintf(stderr,"Renderer: %s\n",r);
CHECK((strstr(r,"zink")&&strstr(r,"M4"))||(getenv("M4_TEST_LLVMPIPE")&&strstr(r,"llvmpipe")));
'''
footer='''
glFinish();CHECK_GL(0);CHECK(glGetGraphicsResetStatus()==GL_NO_ERROR);GLint fb;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fb);glBindFramebuffer(GL_READ_FRAMEBUFFER,fb);
unsigned char *pixels=malloc(2560*1664*4);CHECK(pixels);glReadPixels(0,0,2560,1664,GL_RGBA,GL_UNSIGNED_BYTE,pixels);CHECK_GL(0);glFinish();CHECK(glGetGraphicsResetStatus()==GL_NO_ERROR);
FILE*f=fopen("output.rgba","wb");CHECK(f);CHECK(fwrite(pixels,1,2560*1664*4,f)==2560*1664*4);CHECK(!fclose(f));puts("PASS: captured GL draws finished");return 0;}
'''
(a.output/'replay.c').write_text(header+'\nfree_blobs();\n'.join(out)+footer)
(a.output/'manifest.json').write_text(json.dumps({'exporter_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'trace':str(a.trace),'trace_sha256':hashlib.sha256(a.trace.read_bytes()).hexdigest(),'last_call':a.last_call,'draw_calls':draws,'adaptations':adaptations,'blobs':blobs,'scope':__doc__},indent=2)+'\n')
print(f'Exported {len(draws)} draws, {len(blobs)} blobs to {a.output}')
