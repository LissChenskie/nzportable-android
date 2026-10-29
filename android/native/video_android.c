// SPDX-License-Identifier: GPL-2.0-or-later
// Android EGL surface + bounded offscreen rendering, using Vril's SDL API.
#include "nzportable_def.h"
#include "platform/sdl/sdl_local.h"
#include "android_port.h"
#include "render_math.h"
#include "vulkan/renderer.h"
#include <SDL_vulkan.h>
#include <unistd.h>
#include <time.h>

/* Backend preference is read before config.cfg, because VID_Init precedes
 * its execution. A separate tiny file avoids reparsing arbitrary console code. */
cvar_t vk_single_pass={"vk_single_pass","1",true};
cvar_t vid_benchmark={"vid_benchmark","0",false};
cvar_t vid_performance_mode={"vid_performance_mode","0",true};
cvar_t vid_graphics_api={"vid_graphics_api","0",true};
static char api_path[MAX_OSPATH*2],guard_path[MAX_OSPATH*2],failed_path[MAX_OSPATH*2];

void VID_SetGraphicsAPIChoice(void) {
    int choice=((int)vid_graphics_api.value+1)%3;
    Cvar_SetValue("vid_graphics_api",choice);
    FILE *f=fopen(api_path,"wb");if(f){fprintf(f,"%d\n",choice);fclose(f);}
    remove(guard_path);remove(failed_path);
}
char *VID_GraphicsAPIName(void) {
    static char text[64];int c=(int)vid_graphics_api.value;
    snprintf(text,sizeof(text),"%s / ACTIVE %s",c==1?"VULKAN":c==2?"GLES2":"AUTO",
             NZP_VK_Active()?"VULKAN":"GLES2");return text;
}
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
static GLuint benchmark_framebuffer,benchmark_color,benchmark_depth;
static int active_benchmark=-1;
static Uint64 benchmark_start,benchmark_previous;static unsigned long long benchmark_frames;
static double benchmark_intervals[600],benchmark_cpu_sum,benchmark_gpu_sum;
static unsigned benchmark_samples,benchmark_gpu_samples;
static nzp_vk_command_stats vk_bench_cmd, vk_screen_cmd;
static nzp_vk_frame_timings vk_bench_time, vk_screen_time;
static unsigned vk_screen_samples;
static void vk_accumulate(nzp_vk_command_stats *sum,nzp_vk_frame_timings *times){
    nzp_vk_command_stats now={0};nzp_vk_frame_timings timing={0};
    NZP_VK_GetCommandStats(&now);NZP_VK_GetFrameTimings(&timing);
    sum->draws+=now.draws;sum->pipeline_binds+=now.pipeline_binds;
    sum->viewport_sets+=now.viewport_sets;sum->scissor_sets+=now.scissor_sets;
    sum->depth_bias_sets+=now.depth_bias_sets;sum->uniform_uploads+=now.uniform_uploads;
    sum->uniform_reuses+=now.uniform_reuses;sum->descriptor_binds+=now.descriptor_binds;
    times->fence_wait_ms+=timing.fence_wait_ms;times->acquire_ms+=timing.acquire_ms;
    times->queue_submit_ms+=timing.queue_submit_ms;times->present_ms+=timing.present_ms;
}
static struct timespec benchmark_cpu_start;
static int render_frame_available=1,benchmark_restore;
static float benchmark_saved_fps,benchmark_saved_vsync,benchmark_saved_profile;
extern cvar_t cl_maxfps,nzp_profile;
static int interval_compare(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
int VID_StartRendererBenchmark(void){
    if(cls.state!=ca_connected){Con_Printf("Load a map or replay a recorded demo before benchmarking.\n");return 0;}
    if(vid_benchmark.value)return 0;
    benchmark_saved_fps=cl_maxfps.value;benchmark_saved_vsync=r_vsync.value;benchmark_saved_profile=nzp_profile.value;benchmark_restore=1;
    Cvar_SetValue("cl_maxfps",0);Cvar_SetValue("r_vsync",0);Cvar_SetValue("nzp_profile",1);Cvar_SetValue("vid_benchmark",1);
    return 1;
}
static int drawable_w,drawable_h,world_w,world_h;
static int active_percent=-1,swap_interval=-1,software_sync,has_world_target;
static int hud_active;
static unsigned render_rebuilds;
void VID_GetWorldResolution(int *w,int *h) {
    if(w)*w=world_w;
    if(h)*h=world_h;
}
static int max_texture_size;

static void delete_target(void){
    NZP_GLES_Flush();
    NZP_GLES_SetNativeViewport();
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    if(world_framebuffer)glDeleteFramebuffers(1,&world_framebuffer);
    if(world_color)glDeleteTextures(1,&world_color);
    if(world_depth)glDeleteRenderbuffers(1,&world_depth);
    if(benchmark_framebuffer)glDeleteFramebuffers(1,&benchmark_framebuffer);
    if(benchmark_color)glDeleteTextures(1,&benchmark_color);
    if(benchmark_depth)glDeleteRenderbuffers(1,&benchmark_depth);
    benchmark_framebuffer=benchmark_color=benchmark_depth=0;
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
    if(NZP_VK_Active())NZP_VK_GetSize(&width,&height);
    else SDL_GL_GetDrawableSize(sdl_window,&width,&height);
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
       percent==active_percent && active_benchmark==(vid_benchmark.value!=0)) return;
    if(!NZP_VK_Active())delete_target();
    drawable_w=width;drawable_h=height;
    world_w=target_w;world_h=target_h;
    active_percent=percent;active_benchmark=vid_benchmark.value!=0;
    if(!NZP_VK_Active() && percent!=100 && (max_texture_size<=0 ||
                        (target_w<=max_texture_size&&target_h<=max_texture_size))) {
        if(create_target(target_w,target_h,
                         &world_framebuffer,&world_color,&world_depth,1)) {
            has_world_target=1;
        } else {
            delete_target();
            world_w=width;world_h=height;
        }
    } else if(!NZP_VK_Active() && percent!=100) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Optional 3D %dx%d exceeds GLES texture max %d: keeping native quality",
            target_w,target_h,max_texture_size);
        world_w=width;world_h=height;
    }
    if(!NZP_VK_Active() && active_benchmark){
        if(!create_target(width,height,&benchmark_framebuffer,&benchmark_color,&benchmark_depth,1))Sys_Error("Benchmark FBO unsupported");
    }
    if(!NZP_VK_Active()){glBindTexture(GL_TEXTURE_2D,0);Hyena_InvalidateTextureBinding();glBindFramebuffer(GL_FRAMEBUFFER,0);}
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
    if(NZP_VK_Active()){NZP_VK_BeginUI();glViewport(0,0,drawable_w,drawable_h);return;}
    if(has_world_target) {
        glBindFramebuffer(GL_FRAMEBUFFER,active_benchmark?benchmark_framebuffer:0);
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
    if(!NZP_VK_Active())SDL_GL_SetSwapInterval(swap_interval);
}
void GL_BeginRendering(int *x,int *y,int *w,int *h){
    if(vid_benchmark.value){
        if(!benchmark_start){benchmark_start=SDL_GetPerformanceCounter();benchmark_previous=benchmark_start;benchmark_frames=0;benchmark_samples=benchmark_gpu_samples=0;benchmark_cpu_sum=benchmark_gpu_sum=0;vk_bench_cmd=(nzp_vk_command_stats){0};vk_bench_time=(nzp_vk_frame_timings){0};}
        clock_gettime(CLOCK_THREAD_CPUTIME_ID,&benchmark_cpu_start);
    }
    render_frame_available=1;
    if(NZP_VK_Active())NZP_VK_SetBenchmark(vid_benchmark.value!=0);
    if(NZP_VK_Active())render_frame_available=NZP_VK_BeginFrame(r_vsync.value!=0,render_percent());
    VID_SDLResize();
    hud_active=0;
    if(NZP_VK_Active()){NZP_GLES_SetNativeViewport();}
    else if(has_world_target){
        glBindFramebuffer(GL_FRAMEBUFFER,world_framebuffer);
        NZP_GLES_SetWorldViewport(world_w,world_h,drawable_w,drawable_h);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER,active_benchmark?benchmark_framebuffer:0);
        NZP_GLES_SetNativeViewport();
    }
    *x=*y=0;*w=vid.width;*h=vid.height;
    NZP_GLES_BeginFrame();
    glClear(GL_COLOR_BUFFER_BIT);
}
void GL_EndRendering(void){
    VID_BeginNativeUI();
    Draw_Flush();Android_DrawTouch();Draw_Flush();NZP_GLES_Flush();
    if(!render_frame_available)return;
    if(swap_interval!=(r_vsync.value!=0))VID_SetVSync(r_vsync.value!=0);
    if(software_sync)glFinish();
    if(NZP_VK_Active()) {
        NZP_VK_EndFrame();
        /* The startup marker is removed only on clean shutdown. An abnormal
         * driver/process termination therefore falls back on the next launch. */
    } else if(active_benchmark)glFlush();else SDL_GL_SwapWindow(sdl_window);
    if(NZP_VK_Active()){
        if(vid_benchmark.value){vk_accumulate(&vk_bench_cmd,&vk_bench_time);}
        else if(nzp_profile.value){
            vk_accumulate(&vk_screen_cmd,&vk_screen_time);
            if(++vk_screen_samples==120){
                SDL_Log("NZP_VK_STAGES on-screen frames=%u avg_fence_wait_ms=%.3f avg_acquire_ms=%.3f avg_submit_ms=%.3f avg_present_ms=%.3f draws_per_frame=%.1f pipeline_binds_per_frame=%.1f uniform_reuses_per_frame=%.1f descriptor_binds_per_frame=%.1f",
                    vk_screen_samples,vk_screen_time.fence_wait_ms/vk_screen_samples,vk_screen_time.acquire_ms/vk_screen_samples,
                    vk_screen_time.queue_submit_ms/vk_screen_samples,vk_screen_time.present_ms/vk_screen_samples,
                    (double)vk_screen_cmd.draws/vk_screen_samples,(double)vk_screen_cmd.pipeline_binds/vk_screen_samples,
                    (double)vk_screen_cmd.uniform_reuses/vk_screen_samples,(double)vk_screen_cmd.descriptor_binds/vk_screen_samples);
                vk_screen_cmd=(nzp_vk_command_stats){0};vk_screen_time=(nzp_vk_frame_timings){0};vk_screen_samples=0;
            }
        }
    }
    if(vid_benchmark.value){
        Uint64 now=SDL_GetPerformanceCounter();
        benchmark_intervals[benchmark_samples++]=1000.0*(now-benchmark_previous)/SDL_GetPerformanceFrequency();benchmark_previous=now;
        struct timespec cpu_end;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu_end);
        benchmark_cpu_sum+=(cpu_end.tv_sec-benchmark_cpu_start.tv_sec)*1000.0+(cpu_end.tv_nsec-benchmark_cpu_start.tv_nsec)*0.000001;
        double gpu=NZP_VK_Active()?NZP_VK_GPUTime():-1;
        if(gpu>=0){benchmark_gpu_sum+=gpu;++benchmark_gpu_samples;}
        ++benchmark_frames;
        if(benchmark_frames==600){
            // One explicit completion wait at the end of an offscreen test;
            // no per-frame device-idle calls in ordinary gameplay.
            if(NZP_VK_Active())NZP_VK_Finish();else glFinish();
            double seconds=(double)(SDL_GetPerformanceCounter()-benchmark_start)/SDL_GetPerformanceFrequency();
            qsort(benchmark_intervals,benchmark_samples,sizeof(double),interval_compare);
            unsigned n1=(benchmark_samples+99)/100;double worst=0;
            for(unsigned i=benchmark_samples-n1;i<benchmark_samples;i++)worst+=benchmark_intervals[i];
            double low=worst>0?1000.0*n1/worst:0;
            FILE *f=fopen(va("%s/renderer-benchmark.csv",com_gamedir),"a");
            if(f){
                fseek(f,0,SEEK_END);if(ftell(f)==0)fprintf(f,"backend,native_w,native_h,world_percent,completed_frames,seconds,throughput_fps,low_1pct_submission_fps,p95_submission_ms,p99_submission_ms,cpu_render_ms_avg,gpu_draw_ms_avg,gpu_samples\n");
                fprintf(f,"%s,%d,%d,%d,%llu,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u\n",NZP_VK_Active()?"Vulkan":"GLES2",drawable_w,drawable_h,render_percent(),benchmark_frames,seconds,benchmark_frames/seconds,low,
                    benchmark_intervals[(benchmark_samples*95+99)/100-1],benchmark_intervals[(benchmark_samples*99+99)/100-1],benchmark_cpu_sum/benchmark_frames,
                    benchmark_gpu_samples?benchmark_gpu_sum/benchmark_gpu_samples:-1,benchmark_gpu_samples);fclose(f);
            }
            if(NZP_VK_Active()){
                FILE *stage=fopen(va("%s/vulkan-stage-benchmark.csv",com_gamedir),"a");
                if(stage){
                    fseek(stage,0,SEEK_END);
                    if(ftell(stage)==0)fprintf(stage,"native_w,native_h,world_percent,frames,fence_wait_ms,acquire_ms,submit_ms,present_ms,draws_per_frame,pipeline_binds_per_frame,viewport_sets_per_frame,scissor_sets_per_frame,depth_bias_sets_per_frame,uniform_uploads_per_frame,uniform_reuses_per_frame,descriptor_binds_per_frame\n");
                    fprintf(stage,"%d,%d,%d,%llu,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                        drawable_w,drawable_h,render_percent(),benchmark_frames,
                        vk_bench_time.fence_wait_ms/benchmark_frames,vk_bench_time.acquire_ms/benchmark_frames,
                        vk_bench_time.queue_submit_ms/benchmark_frames,vk_bench_time.present_ms/benchmark_frames,
                        (double)vk_bench_cmd.draws/benchmark_frames,(double)vk_bench_cmd.pipeline_binds/benchmark_frames,
                        (double)vk_bench_cmd.viewport_sets/benchmark_frames,(double)vk_bench_cmd.scissor_sets/benchmark_frames,
                        (double)vk_bench_cmd.depth_bias_sets/benchmark_frames,(double)vk_bench_cmd.uniform_uploads/benchmark_frames,
                        (double)vk_bench_cmd.uniform_reuses/benchmark_frames,(double)vk_bench_cmd.descriptor_binds/benchmark_frames);
                    fclose(stage);
                }
            }
            Con_Printf("Offscreen renderer benchmark: %.2f FPS, %llu completed frames in %.3f s\n",benchmark_frames/seconds,benchmark_frames,seconds);
            Cvar_SetValue("vid_benchmark",0);benchmark_start=0;
            if(benchmark_restore){Cvar_SetValue("cl_maxfps",benchmark_saved_fps);Cvar_SetValue("r_vsync",benchmark_saved_vsync);Cvar_SetValue("nzp_profile",benchmark_saved_profile);benchmark_restore=0;}

        }
    } else benchmark_start=0;
}
void GL_Init(void){
    NZP_GLES_Init();glClearDepth(1);glClearColor(16.f/255,32.f/255,64.f/255,1);
    glCullFace(GL_FRONT);glEnable(GL_TEXTURE_2D);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.666f);
    glShadeModel(GL_FLAT);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}
