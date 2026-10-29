// SPDX-License-Identifier: GPL-2.0-or-later
// Real GLES shader/FBO tests using Mesa EGL on the host (not mocked GL calls).
#include <EGL/egl.h>
#include "gles_compat.h"
#include "render_math.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void clear(void){glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
static void quad(float x0,float y0,float x1,float y1){glBegin(GL_QUADS);glVertex3f(x0,y0,0);glVertex3f(x1,y0,0);glVertex3f(x1,y1,0);glVertex3f(x0,y1,0);glEnd();}
static void pixel(int x,int y,int r,int g,int b){unsigned char p[4];glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    if(abs((int)p[0]-r)>3||abs((int)p[1]-g)>3||abs((int)p[2]-b)>3){fprintf(stderr,"pixel %d,%d: %u %u %u expected %d %d %d\n",x,y,p[0],p[1],p[2],r,g,b);abort();}assert(glGetError()==GL_NO_ERROR);}
int main(void){
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(d,NULL,NULL));
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_DEPTH_SIZE,16,EGL_NONE};
    EGLConfig config;EGLint n;assert(eglChooseConfig(d,ca,&config,1,&n)&&n);
    const EGLint pa[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLSurface s=eglCreatePbufferSurface(d,config,pa);EGLContext c=eglCreateContext(d,config,EGL_NO_CONTEXT,ctx);
    assert(eglMakeCurrent(d,s,s,c));printf("GLES test GPU: %s\n",glGetString(GL_RENDERER));
    NZP_GLES_Init();glViewport(0,0,64,64);glClearColor(0,0,0,1);glDisable(GL_TEXTURE_2D);glShadeModel(GL_SMOOTH);
    NZP_GLES_BeginFrame();
    clear();glColor4f(1,0,0,1);quad(-1,-1,0,1);glColor4f(0,1,0,1);quad(0,-1,1,1);pixel(10,32,255,0,0);pixel(50,32,0,255,0);
    // A raster-state change cannot retroactively alpha-test an earlier batch.
    clear();glColor4f(1,0,0,.2f);quad(-1,-1,0,1);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.5f);quad(0,-1,1,1);
    pixel(10,32,255,0,0);pixel(50,32,0,0,0);glDisable(GL_ALPHA_TEST);
    // Matrix mutation flushes before translation; stack pop restores geometry.
    clear();glColor4f(0,0,1,1);glPushMatrix();glTranslatef(.5f,0,0);glScalef(.25f,.25f,1);quad(-1,-1,1,1);glPopMatrix();
    pixel(48,32,0,0,255);pixel(8,32,0,0,0);
    clear();glColor4f(0,1,0,1);glBegin(GL_TRIANGLE_STRIP);glVertex3f(-1,-1,0);glVertex3f(1,-1,0);glVertex3f(-1,1,0);glVertex3f(1,1,0);glEnd();pixel(50,50,0,255,0);pixel(10,10,0,255,0);
    // Client arrays, packed short positions, byte colors, and indexed topology.
    struct {short xyz[3];unsigned char rgba[4];} vertices[]={{{-1,-1,0},{255,0,0,255}},{{1,-1,0},{255,0,0,255}},{{1,1,0},{255,0,0,255}},{{-1,1,0},{255,0,0,255}}};
    unsigned short indices[]={0,1,2,0,2,3};clear();glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3,GL_SHORT,sizeof(vertices[0]),vertices[0].xyz);glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(vertices[0]),vertices[0].rgba);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,indices);pixel(32,32,255,0,0);glDisableClientState(GL_COLOR_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
    clear();glShadeModel(GL_FLAT);glBegin(GL_TRIANGLES);glColor3f(1,0,0);glVertex3f(-1,-1,0);glColor3f(0,1,0);glVertex3f(1,-1,0);glColor3f(0,0,1);glVertex3f(0,1,0);glEnd();pixel(32,32,0,0,255);
    // Fog must be expressed in eye coordinates, independently of projection.
    clear();float fog[]={1,0,0,1};glFogfv(GL_FOG_COLOR,fog);glFogf(GL_FOG_START,0);glFogf(GL_FOG_END,1);glFogi(GL_FOG_MODE,GL_LINEAR);glEnable(GL_FOG);
    glColor4f(0,0,1,1);glPushMatrix();glTranslatef(0,0,-.5f);quad(-1,-1,1,1);glPopMatrix();pixel(32,32,128,0,128);glDisable(GL_FOG);
    // Static world VBO + streamed indices bypass CPU vertex expansion.
    const float positions[]={-1,-1,0,1,-1,0,1,1,0,-1,1,0};GLuint buffers[2];glGenBuffers(2,buffers);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBufferData(GL_ARRAY_BUFFER,sizeof(positions),positions,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STREAM_DRAW);
    nzp_gpu_array gpu[3]={{buffers[0],0,3,GL_FLOAT,0},{0},{0}};
    clear();glColor4f(0,1,0,1);assert(NZP_GLES_DrawGPU(GL_TRIANGLES,gpu,buffers[1],0,6));pixel(32,32,0,255,0);
    glColor4f(1,0,0,1);quad(-1,-1,0,1);pixel(10,32,255,0,0);pixel(50,32,0,255,0);glDeleteBuffers(2,buffers);
    // GLES texture modulation/replacement and texture matrix state.
    GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    const unsigned char texel[]={0,255,0,255};glTexImage2D(GL_TEXTURE_2D,0,4,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,texel);
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    clear();glEnable(GL_TEXTURE_2D);glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glColor4f(1,0,0,1);quad(-1,-1,1,1);pixel(32,32,0,255,0);
    clear();glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);quad(-1,-1,1,1);pixel(32,32,0,0,0);
    clear();NZP_GLES_Present(texture,64,64);pixel(32,32,0,255,0);glDeleteTextures(1,&texture);
    // Full mobile path: pixel-space HUD arrays -> offscreen FBO -> presentation.
    GLuint target,fbo;glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    clear();glDisable(GL_TEXTURE_2D);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,640,320,0,-99999,99999);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    float hud[]={0,0,0,640,0,0,640,320,0,0,0,0,640,320,0,0,320,0};unsigned char rgba[24];memset(rgba,255,sizeof(rgba));
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);glVertexPointer(3,GL_FLOAT,0,hud);glColorPointer(4,GL_UNSIGNED_BYTE,0,rgba);
    glDrawArrays(GL_TRIANGLES,0,6);NZP_GLES_Flush();pixel(32,32,255,255,255);glBindFramebuffer(GL_FRAMEBUFFER,0);NZP_GLES_Present(target,64,64);pixel(32,32,255,255,255);
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&target);
    glDisableClientState(GL_COLOR_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    // Rotate all three frame arenas and cross the streaming capacity boundary.
    for(int frame=0;frame<4;frame++){
        NZP_GLES_BeginFrame();clear();glColor4f(1,0,0,1);quad(-1,-1,0,1);
        for(int q=0;q<10000;q++)quad(2,2,3,3);
        glColor4f(0,0,1,1);quad(0,-1,1,1);pixel(10,32,255,0,0);pixel(50,32,0,0,255);
    }
    int rw,rh;nzp_render_size(2400,1080,1280,720,&rw,&rh);assert(rw==1280&&rh==576);nzp_render_size(1280,720,1024,576,&rw,&rh);assert(rw==1024&&rh==576);
    // Native 100% must never be clipped, rounded down or tied to device model.
    nzp_render_percent(2400,1080,100,&rw,&rh);assert(rw==2400&&rh==1080);
    nzp_render_percent(2341,1031,100,&rw,&rh);assert(rw==2341&&rh==1031);
    nzp_render_percent(2400,1080,90,&rw,&rh);assert(rw==2160&&rh==972);
    nzp_render_percent(2400,1080,80,&rw,&rh);assert(rw==1920&&rh==864);
    nzp_render_percent(2400,1080,75,&rw,&rh);assert(rw==1800&&rh==810);
    nzp_render_percent(2400,1080,60,&rw,&rh);assert(rw==1440&&rh==648);
    nzp_render_percent(1080,2400,100,&rw,&rh);assert(rw==1080&&rh==2400);
    NZP_GLES_SetWorldViewport(32,32,64,64);
    glViewport(0,0,64,64);
    GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    assert(viewport[2]==32&&viewport[3]==32);
    NZP_GLES_SetNativeViewport();
    glViewport(0,0,64,64);
    glGetIntegerv(GL_VIEWPORT,viewport);
    assert(viewport[2]==64&&viewport[3]==64);
    assert(glGetError()==GL_NO_ERROR);

    NZP_GLES_Shutdown();eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,c);eglDestroySurface(d,s);eglTerminate(d);
    puts("GLES pixel, batching, matrices, topology, fog and size tests passed");return 0;
}
