// SPDX-License-Identifier: GPL-2.0-or-later
// GLES2 camera-rotation motion blur on Mesa (real shaders). Behaviour only: says nothing about speed on a Mali GPU.
#include <EGL/egl.h>
#include "gles_compat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 256
#define H 192
#define CHECK(c,...) do{if(!(c)){printf("FAIL line %d: %s  ",__LINE__,#c);printf(__VA_ARGS__);printf("\n");exit(1);}}while(0)
typedef unsigned char px_t;
static void grab(px_t *out){glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,out);}
static double sharp(const px_t *a,int horizontal){ /* RMS neighbour difference of green */
    double sum=0;int n=0;for(int y=4;y<H-5;y++)for(int x=4;x<W-5;x++){const double d=a[(y*W+x)*4+1]-a[((y+!horizontal)*W+x+horizontal)*4+1];sum+=d*d;n++;}
    return sqrt(sum/n);
}
static double max_diff(const px_t *a,const px_t *b){int m=0;for(int i=0;i<W*H*4;i++)if(i%4!=3){int d=abs(a[i]-b[i]);if(d>m)m=d;}return m;}
static double mad(const px_t *a,const px_t *b){double s=0;for(int i=0;i<W*H*4;i++)if(i%4!=3)s+=abs(a[i]-b[i]);return s/(W*H*3);}
static void frame(GLuint tex,float step,float dt,const nzp_mblur_params *mb,int *drew,px_t *out){
    NZP_GLES_BeginFrame();
    const float h[9]={1,0,-step,0,1,0,0,0,1};NZP_GLES_SetReprojection(h);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,W,H);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    const int ok=mb?NZP_GLES_PresentBlur(tex,W,H,mb,dt):0;
    if(drew)*drew=ok;
    if(!ok)NZP_GLES_Present(tex,W,H);
    grab(out);
}
int main(void){
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(!eglInitialize(d,NULL,NULL))return 1;
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_DEPTH_SIZE,16,EGL_NONE};
    EGLConfig config;EGLint n;if(!eglChooseConfig(d,ca,&config,1,&n)||!n)return 1;
    const EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLSurface s=eglCreatePbufferSurface(d,config,pa);EGLContext c=eglCreateContext(d,config,EGL_NO_CONTEXT,ctx);
    if(!eglMakeCurrent(d,s,s,c))return 1;
    printf("GLES2 motion blur test GPU: %s\n",glGetString(GL_RENDERER));
    NZP_GLES_Init();
    px_t *tex_px=malloc(W*H*4);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){int v=40+x+y/2;if((x/8+y/8)%2)v+=70;if((x+2*y)%17==0)v=250;if(v>255)v=255;
        px_t *q=&tex_px[(y*W+x)*4];q[0]=v;q[1]=v;q[2]=v/2+30;q[3]=255;}
    GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,tex_px);
    nzp_mblur_params ultra,off;nzp_mblur_resolve(&ultra,4,0,0,0,0,0,100);nzp_mblur_resolve(&off,0,0,0,0,0,0,100);
    static px_t plain[W*H*4],still[W*H*4],pan[W*H*4],pan_a[W*H*4],pan_b[W*H*4];int drew=0;

    frame(tex,0.f,16.6f,NULL,&drew,plain);
    frame(tex,0.f,16.6f,&off,&drew,still);
    CHECK(!drew,"a disabled blur must not draw");CHECK(max_diff(plain,still)<=1,"disabled = plain copy");
    frame(tex,0.f,16.6f,&ultra,&drew,still);
    CHECK(drew,"enabled blur must draw");
    printf("still camera: max difference to the plain copy %.0f\n",max_diff(plain,still));
    CHECK(max_diff(plain,still)<=1,"a still camera must not be blurred");

    frame(tex,0.12f,16.6f,&ultra,&drew,pan);
    const double h0=sharp(plain,1),h1=sharp(pan,1),v0=sharp(plain,0),v1=sharp(pan,0);
    printf("pan: horizontal sharpness %.1f -> %.1f (%.0f%%), vertical %.1f -> %.1f (%.0f%%)\n",h0,h1,100*h1/h0,v0,v1,100*v1/v0);
    CHECK(h1<0.6*h0,"turning must blur along the motion");CHECK(h1/h0<0.7*(v1/v0),"directional");

    /* the same motion at 60 and 120 FPS looks the same: shutter time, not frames */
    frame(tex,0.12f,16.6f,&ultra,&drew,pan_a);frame(tex,0.06f,8.3f,&ultra,&drew,pan_b);
    printf("60 vs 120 FPS: mean difference %.2f\n",mad(pan_a,pan_b));CHECK(mad(pan_a,pan_b)<0.3,"frame rate independent");

    /* movement gain is Vulkan-only: it must not change the GLES2 picture; the camera gain must */
    nzp_mblur_params m0=ultra;m0.movement=0.f;frame(tex,0.12f,16.6f,&m0,&drew,pan_b);CHECK(max_diff(pan_a,pan_b)<=1,"movement gain ignored");
    nzp_mblur_params c0=ultra;c0.camera=0.f;frame(tex,0.12f,16.6f,&c0,&drew,pan_b);CHECK(!drew||max_diff(plain,pan_b)<=1,"camera gain 0 = no blur");
    nzp_mblur_params weak=ultra;weak.intensity=0.4f;frame(tex,0.3f,16.6f,&weak,&drew,pan_b);frame(tex,0.3f,16.6f,&ultra,&drew,pan_a);
    printf("intensity 0.4 -> sharpness %.1f, 1.0 -> %.1f (off %.1f)\n",sharp(pan_b,1),sharp(pan_a,1),h0);
    CHECK(sharp(pan_b,1)>sharp(pan_a,1)&&sharp(pan_b,1)<h0,"intensity scales the blur");
    nzp_mblur_params few=ultra;few.samples=4;nzp_mblur_params many=ultra;many.samples=16;
    frame(tex,0.5f,16.6f,&few,&drew,pan_a);frame(tex,0.5f,16.6f,&many,&drew,pan_b);printf("samples 4 vs 16: mean difference %.2f\n",mad(pan_a,pan_b));CHECK(mad(pan_a,pan_b)>0.2,"samples change the result");

    /* a reset (teleport, new map) is not blurred, the next frame is */
    NZP_GLES_BeginFrame();{const float h[9]={1,0,-0.5f,0,1,0,0,0,1};NZP_GLES_SetReprojection(h);}
    NZP_GLES_ResetMotionHistory();glClear(GL_COLOR_BUFFER_BIT);NZP_GLES_PresentBlur(tex,W,H,&ultra,16.6f);grab(pan_a);
    CHECK(max_diff(plain,pan_a)<=1,"the frame after a reset must not be blurred");
    frame(tex,0.12f,16.6f,&ultra,&drew,pan_a);CHECK(sharp(pan_a,1)<0.6*h0,"blur resumes after the reset frame");

    /* the engine's own drawing still works after the blur program was used */
    NZP_GLES_BeginFrame();glClear(GL_COLOR_BUFFER_BIT);{const float h[9]={1,0,-0.12f,0,1,0,0,0,1};NZP_GLES_SetReprojection(h);}
    NZP_GLES_PresentBlur(tex,W,H,&ultra,16.6f);
    glDisable(GL_TEXTURE_2D);glColor4f(0,1,0,1);glBegin(GL_QUADS);glVertex3f(-.2f,-.2f,0);glVertex3f(.2f,-.2f,0);glVertex3f(.2f,.2f,0);glVertex3f(-.2f,.2f,0);glEnd();NZP_GLES_Flush();
    px_t p[4];glReadPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    CHECK(p[0]<4&&p[1]>250&&p[2]<4,"engine drawing after the blur: %u %u %u",p[0],p[1],p[2]);
    CHECK(glGetError()==GL_NO_ERROR,"GL error");
    CHECK(!NZP_GLES_MotionBlurFailed(),"shader must compile");
    puts("GLES2 motion blur: still image, direction, frame rate, gains, settings, reset, state restore passed");
    return 0;
}