void VID_SetPalette(unsigned char *p){for(int i=0;i<256;i++)d_8to24table[i]=0xff000000u|p[i*3]|(p[i*3+1]<<8)|(p[i*3+2]<<16);d_8to24table[255]&=0xffffff;}
void VID_ShiftPalette(unsigned char *p){(void)p;}
void VID_Init(unsigned char *palette){
    snprintf(api_path,sizeof(api_path),"%s/graphics-api.txt",com_gamedir);
    snprintf(guard_path,sizeof(guard_path),"%s/vulkan-starting",com_gamedir);
    snprintf(failed_path,sizeof(failed_path),"%s/vulkan-failed",com_gamedir);
    int preference=0;FILE *pref=fopen(api_path,"rb");
    if(pref){if(fscanf(pref,"%d",&preference)!=1)preference=0;fclose(pref);}
    if(preference<0||preference>2)preference=0;
    SDL_Log("NZP_GRAPHICS preference=%d path=%s",preference,api_path);
    Cvar_RegisterVariable(&vk_single_pass);Cvar_RegisterVariable(&vid_benchmark);Cvar_RegisterVariable(&vid_performance_mode);
    Cvar_RegisterVariable(&vid_graphics_api);Cvar_SetValue("vid_graphics_api",preference);
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
    if(access(guard_path,F_OK)==0){
        FILE *f=fopen(failed_path,"wb");if(f)fclose(f);remove(guard_path);
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,"Previous Vulkan startup did not complete; using GLES2 until API is reselected");
    }
    /* Auto remains on the validated GLES2 baseline until this driver has been
     * qualified on actual hardware. Explicit Vulkan opts into the new backend. */
    if(preference==1 && access(failed_path,F_OK)!=0) {
        FILE *f=fopen(guard_path,"wb");if(f)fclose(f);
        sdl_window=SDL_CreateWindow("NZPortable Vulkan",SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,initial_width,initial_height,
            SDL_WINDOW_VULKAN|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_ALLOW_HIGHDPI);
        if(sdl_window && NZP_VK_Init(sdl_window,com_gamedir,COM_CheckParm("-vkvalidation")!=0)) {
            max_texture_size=NZP_VK_MaxTextureSize();
        } else {
            FILE *failed=fopen(failed_path,"wb");if(failed)fclose(failed);remove(guard_path);
            if(sdl_window)SDL_DestroyWindow(sdl_window);sdl_window=NULL;
        }
    }
    if(!NZP_VK_Active()) {
        sdl_window=SDL_CreateWindow("NZPortable Android",SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,initial_width,initial_height,
            SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_ALLOW_HIGHDPI);
        if(!sdl_window)Sys_Error("Android window: %s",SDL_GetError());
        sdl_gl_context=SDL_GL_CreateContext(sdl_window);
        if(!sdl_gl_context)Sys_Error("Android GLES context: %s",SDL_GetError());
        glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture_size);
        const char *renderer=(const char*)glGetString(GL_RENDERER);
        software_sync=renderer&&(strstr(renderer,"Emulator")||strstr(renderer,"SwiftShader"));
        SDL_Log("GPU: %s / %s / %s",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
    }
    vid.colormap=host_colormap;vid.fullbright=256-LittleLong(*((int*)vid.colormap+2048));vid.numpages=2;
    GL_Init();VID_SetVSync(true);VID_SDLResize();
    // Preserve upstream palette gamma, without forcing gamma on Android's compositor.
    for(int i=0;i<768;i++)palette[i]=(unsigned char)fminf(255,powf((palette[i]+1)/256.f,.7f)*255+.5f);
    VID_SetPalette(palette);
}
void VID_Shutdown(void){
    SDL_Log("NZP_GRAPHICS clean shutdown backend=%s",NZP_VK_Active()?"Vulkan":"GLES2");
    if(NZP_VK_Active()){NZP_GLES_Shutdown();NZP_VK_Shutdown();remove(guard_path);}
    if(sdl_gl_context){delete_target();NZP_GLES_Shutdown();SDL_GL_DeleteContext(sdl_gl_context);}
    if(sdl_window)SDL_DestroyWindow(sdl_window);sdl_gl_context=NULL;sdl_window=NULL;
}
