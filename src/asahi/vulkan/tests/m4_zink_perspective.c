/* SPDX-License-Identifier: MIT
 * CPU-reference pixel checks for perspective interpolation and clip planes.
 * Run inside the J713 watchdog/thermal guard.
 */
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define CHECK(x) do { if (!(x)) { \
   fprintf(stderr, "line %d: %s EGL=%x GL=%x\n", __LINE__, #x, \
           eglGetError(), glGetError()); exit(1); } } while (0)

static GLuint
shader(GLenum stage, const char *source)
{
   GLuint s = glCreateShader(stage);
   glShaderSource(s, 1, &source, NULL);
   glCompileShader(s);
   GLint ok;
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[4096];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      fprintf(stderr, "%s\n", log);
   }
   CHECK(ok);
   return s;
}

/* CPU homogeneous clipping, independent of the driver's clip/state path. */
static int
coverage(const float vertices[18], unsigned x, unsigned y)
{
   double poly[16][4], temp[16][4]; unsigned n=3;
   for(unsigned i=0;i<3;i++) for(unsigned c=0;c<4;c++) poly[i][c]=vertices[i*6+c];
   for(unsigned plane=0;plane<6 && n;plane++) {
      unsigned next=0, axis=plane/2; double sign=(plane&1)?-1:1;
      for(unsigned i=0;i<n;i++) {
         unsigned j=(i+1)%n;
         double da=poly[i][3]+sign*poly[i][axis], db=poly[j][3]+sign*poly[j][axis];
         if(da>=0) { memcpy(temp[next++],poly[i],sizeof(poly[i])); }
         if((da>=0)!=(db>=0)) {
            double t=da/(da-db);
            for(unsigned c=0;c<4;c++) temp[next][c]=poly[i][c]+t*(poly[j][c]-poly[i][c]);
            next++;
         }
      }
      n=next; memcpy(poly,temp,n*sizeof(poly[0]));
   }
   if(n<3) return 0;
   double px=(x+0.5)/32.0-1, py=(y+0.5)/32.0-1;
   bool inside=true, edge=false;
   for(unsigned i=0;i<n;i++) {
      unsigned j=(i+1)%n;
      if(poly[i][3]<=0 || poly[j][3]<=0) return -1;
      double ax=poly[i][0]/poly[i][3], ay=poly[i][1]/poly[i][3];
      double bx=poly[j][0]/poly[j][3], by=poly[j][1]/poly[j][3];
      double cross=(bx-ax)*(py-ay)-(by-ay)*(px-ax);
      double length=hypot(bx-ax,by-ay);
      if(length<1e-9) continue;
      if(fabs(cross)/length<0.02) edge=true;
      if(cross<0) inside=false;
   }
   return edge ? -1 : inside;
}

