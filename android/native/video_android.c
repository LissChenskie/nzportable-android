// SPDX-License-Identifier: GPL-2.0-or-later
// Android EGL surface + bounded offscreen rendering, using Vril's SDL API.
#include "nzportable_def.h"
#include "platform/sdl/sdl_local.h"
#include "android_port.h"
#include "render_math.h"

SDL_Window *sdl_window;
SDL_GLContext sdl_gl_context;
int sdl_window_width=1280,sdl_window_height=720;
cvar_t vid_width={"vid_width","1280",true},vid_height={"vid_height","720",true};
cvar_t vid_fullscreen={"vid_fullscreen","1",true},r_vsync={"r_vsync","1",true};
cvar_t gl_ztrick={"gl_ztrick","0"};
unsigned d_8to24table[256];unsigned char d_15to8table[65536];
int texture_mode=GL_LINEAR;
double gldepthmin,gldepthmax;
qboolean isPermedia=true;
static GLuint framebuffer,color,depth;
static int drawable_w,drawable_h,swap_interval=-1;

static void delete_target(void){
    NZP_GLES_Flush();glBindFramebuffer(GL_FRAMEBUFFER,0);
    if(framebuffer)glDeleteFramebuffers(1,&framebuffer);
    if(color)glDeleteTextures(1,&color);if(depth)glDeleteRenderbuffers(1,&depth);
    framebuffer=color=depth=0;
}
void VID_SDLResize(void){
    if(!sdl_window)return;int w,h,max_w=(int)vid_width.value,max_h=(int)vid_height.value;
    SDL_GL_GetDrawableSize(sdl_window,&drawable_w,&drawable_h);
    if(drawable_w<1||drawable_h<1)return;
    max_w=max_w<640?640:max_w>1600?1600:max_w;max_h=max_h<360?360:max_h>900?900:max_h;
    int thermal=Android_ThermalStatus();
    if(thermal>=2){if(max_w>1024)max_w=1024;if(max_h>576)max_h=576;}
    if(thermal>=3){if(max_w>960)max_w=960;if(max_h>540)max_h=540;}
    if(thermal>=4){max_w=854;max_h=480;}
    nzp_render_size(drawable_w,drawable_h,max_w,max_h,&w,&h);
    if(framebuffer&&w==(int)vid.width&&h==(int)vid.height)return;
    delete_target();
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,w,h);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)Sys_Error("Android render target incomplete");
    glBindTexture(GL_TEXTURE_2D,0);Hyena_InvalidateTextureBinding();
    vid.width=vid.maxwarpwidth=sdl_window_width=w;vid.height=vid.maxwarpheight=sdl_window_height=h;
    vid.conwidth=w;vid.conheight=h;vid.aspect=((float)h/w)*(320.f/240.f);
    vid.scale=h/STD_UI_HEIGHT;if(vid.scale<1)vid.scale=1;vid.recalc_refdef=true;
    SDL_Log("Android renderer: %dx%d -> %dx%d, thermal tier %d",w,h,drawable_w,drawable_h,thermal);
}
void VID_SetFullscreen(qboolean fullscreen){(void)fullscreen;Cvar_SetValue("vid_fullscreen",1);}
void VID_SetVSync(qboolean vsync){swap_interval=vsync?1:0;SDL_GL_SetSwapInterval(swap_interval);}
void GL_BeginRendering(int *x,int *y,int *w,int *h){
    VID_SDLResize();glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);*x=*y=0;*w=vid.width;*h=vid.height;
    NZP_GLES_BeginFrame();
    // Menus contain blended/fading layers and cannot inherit undefined FBO pixels.
    glClear(GL_COLOR_BUFFER_BIT);
}
void GL_EndRendering(void){
    Draw_Flush();Android_DrawTouch();Draw_Flush();NZP_GLES_Flush();
    glBindFramebuffer(GL_FRAMEBUFFER,0);NZP_GLES_Present(color,drawable_w,drawable_h);
    if(swap_interval!=(r_vsync.value!=0))VID_SetVSync(r_vsync.value!=0);
    SDL_GL_SwapWindow(sdl_window);
}
void GL_Init(void){
    NZP_GLES_Init();glClearDepth(1);glClearColor(16.f/255,32.f/255,64.f/255,1);
    glCullFace(GL_FRONT);glEnable(GL_TEXTURE_2D);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.666f);
    glShadeModel(GL_FLAT);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}
void VID_SetPalette(unsigned char *p){for(int i=0;i<256;i++)d_8to24table[i]=0xff000000u|p[i*3]|(p[i*3+1]<<8)|(p[i*3+2]<<16);d_8to24table[255]&=0xffffff;}
void VID_ShiftPalette(unsigned char *p){(void)p;}
void VID_Init(unsigned char *palette){
    Cvar_RegisterVariable(&vid_width);Cvar_RegisterVariable(&vid_height);Cvar_RegisterVariable(&vid_fullscreen);
    Cvar_RegisterVariable(&r_vsync);Cvar_RegisterVariable(&gl_ztrick);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
    sdl_window=SDL_CreateWindow("NZPortable Android",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!sdl_window)Sys_Error("Android window: %s",SDL_GetError());
    sdl_gl_context=SDL_GL_CreateContext(sdl_window);if(!sdl_gl_context)Sys_Error("Android GLES context: %s",SDL_GetError());
    SDL_Log("GPU: %s / %s / %s",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
    vid.colormap=host_colormap;vid.fullbright=256-LittleLong(*((int*)vid.colormap+2048));vid.numpages=2;
    GL_Init();VID_SetVSync(true);VID_SDLResize();
    // Preserve upstream palette gamma, without forcing gamma on Android's compositor.
    for(int i=0;i<768;i++)palette[i]=(unsigned char)fminf(255,powf((palette[i]+1)/256.f,.7f)*255+.5f);
    VID_SetPalette(palette);
}
void VID_Shutdown(void){
    if(sdl_gl_context){delete_target();NZP_GLES_Shutdown();SDL_GL_DeleteContext(sdl_gl_context);}
    if(sdl_window)SDL_DestroyWindow(sdl_window);sdl_gl_context=NULL;sdl_window=NULL;
}
