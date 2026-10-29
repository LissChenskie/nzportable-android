// SPDX-License-Identifier: GPL-2.0-or-later
// Android EGL surface + bounded offscreen rendering, using Vril's SDL API.
#include "nzportable_def.h"
#include "platform/sdl/sdl_local.h"
#include "android_port.h"
#include "render_math.h"

SDL_Window *sdl_window;
SDL_GLContext sdl_gl_context;
int sdl_window_width=1,sdl_window_height=1;
/* Kept for old config compatibility, but never used as hidden ceilings. */
cvar_t vid_width={"vid_width","0",true},vid_height={"vid_height","0",true};
/* A percent below 100 is only applied after user explicitly chooses it. */
cvar_t vid_render_scale={"vid_render_scale","100",true};
cvar_t vid_ui_scale={"vid_ui_scale","100",true};
cvar_t vid_fullscreen={"vid_fullscreen","1",true},r_vsync={"r_vsync","1",true};
cvar_t gl_ztrick={"gl_ztrick","0"};
unsigned d_8to24table[256];unsigned char d_15to8table[65536];
int texture_mode=GL_LINEAR;
double gldepthmin,gldepthmax;
qboolean isPermedia=true;
/* 100% renders directly to the native EGL surface, no full-frame copy.
 * Only explicit sub-100% creates one offscreen 3D FBO; the UI stays native. */
static GLuint world_framebuffer,world_color,world_depth;
static int drawable_w,drawable_h,world_w,world_h;
static int active_percent=-1,swap_interval=-1,software_sync,has_world_target;
static int hud_active;
static unsigned render_rebuilds;
static int max_texture_size;

static void delete_target(void){
    NZP_GLES_Flush();
    NZP_GLES_SetNativeViewport();
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    if(world_framebuffer)glDeleteFramebuffers(1,&world_framebuffer);
    if(world_color)glDeleteTextures(1,&world_color);
    if(world_depth)glDeleteRenderbuffers(1,&world_depth);
    world_framebuffer=world_color=world_depth=0;
    has_world_target=0;
}
static int create_target(int w,int h,GLuint *fbo,GLuint *tex,GLuint *depth,
                          int linear)
{
    glGenTextures(1,tex);
    glBindTexture(GL_TEXTURE_2D,*tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenRenderbuffers(1,depth);
    glBindRenderbuffer(GL_RENDERBUFFER,*depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,w,h);
    glGenFramebuffers(1,fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,*fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,*tex,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,*depth);
    GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status!=GL_FRAMEBUFFER_COMPLETE) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
            "Optional 3D FBO %dx%d unsupported (0x%x): falling back to native",
            w,h,(unsigned)status);
        return 0;
    }
    return 1;
}
static int render_percent(void) {
    int percent=(int)(vid_render_scale.value+0.5f);
    /* Only the advertised menu choices, not a thermal-dependent adjustment. */
    if(percent>=95)return 100;
    if(percent>=85)return 90;
    if(percent>=78)return 80;
    if(percent>=68)return 75;
    return 60;
}
void VID_SDLResize(void) {
    if(!sdl_window)return;
    int width=0,height=0;
    SDL_GL_GetDrawableSize(sdl_window,&width,&height);
    if(width<=0||height<=0)return;
    int percent=render_percent();
    int target_w,target_h;
    nzp_render_percent(width,height,percent,&target_w,&target_h);
    /* A UI scale is a display preference; do not mix it with 3D resolution. */
    int ui_percent=(int)(vid_ui_scale.value+0.5f);
    if(ui_percent<70)ui_percent=70;
    if(ui_percent>150)ui_percent=150;
    int ui_scale=(int)lroundf((float)height*ui_percent/(STD_UI_HEIGHT*100.f));
    if(ui_scale<1)ui_scale=1;
    if(ui_scale!=vid.scale) {
        vid.scale=ui_scale;
        vid.recalc_refdef=true;
    }
    if(active_percent>=0 && width==drawable_w && height==drawable_h &&
       percent==active_percent) return;
    delete_target();
    drawable_w=width;drawable_h=height;
    world_w=target_w;world_h=target_h;
    active_percent=percent;
    if(percent!=100 && (max_texture_size<=0 ||
                        (target_w<=max_texture_size&&target_h<=max_texture_size))) {
        if(create_target(target_w,target_h,
                         &world_framebuffer,&world_color,&world_depth,1)) {
            has_world_target=1;
        } else {
            delete_target();
            world_w=width;world_h=height;
        }
    } else if(percent!=100) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Optional 3D %dx%d exceeds GLES texture max %d: keeping native quality",
            target_w,target_h,max_texture_size);
        world_w=width;world_h=height;
    }
    glBindTexture(GL_TEXTURE_2D,0);
    Hyena_InvalidateTextureBinding();
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    vid.width=vid.maxwarpwidth=sdl_window_width=width;
    vid.height=vid.maxwarpheight=sdl_window_height=height;
    vid.conwidth=width;vid.conheight=height;
    vid.aspect=((float)height/width)*(320.f/240.f);
    vid.recalc_refdef=true;
    ++render_rebuilds;
    SDL_Log("NZP_RESOLUTION surface=%dx%d world=%dx%d (%d%%) ui_scale=%d%% fbo_rebuild=%u",
            width,height,target_w,target_h,percent,ui_percent,render_rebuilds);
}
/* Between 3D and 2D, upscale only the WORLD into the native HUD buffer.
 * No 2D glyphs, menu or touch overlay are ever rendered at low resolution. */
