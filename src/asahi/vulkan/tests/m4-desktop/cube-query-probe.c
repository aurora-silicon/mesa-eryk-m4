/* textureQueryLod on a cube map through Zink: prints the (level, lod) pair
 * for a few screen scales against the CTS reference formula. Build with
 * `cc -O2 cube-query-probe.c -lEGL -lGL -lm`; run only through
 * gpu-experiment.py and run-m4-gl.py (M4_TEST_LLVMPIPE=1 for the control). */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError()); exit(1); } } while(0)
static GLuint compile(GLenum type, const char *s) {
 GLuint sh=glCreateShader(type);glShaderSource(sh,1,&s,NULL);glCompileShader(sh);GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[8192];glGetShaderInfoLog(sh,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);return sh;
}
int main(int argc, char **argv) {
 const int mode = argc > 1 ? atoi(argv[1]) : 0; /* 0 cube, 1 2D control */
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);
 CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext ctx=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);CHECK(ctx!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
 printf("Renderer: %s\n",glGetString(GL_RENDERER));fflush(stdout);
 const int size=mode==1?64:128; GLuint tex; glGenTextures(1,&tex);
 GLenum target = mode == 1 ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
 glBindTexture(target,tex);
 for (int level = 0; level < 7; ++level) {
   int ls = size >> level; if (ls < 1) ls = 1;
   if (mode == 1) glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,ls,ls,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
   else for (int f = 0; f < 6; ++f) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,level,GL_RGBA8,ls,ls,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
 }
 glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,6);
 glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 const char *reduction=getenv("M4_QUERY_REDUCTION");
 float bias=getenv("M4_QUERY_BIAS")?atof(getenv("M4_QUERY_BIAS")):0;
 float minimum=getenv("M4_QUERY_MIN")?atof(getenv("M4_QUERY_MIN")):0;
 float maximum=getenv("M4_QUERY_MAX")?atof(getenv("M4_QUERY_MAX")):6;
 int nearest=getenv("M4_QUERY_NEAREST")!=NULL;
 if(reduction)glTexParameteri(target,GL_TEXTURE_REDUCTION_MODE_ARB,atoi(reduction)==1?GL_MIN:GL_MAX);
 glTexParameterf(target,GL_TEXTURE_LOD_BIAS,bias);
 glTexParameterf(target,GL_TEXTURE_MIN_LOD,minimum);
 glTexParameterf(target,GL_TEXTURE_MAX_LOD,maximum);
 if(nearest)glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);
 CHECK(glGetError()==GL_NO_ERROR);
 /* Output: 128x1 RGBA32F, pixel x uses scale s = 2^((x/16) - 2) texels per pixel */
 GLuint out; glGenTextures(1,&out); glBindTexture(GL_TEXTURE_2D,out); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,128,4,0,GL_RGBA,GL_FLOAT,NULL);
 GLuint fb; glGenFramebuffers(1,&fb); glBindFramebuffer(GL_FRAMEBUFFER,fb); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,out,0);
 CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 GLuint p=glCreateProgram();
 glAttachShader(p,compile(GL_VERTEX_SHADER,"#version 330 core\nvoid main(){vec2 v=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(v*2.-1.,0,1);}"));
 glAttachShader(p,compile(GL_FRAGMENT_SHADER, mode == 1 ?
  "#version 330 core\n#extension GL_ARB_texture_query_lod : require\nuniform sampler2D tex;out vec4 color;void main(){float s=exp2(floor(gl_FragCoord.x/16.)-2.);vec2 uv=gl_FragCoord.xy*s/64.;vec2 q=textureQueryLOD(tex,uv);color=vec4(q,s,0);}" :
  mode == 2 ?
  "#version 330 core\n#extension GL_ARB_texture_query_lod : require\nuniform samplerCube tex;out vec4 color;void main(){float s=exp2(floor(gl_FragCoord.x/16.)-2.);vec3 dir=vec3(0.3,0.2,1.0);vec2 q=textureQueryLOD(tex,dir);color=vec4(q,s,0);}" :
  mode == 3 ?
  "#version 330 core\n#extension GL_ARB_texture_query_lod : require\nuniform samplerCube tex;out vec4 color;void main(){float s=exp2(floor(gl_FragCoord.x/16.)-2.);vec2 uv=mod(floor(gl_FragCoord.xy),2.)*s/64.;vec3 dir=vec3(uv.x,uv.y,1.0);vec2 q=textureQueryLOD(tex,dir);vec4 c=texture(tex,dir);color=vec4(q,s,c.x);}" :
  "#version 330 core\n#extension GL_ARB_texture_query_lod : require\nuniform samplerCube tex;out vec4 color;void main(){float s=exp2(floor(gl_FragCoord.x/16.)-2.);vec2 uv=mod(floor(gl_FragCoord.xy),2.)*s/64.;vec3 dir=vec3(uv.x,uv.y,1.0);vec2 q=textureQueryLOD(tex,dir);color=vec4(q,s,0);}"));
 glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(p); GLuint vao; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
 glBindTexture(target,tex);
 glViewport(0,0,128,4);glDrawArrays(GL_TRIANGLES,0,3);glFinish();
 CHECK(glGetError()==GL_NO_ERROR); float px[128*4*4];glReadPixels(0,0,128,4,GL_RGBA,GL_FLOAT,px);CHECK(glGetError()==GL_NO_ERROR);
 int fail=0;
 for(int x=8;x<128;x+=16){ float *v=&px[4*(2*128+x)]; float s=v[2];
   /* reference: Cube size 128, direction delta s/64, face projection divides by 2.
    * All four lanes stay strictly on +Z, including the largest gradient. */
   float ref = log2f(s)+bias;
   float level=fminf(fmaxf(ref,minimum),maximum);
   level=fminf(fmaxf(level,0),6);
   if(nearest)level=floorf(level+.5f);
   printf("x=%d scale=%g -> level=%g lod=%g (ref lod %g)\n",x,s,v[0],v[1],ref);
   if (!isfinite(v[0]) || !isfinite(v[1]) || fabsf(v[1]-ref) > 0.1f || fabsf(v[0]-level) > 0.1f) fail=1; }
 printf("M4 probe %s mode %d\n", fail?"FAIL":"PASS", mode); return fail;
}
