/* SPDX-License-Identifier: MIT
 * Fragment image store and framebuffer color oracle; run hardware under GPU guard.
 */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL line %d: %s GL=%x\n", __LINE__, #x, glGetError()); \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static GLuint shader(GLenum type, const char *text) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &text, 0);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(s, sizeof(log), 0, log);
    fputs(log, stderr);
  }
  CHECK(ok);
  return s;
}
int main(int argc, char **argv) {
  bool typed=false,attachmentless=false,sampled=false,buffer=false,pbo=false,cpu_bgra=false;
  for(int i=1;i<argc;i++) {
    if(!strcmp(argv[i],"typed"))typed=true;
    else if(!strcmp(argv[i],"sampled"))sampled=true;
    else if(!strcmp(argv[i],"buffer"))buffer=true;
    else if(!strcmp(argv[i],"pbo-bgra"))pbo=true;
    else if(!strcmp(argv[i],"cpu-bgra"))cpu_bgra=true;
    else {CHECK(!strcmp(argv[i],"attachmentless"));attachmentless=true;}
  }
  EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,0,0);
  CHECK(eglInitialize(d,0,0) && eglBindAPI(EGL_OPENGL_API));
  const EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
  EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
  CHECK(c!=EGL_NO_CONTEXT && eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
  const char *renderer=(const char *)glGetString(GL_RENDERER);
  printf("Renderer: %s typed=%u\n",renderer,typed);fflush(stdout);
  CHECK((strstr(renderer,"zink") && strstr(renderer,"M4")) ||
    (getenv("M4_TEST_LLVMPIPE") && strstr(renderer,"llvmpipe")));
  GLuint program=glCreateProgram();
  glAttachShader(program,shader(GL_VERTEX_SHADER,
    "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}"));
  char fs[2048];snprintf(fs,sizeof(fs),
    "#version 330 core\n#extension GL_ARB_shader_image_load_store : require\n"
    "%s uniform writeonly %s dst;uniform sampler2D input_image;out vec4 color;void main(){"
    "ivec2 p=ivec2(gl_FragCoord.xy);imageStore(dst,%s,%s);"
    "color=vec4(1,0,0,1);}",typed?"layout(rgba8)":"",buffer?"imageBuffer":"image2D",buffer?"p.x+p.y*64":"p",sampled?"texture(input_image,gl_FragCoord.xy/64.)":"vec4(p.x<32?0.25:0.75,p.y<32?0.75:0.25,0.5,1)");
  glAttachShader(program,shader(GL_FRAGMENT_SHADER,fs));glLinkProgram(program);
  GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(program);
  GLint loc=glGetUniformLocation(program,"dst");CHECK(loc>=0);glUniform1i(loc,0);
  GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  GLuint tex,bo=0;glGenTextures(1,&tex);
  glBindTexture(buffer?GL_TEXTURE_BUFFER:GL_TEXTURE_2D,tex);
  unsigned char pixels[64*64*4];memset(pixels,0xa5,sizeof(pixels));
  if(buffer) {
    glGenBuffers(1,&bo);glBindBuffer(GL_TEXTURE_BUFFER,bo);
    glBufferData(GL_TEXTURE_BUFFER,sizeof(pixels),pixels,GL_DYNAMIC_COPY);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA8,bo);
    glGetBufferSubData(GL_TEXTURE_BUFFER,0,sizeof(pixels),pixels);
  } else {
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  }
  CHECK(glGetError()==GL_NO_ERROR);
  for(unsigned i=0;i<sizeof(pixels);i++) CHECK(pixels[i]==0xa5);
  puts("PASS: initial image readback");
  glBindImageTexture(0,tex,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA8);
  GLuint source=0;
  if(sampled) {
    glGenTextures(1,&source);glBindTexture(GL_TEXTURE_2D,source);
    for(unsigned i=0;i<4096;i++) {
      pixels[4*i]=i%64<32?64:191;pixels[4*i+1]=i/64<32?191:64;pixels[4*i+2]=128;pixels[4*i+3]=255;
    }
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    loc=glGetUniformLocation(program,"input_image");CHECK(loc>=0);glUniform1i(loc,0);
  }
  GLuint fb,rb;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
  glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);
  glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,64,64);
  if(attachmentless) {
    glFramebufferParameteri(GL_FRAMEBUFFER,GL_FRAMEBUFFER_DEFAULT_WIDTH,64);
    glFramebufferParameteri(GL_FRAMEBUFFER,GL_FRAMEBUFFER_DEFAULT_HEIGHT,64);
    glFramebufferParameteri(GL_FRAMEBUFFER,GL_FRAMEBUFFER_DEFAULT_LAYERS,1);
  } else glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
  CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
  glViewport(0,0,64,64);glDrawArrays(GL_TRIANGLES,0,3);
  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_TEXTURE_UPDATE_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  if(buffer) glGetBufferSubData(GL_TEXTURE_BUFFER,0,sizeof(pixels),pixels);
  else {
    glBindTexture(GL_TEXTURE_2D,tex);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  }
  CHECK(glGetError()==GL_NO_ERROR);
  unsigned bad=0;
  for(unsigned i=0;i<64*64;i++) {
    int red=i%64<32?64:191,green=i/64<32?191:64;
    if(abs(pixels[4*i]-red)>1 || abs(pixels[4*i+1]-green)>1 || abs(pixels[4*i+2]-128)>1 || pixels[4*i+3]!=255) {
      if(bad<2) fprintf(stderr,"pixel%u=%u,%u,%u,%u expected=%d,%d,128,255\n",i,pixels[4*i],pixels[4*i+1],pixels[4*i+2],pixels[4*i+3],red,green);
      bad++;
    }
  }
  printf("image bad=%u/4096\n",bad);
  if(!attachmentless) {
  if(pbo) {
    GLuint pack;glGenBuffers(1,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(pixels),pixels,GL_DYNAMIC_READ);
    glReadPixels(0,0,64,64,GL_BGRA,GL_UNSIGNED_BYTE,0);
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(pixels),pixels);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&pack);
  } else glReadPixels(0,0,64,64,cpu_bgra?GL_BGRA:GL_RGBA,GL_UNSIGNED_BYTE,pixels);
  CHECK(glGetError()==GL_NO_ERROR);
  unsigned color_bad=0;
  for(unsigned i=0;i<64*64;i++) if(pixels[4*i+((pbo||cpu_bgra)?2:0)]!=255 || pixels[4*i+1] || pixels[4*i+((pbo||cpu_bgra)?0:2)] || pixels[4*i+3]!=255)color_bad++;
  printf("color bgra=%u pbo=%u bad=%u/4096 first=%u,%u,%u,%u\n",(pbo||cpu_bgra),pbo,color_bad,pixels[0],pixels[1],pixels[2],pixels[3]);bad+=color_bad;
  }
  glDeleteTextures(1,&source);glDeleteBuffers(1,&bo);
  glFinish();glDeleteFramebuffers(1,&fb);glDeleteRenderbuffers(1,&rb);glDeleteTextures(1,&tex);
  glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
  eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglTerminate(d);
  printf("mode attachmentless=%u sampled=%u buffer=%u\n",attachmentless,sampled,buffer);
  printf("%s: fragment image %s\n",bad?"FAIL":"PASS",attachmentless?"writes without attachments":"and color writes");return bad?1:0;
}