void VID_BeginNativeUI(void) {
    if(hud_active)return;
    hud_active=1;
    NZP_GLES_Flush();
    NZP_GLES_SetNativeViewport();
    if(has_world_target) {
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        NZP_GLES_Present(world_color,drawable_w,drawable_h);
    }
    glViewport(0,0,drawable_w,drawable_h);
}
void VID_SetFullscreen(qboolean fullscreen){
    (void)fullscreen;
    Cvar_SetValue("vid_fullscreen",1);
}
void VID_SetVSync(qboolean vsync){
    swap_interval=vsync?1:0;
    SDL_GL_SetSwapInterval(swap_interval);
}
void GL_BeginRendering(int *x,int *y,int *w,int *h){
    VID_SDLResize();
    hud_active=0;
    if(has_world_target){
        glBindFramebuffer(GL_FRAMEBUFFER,world_framebuffer);
        NZP_GLES_SetWorldViewport(world_w,world_h,drawable_w,drawable_h);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        NZP_GLES_SetNativeViewport();
    }
    *x=*y=0;*w=vid.width;*h=vid.height;
    NZP_GLES_BeginFrame();
    glClear(GL_COLOR_BUFFER_BIT);
}
void GL_EndRendering(void){
    VID_BeginNativeUI();
    Draw_Flush();Android_DrawTouch();Draw_Flush();NZP_GLES_Flush();
    if(swap_interval!=(r_vsync.value!=0))VID_SetVSync(r_vsync.value!=0);
    if(software_sync)glFinish();
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
    Cvar_RegisterVariable(&vid_width);Cvar_RegisterVariable(&vid_height);
    Cvar_RegisterVariable(&vid_render_scale);Cvar_RegisterVariable(&vid_ui_scale);
    Cvar_RegisterVariable(&vid_fullscreen);
    Cvar_RegisterVariable(&r_vsync);Cvar_RegisterVariable(&gl_ztrick);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
    SDL_DisplayMode mode;
    int initial_width=640,initial_height=360;
    if(SDL_GetCurrentDisplayMode(0,&mode)==0 && mode.w>0 && mode.h>0) {
        initial_width=mode.w; initial_height=mode.h;
    }
    sdl_window=SDL_CreateWindow("NZPortable Android",SDL_WINDOWPOS_CENTERED,
       SDL_WINDOWPOS_CENTERED,initial_width,initial_height,
       SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!sdl_window)Sys_Error("Android window: %s",SDL_GetError());
    sdl_gl_context=SDL_GL_CreateContext(sdl_window);if(!sdl_gl_context)Sys_Error("Android GLES context: %s",SDL_GetError());
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture_size);
    const char *renderer=(const char*)glGetString(GL_RENDERER);
    software_sync=renderer&&(strstr(renderer,"Emulator")||strstr(renderer,"SwiftShader"));
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
