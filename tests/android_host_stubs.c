// SPDX-License-Identifier: GPL-2.0-or-later
// Host smoke runner only: real engine/rendering, dummy Android services/input.
#include "nzportable_def.h"
#include "android_port.h"
#include "vulkan/renderer.h"
#include <stdlib.h>
cvar_t nzp_profile={"nzp_profile","0",true};
void Android_Init(void){Cvar_RegisterVariable(&nzp_profile);}
int Android_Event(const SDL_Event *e){(void)e;return 0;}
int Android_Foreground(void){return 1;}
void Android_Frame(void){
    static int stage,ticks,benchmark_started;
    const int benchmark=getenv("NZP_SMOKE_BENCHMARK")!=NULL;
    extern int VID_StartRendererBenchmark(void);
    ++ticks;
    if(r_framecount>5){sv.paused=true;cl.paused=true;cl.time=cl.oldtime=1.0;}
    if(ticks>60&&!stage){const char *map=getenv("NZP_SMOKE_MAP");if(map)Cbuf_AddText(va("map %s\n",map));stage=1;}
    if(r_framecount==120){if(NZP_VK_Active())NZP_VK_RequestCapture("build/game-smoke/smoke-120.tga");else Sys_CaptureScreenshot();}
    if(r_framecount==240){if(NZP_VK_Active())NZP_VK_RequestCapture("build/game-smoke/smoke-240.tga");else Sys_CaptureScreenshot();}
    if(benchmark && !benchmark_started && r_framecount>=30)benchmark_started=VID_StartRendererBenchmark();
    static int reported;
    if(r_framecount>=241&&!reported){int base,lm,bi,li;R_WorldBatchStats(&base,&lm,&bi,&li);SDL_Log("NZP_SCENE_BATCHES base=%d lightmap=%d base_indices=%d lightmap_indices=%d",base,lm,bi,li);reported=1;}
    if(r_framecount>(benchmark?1000:300)||ticks>(benchmark?30000:3000)){if(NZP_VK_ValidationErrors())abort();Sys_Quit();}
}
void Android_DrawTouch(void){}
void Android_TouchMove(float *x,float *y){*x=*y=0;}
void Android_TouchLook(void){}
void Android_ResetTouch(void){}
int Android_ThermalStatus(void){return 0;}
int Android_FrameRate(void){return 0;}
int Android_ProfileEnabled(void){return 0;}
void Android_RecordFrame(double ms){(void)ms;}
void Android_RequestLANAddress(void){}
void Android_RequestLANName(void){}
