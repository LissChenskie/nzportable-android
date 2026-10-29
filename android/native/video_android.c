// SPDX-License-Identifier: GPL-2.0-or-later
// Android OpenGL ES 2: native display by default, optional 3D world scale.
// UI/HUD is always composited at the actual drawable resolution.
#include "nzportable_def.h"
#include "platform/sdl/sdl_local.h"
#include "android_port.h"
#include "render_math.h"

SDL_Window *sdl_window;
SDL_GLContext sdl_gl_context;
int sdl_window_width=1,sdl_window_height=1;
// Legacy vid_width/vid_height are not caps on Android. Keep aliases archived for
// old configs but NEVER use them to reduce the actual drawable resolution.
cvar_t vid_width={"vid_width","0",true},vid_height={"vid_height","0",true};
extern cvar_t android_profile;
cvar_t vid_render_scale={"vid_render_scale","100",true};
cvar_t vid_ui_scale={"vid_ui_scale","100",true};
cvar_t vid_fullscreen={"vid_fullscreen","1",true},r_vsync={"r_vsync","1",true};
cvar_t gl_ztrick={"gl_ztrick","0"};
unsigned d_8to24table[256];unsigned char d_15to8table[65536];
int texture_mode=GL_LINEAR;
double gldepthmin,gldepthmax;
qboolean isPermedia=true;
static GLuint framebuffer,color,depth;
static int drawable_w,drawable_h,scene_w,scene_h,swap_interval=-1;
static int software_sync,frame_ready,context_limit,reported_w,reported_h,reported_scale;

static void delete_target(void) {
    NZP_GLES_Flush();
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    if(framebuffer)glDeleteFramebuffers(1,&framebuffer);
    if(color)glDeleteTextures(1,&color);
    if(depth)glDeleteRenderbuffers(1,&depth);
    framebuffer=color=depth=0;
}
// Called on the render thread, including after Android surface changes.
void VID_SDLResize(void) {
    int next_w,next_h;
    if(!sdl_window)return;
    int window_w,window_h;
    SDL_GL_GetDrawableSize(sdl_window,&window_w,&window_h);
    if(window_w<1||window_h<1)return;
    int percent=(int)vid_render_scale.value;
    nzp_scale_size(window_w,window_h,percent,&next_w,&next_h);
    if((next_w>context_limit||next_h>context_limit) && context_limit>0) {
        // Do not silently degrade image fidelity due to GL_MAX_TEXTURE_SIZE.
        // The default framebuffer has no such FBO texture size restriction.
        if(percent<100) {
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Selected world FBO %dx%d exceeds GLES max texture %d; using native",
                next_w,next_h,context_limit);
            next_w=window_w;next_h=window_h;
        }
    }
    const int native=(next_w==window_w&&next_h==window_h);
    int changed=scene_w!=next_w||scene_h!=next_h||
                drawable_w!=window_w||drawable_h!=window_h||
                (!native&&!framebuffer)||(native&&framebuffer);
    drawable_w=window_w;drawable_h=window_h;
    if(!changed)return;

    if(framebuffer||color||depth)delete_target();
    if(!native) {
        glGenTextures(1,&color);
        glBindTexture(GL_TEXTURE_2D,color);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,next_w,next_h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenRenderbuffers(1,&depth);
        glBindRenderbuffer(GL_RENDERBUFFER,depth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,next_w,next_h);
        glGenFramebuffers(1,&framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
            Sys_Error("Cannot create requested Android 3D render target");
        glBindTexture(GL_TEXTURE_2D,0);
        Hyena_InvalidateTextureBinding();
    }
    scene_w=next_w;scene_h=next_h;
    vid.recalc_refdef=true;
    if(reported_w!=window_w||reported_h!=window_h||reported_scale!=percent) {
        SDL_Log("NZP_RESOLUTION native=%dx%d render=%dx%d selected=%d%% separate_ui=1",
            window_w,window_h,next_w,next_h,percent);
        reported_w=window_w;reported_h=window_h;reported_scale=percent;
    }
}
void VID_SetFullscreen(qboolean fullscreen) {
    (void)fullscreen;
    Cvar_SetValue("vid_fullscreen",1);
}
void VID_SetVSync(qboolean vsync) {
    swap_interval=vsync?1:0;
    SDL_GL_SetSwapInterval(swap_interval);
}
void GL_BeginRendering(int *x,int *y,int *w,int *h) {
    VID_SDLResize();
    if(scene_w<1||scene_h<1)return;
    // Scene-only dimensions; perspective and world LOD use real 3D size.
    vid.width=vid.conwidth=vid.maxwarpwidth=sdl_window_width=scene_w;
    vid.height=vid.conheight=vid.maxwarpheight=sdl_window_height=scene_h;
    vid.aspect=((float)scene_h/scene_w)*(320.f/240.f);
    vid.scale=nzp_ui_scale(drawable_h,(int)vid_ui_scale.value);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    *x=*y=0;*w=scene_w;*h=scene_h;
    NZP_GLES_BeginFrame();
    glViewport(0,0,scene_w,scene_h);
    // Legacy menu layers and fades require defined initial color.
    glClear(GL_COLOR_BUFFER_BIT);
    frame_ready=1;
}
// Called *after* V_RenderView and *before* Draw_Set2D.
// Present the scaled world once, then draw HUD and menus directly in native
// surface pixels to avoid low-resolution text and touch targets.
void VID_BeginNativeUI(void) {
    if(!frame_ready)return;
    NZP_GLES_Flush();
    if(framebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        NZP_GLES_Present(color,drawable_w,drawable_h);
    }
    vid.width=vid.conwidth=drawable_w;
    vid.height=vid.conheight=drawable_h;
    vid.maxwarpwidth=drawable_w;vid.maxwarpheight=drawable_h;
    vid.scale=nzp_ui_scale(drawable_h,(int)vid_ui_scale.value);
    glViewport(0,0,drawable_w,drawable_h);
}
void GL_EndRendering(void) {
    Draw_Flush();
    Android_DrawTouch();
    Draw_Flush();
    NZP_GLES_Flush();
    if(swap_interval!=(r_vsync.value!=0))VID_SetVSync(r_vsync.value!=0);
    // Only software emulators require synchronous GPU readback.
    if(software_sync)glFinish();
    SDL_GL_SwapWindow(sdl_window);
    frame_ready=0;
}
void GL_Init(void) {
    NZP_GLES_Init();
    glClearDepth(1);
    glClearColor(16.f/255,32.f/255,64.f/255,1);
    glCullFace(GL_FRONT);glEnable(GL_TEXTURE_2D);glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER,.666f);
    glShadeModel(GL_FLAT);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}