int
main(void)
{
   EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                       EGL_DEFAULT_DISPLAY, NULL);
   CHECK(d != EGL_NO_DISPLAY && eglInitialize(d, NULL, NULL) &&
         eglBindAPI(EGL_OPENGL_ES_API));
   const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
   EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
   CHECK(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   fprintf(stderr, "renderer: %s\n", renderer);
   CHECK(renderer && strstr(renderer, "zink") && strstr(renderer, "M4") &&
         !strstr(renderer, "llvmpipe"));
   GLuint vs = shader(GL_VERTEX_SHADER,
      "#version 300 es\nlayout(location=0) in vec4 pos; layout(location=1) in vec2 tex; "
      "out highp vec2 uv; void main(){gl_Position=pos; uv=tex;}");
   GLuint fs = shader(GL_FRAGMENT_SHADER,
      "#version 300 es\nprecision highp float; in vec2 uv; out vec4 result; "
      "void main(){result=vec4(uv,0.5,1);}");
   GLuint p = glCreateProgram();
   glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
   GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); CHECK(ok);
   glDeleteShader(vs); glDeleteShader(fs);
   GLuint vao,vbo,fbo,color;
   /* The same screen triangle has a 4:1 clip-W ratio. Its smooth UV must be
    * divided by the interpolated reciprocal W, not interpolated linearly. */
   const float verts[] = {-1,-1,0,1, 0,0, 1,-1,0,1, 1,0, -4,4,0,4, 0,1};
   glGenVertexArrays(1,&vao); glBindVertexArray(vao);
   glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
   glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
   glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,6*sizeof(float),0);
   glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,6*sizeof(float),(void *)(4*sizeof(float)));
   glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
   glGenTextures(1,&color); glBindTexture(GL_TEXTURE_2D,color);
   glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,64,64);
   glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
   CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   glViewport(0,0,64,64); glDisable(GL_DITHER); glUseProgram(p);
   glClearColor(1,0,1,0); glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES,0,3);
   uint8_t pixels[64][64][4];
   glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
   CHECK(glGetError()==GL_NO_ERROR);
   unsigned checked=0, failures=0;
   for(unsigned y=1;y<62;y++) for(unsigned x=1;x+y<62;x++) {
      double a=(x+0.5)/64.0, b=(y+0.5)/64.0, invw=1.0-0.75*b;
      int expected[4]={(int)(255*a/invw+0.5),(int)(255*0.25*b/invw+0.5),128,255};
      for(unsigned ch=0;ch<4;ch++) if(abs((int)pixels[y][x][ch]-expected[ch])>1) {
         if(failures<12) fprintf(stderr,"FAIL perspective x=%u y=%u ch=%u got=%u expected=%d\n",
                                 x,y,ch,pixels[y][x][ch],expected[ch]);
         failures++; break;
      }
      checked++;
   }
   fprintf(stderr,"perspective: %u pixels checked, %u failures (UNORM tolerance 1)\n",checked,failures);
   for (unsigned clip = 0; clip < 9; ++clip) {
      float v[] = {-0.8,-0.8,0,1, 0,0, 0.8,-0.8,0,1, 1,0, -0.8,0.8,0,1, 0,1};
      for (unsigned vertex = 0; vertex < 3; ++vertex) {
         if (clip < 2) v[vertex*6+2] = clip ? 2.0f : -2.0f;
         if (clip == 2) v[vertex*6+3] = -1.0f;
      }
      if (clip == 3 || clip == 4) v[2] = clip == 3 ? -2.0f : 2.0f;
      if (clip == 5) v[3] = -0.25f;
      if (clip == 6) v[3] = 0.01f;
      if (clip == 7) v[0] = v[1] = -50.0f;
      if (clip == 8) { v[3] = -0.25f; v[2] = -1.0f; }
      glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(v),v);
      glClearColor(1,0,1,0); glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES,0,3);
      glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
      CHECK(glGetError()==GL_NO_ERROR);
      unsigned bad=0, count=0;
      for(unsigned y=0;y<64;y++) for(unsigned x=0;x<64;x++) {
         int inside=coverage(v,x,y);
         if(inside<0) continue;
         if (pixels[y][x][3] != (inside ? 255 : 0)) {
            if(bad<4) fprintf(stderr,"FAIL clip=%u x=%u y=%u alpha=%u expected=%u\n",
                              clip,x,y,pixels[y][x][3],inside?255:0);
            bad++;
         }
         count++;
      }
      fprintf(stderr,"clip case %u: %u pixels checked, %u failures\n",clip,count,bad);
      failures+=bad;
   }
   glEnable(GL_CULL_FACE);
   for(unsigned mode=0;mode<4;mode++) {
      const float v[]={-0.8,-0.8,0,1,0,0, 0.8,-0.8,0,1,1,0, -0.8,0.8,0,1,0,1};
      bool front_ccw=!(mode&1), cull_front=!!(mode&2);
      glFrontFace(front_ccw?GL_CCW:GL_CW); glCullFace(cull_front?GL_FRONT:GL_BACK);
      glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(v),v);
      glClearColor(1,0,1,0); glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES,0,3);
      glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels); CHECK(glGetError()==GL_NO_ERROR);
      unsigned bad=0;
      for(unsigned y=0;y<64;y++) for(unsigned x=0;x<64;x++) {
         int cov=coverage(v,x,y); if(cov<0) continue;
         bool inside=cov && (front_ccw!=cull_front);
         if(pixels[y][x][3]!=(inside?255:0)) {
            if(bad<4) fprintf(stderr,"FAIL cull=%u x=%u y=%u alpha=%u expected=%u\n",mode,x,y,pixels[y][x][3],inside?255:0);
            bad++;
         }
      }
      fprintf(stderr,"cull case %u: %u failures\n",mode,bad); failures+=bad;
   }
   glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&color);
   glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao); glDeleteProgram(p);
   CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
   CHECK(eglDestroyContext(d,c) && eglTerminate(d));
   return failures ? 1 : 0;
}
