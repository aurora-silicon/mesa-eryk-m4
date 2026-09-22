/* SPDX-License-Identifier: MIT
 * Check sparse MSAA values and residency across samples, array layers and
 * rebinds. Run only under gpu-experiment.py. Build: cc FILE -O2 -lEGL -lGL.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s GL=%x\n",__LINE__,#x,glGetError());exit(1);}}while(0)
static GLuint program(const char *source) {
 GLuint s=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(s,1,&source,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}CHECK(ok);
 GLuint p=glCreateProgram();glAttachShader(p,s);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glDeleteShader(s);return p;
}
int main(void) {
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,NULL,NULL);CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,6,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,a);CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 PFNGLTEXPAGECOMMITMENTARBPROC commit=(void*)eglGetProcAddress("glTexPageCommitmentARB");CHECK(commit);
 printf("Renderer: %s\n",glGetString(GL_RENDERER));
 GLuint write=program("#version 460\nlayout(local_size_x=1)in;layout(binding=0,rgba32ui)writeonly uniform uimage2DMSArray img;uniform int ns;void main(){ivec3 p=ivec3(gl_GlobalInvocationID);for(int s=0;s<ns;s++)imageStore(img,p,s,uvec4(p.x+1,p.y+2,p.z+3,s+4));}");
 GLuint read=program("#version 460\n#extension GL_ARB_sparse_texture2:require\nlayout(local_size_x=1)in;layout(binding=0)uniform usampler2DMSArray tex;layout(binding=0,rgba32ui)readonly uniform uimage2DMSArray img;layout(std430,binding=0)buffer Result{uvec4 v[];};uniform int ns;void main(){ivec3 p=ivec3(gl_GlobalInvocationID);ivec3 sz=textureSize(tex);for(int s=0;s<ns;s++){uvec4 a,b;int ca=sparseTexelFetchARB(tex,p,s,a);int cb=sparseImageLoadARB(img,p,s,b);uint i=uint(((p.z*sz.y+p.y)*sz.x+p.x)*ns+s)*3;v[i]=a;v[i+1]=b;v[i+2]=uvec4(sparseTexelsResidentARB(ca),sparseTexelsResidentARB(cb),0,0);}}");
 for(int ns=2;ns<=4;ns*=2) {
  const GLenum target=GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
  GLint px=0,py=0,pz=0,count=0;
  glGetInternalformativ(target,GL_RGBA32UI,GL_NUM_VIRTUAL_PAGE_SIZES_ARB,1,&count);CHECK(count>0);
  glGetInternalformativ(target,GL_RGBA32UI,GL_VIRTUAL_PAGE_SIZE_X_ARB,1,&px);glGetInternalformativ(target,GL_RGBA32UI,GL_VIRTUAL_PAGE_SIZE_Y_ARB,1,&py);glGetInternalformativ(target,GL_RGBA32UI,GL_VIRTUAL_PAGE_SIZE_Z_ARB,1,&pz);CHECK(px>0&&py>0&&pz==1);
  int w=px*2,h=py*2,layers=2;size_t words=(size_t)w*h*layers*ns*12;
  GLuint tex,buf;glGenTextures(1,&tex);glBindTexture(target,tex);glTexParameteri(target,GL_TEXTURE_SPARSE_ARB,GL_TRUE);glTexStorage3DMultisample(target,ns,GL_RGBA32UI,w,h,layers,GL_TRUE);CHECK(glGetError()==GL_NO_ERROR);
  glGenBuffers(1,&buf);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);glBufferData(GL_SHADER_STORAGE_BUFFER,words*4,NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buf);
  for(int phase=0;phase<2;phase++) {
   for(int z=0;z<layers;z++)for(int y=0;y<2;y++)for(int x=0;x<2;x++)commit(target,0,x*px,y*py,z,px,py,1,((x+y+z+phase)&1)==0);
   CHECK(glGetError()==GL_NO_ERROR);
   glBindImageTexture(0,tex,0,GL_TRUE,0,GL_READ_WRITE,GL_RGBA32UI);glUseProgram(write);glUniform1i(glGetUniformLocation(write,"ns"),ns);glDispatchCompute(w,h,layers);glMemoryBarrier(GL_ALL_BARRIER_BITS);
   glUseProgram(read);glUniform1i(glGetUniformLocation(read,"ns"),ns);glDispatchCompute(w,h,layers);glMemoryBarrier(GL_ALL_BARRIER_BITS);
   unsigned *out=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,words*4,GL_MAP_READ_BIT);CHECK(out);
   for(int z=0;z<layers;z++)for(int y=0;y<h;y++)for(int x=0;x<w;x++)for(int s=0;s<ns;s++) {
    unsigned *p=out+(((z*h+y)*w+x)*ns+s)*12;unsigned resident=((x/px+y/py+z+phase)&1)==0;
    unsigned expected[]={resident?x+1:0,resident?y+2:0,resident?z+3:0,resident?s+4:0};
    for(int k=0;k<10;k++){unsigned want=k<8?expected[k%4]:resident;if(p[k]!=want){fprintf(stderr,"ns=%d phase=%d xyz=%d,%d,%d sample=%d field=%d got=%u want=%u\n",ns,phase,x,y,z,s,k,p[k],want);fprintf(stderr,"tex=%u,%u,%u,%u image=%u,%u,%u,%u resident=%u,%u\n",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9]);return 1;}}
   }
   CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));CHECK(glGetError()==GL_NO_ERROR);printf("PASS samples=%d phase=%d pages=%dx%d layers=%d\n",ns,phase,px,py,layers);
  }
  glDeleteBuffers(1,&buf);glDeleteTextures(1,&tex);
 }
 puts("PASS: sparse multisample values, residency and rebinds");return 0;
}