void VID_SetPalette(unsigned char *p) {
    for(int i=0;i<256;i++)
        d_8to24table[i]=0xff000000u|p[i*3]|(p[i*3+1]<<8)|(p[i*3+2]<<16);
    d_8to24table[255]&=0xffffff;
}
void VID_ShiftPalette(unsigned char *p){(void)p;}
void VID_Init(unsigned char *palette) {
    Cvar_RegisterVariable(&vid_width);Cvar_RegisterVariable(&vid_height);
    Cvar_RegisterVariable(&vid_render_scale);Cvar_RegisterVariable(&vid_ui_scale);
    Cvar_RegisterVariable(&android_profile);
    Cvar_RegisterVariable(&vid_fullscreen);Cvar_RegisterVariable(&r_vsync);
    Cvar_RegisterVariable(&gl_ztrick);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
    SDL_DisplayMode mode;
    int native_width=640,native_height=480;
    if(SDL_GetCurrentDisplayMode(0,&mode)==0) {
        native_width=mode.w;native_height=mode.h;
    }
    sdl_window=SDL_CreateWindow("NZPortable Android",SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,native_width,native_height,
        SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_ALLOW_HIGHDPI|
        SDL_WINDOW_RESIZABLE);
    if(!sdl_window)Sys_Error("Android window: %s",SDL_GetError());
    sdl_gl_context=SDL_GL_CreateContext(sdl_window);
    if(!sdl_gl_context)Sys_Error("Android GLES context: %s",SDL_GetError());
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    software_sync=renderer&&(strstr(renderer,"Emulator")||strstr(renderer,"SwiftShader"));
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&context_limit);
    SDL_Log("GPU: %s / %s / %s",glGetString(GL_VENDOR),
        glGetString(GL_RENDERER),glGetString(GL_VERSION));
    vid.colormap=host_colormap;vid.fullbright=256-LittleLong(*((int*)vid.colormap+2048));
    vid.numpages=2;
    GL_Init();VID_SetVSync(true);VID_SDLResize();
    // Preserve upstream palette gamma; don't alter Android compositor gamma.
    for(int i=0;i<768;i++)
        palette[i]=(unsigned char)fminf(255,powf((palette[i]+1)/256.f,.7f)*255+.5f);
    VID_SetPalette(palette);
}
void VID_Shutdown(void) {
    if(sdl_gl_context) {
        delete_target();NZP_GLES_Shutdown();SDL_GL_DeleteContext(sdl_gl_context);
    }
    if(sdl_window)SDL_DestroyWindow(sdl_window);
    sdl_gl_context=NULL;sdl_window=NULL;
}
