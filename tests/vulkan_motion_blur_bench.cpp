// SPDX-License-Identifier: GPL-2.0-or-later
// Relative cost of the motion blur presets on a SOFTWARE Vulkan device (lavapipe). This is NOT a Mali measurement: a CPU
// rasteriser has a different bottleneck than a tile-based mobile GPU. Use it only to compare presets with each other and to
// see the extra memory and CPU command overhead; real GPU numbers need the device (renderer-benchmark.csv, "Offscreen benchmark").
#include <SDL.h>
#include "vulkan/renderer.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>
static const int W=960,H=540;
static nzp_vk_uniform uniform(float tx){nzp_vk_uniform u{};for(int i=0;i<4;i++)u.mvp[i*5]=u.mv[i*5]=u.tm[i*5]=1;u.params[0]=u.params[1]=1;u.params[2]=-1;u.mvp[12]=tx;return u;}
static double cpu_ms(){timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static double wall_ms(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
int main(){
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    SDL_Window *win=SDL_CreateWindow("mbb",0,0,W,H,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);if(!win)return 1;
    if(!NZP_VK_Init(win,"build/host-tests",0))return 1;NZP_VK_SetBenchmark(1);
    std::vector<unsigned char> tex(256*256*4);for(int y=0;y<256;y++)for(int x=0;x<256;x++){int v=((x/8+y/8)%2)?200:60;auto*q=&tex[(y*256+x)*4];q[0]=v;q[1]=v;q[2]=v;q[3]=255;}
    NZP_VK_Texture(1,0,0,0,256,256,0x1908,0x1401,tex.data(),1,4);
    NZP_VK_TextureFilter(1,0x2801,0x2601);NZP_VK_TextureFilter(1,0x2800,0x2601);
    nzp_vk_raster s{};s.blend_src=1;s.blend_dst=0;s.depth_func=0x203;s.depth_far=1;s.depth_write=1;s.viewport[2]=W;s.viewport[3]=H;
    const char *names[]={"OFF","LOW","MEDIUM","HIGH","ULTRA"};
    printf("%-7s %9s %9s %12s %s\n","preset","wall ms","cpu ms","extra MiB","(960x540, software Vulkan, camera panning fast)\n");
    for(int mode=0;mode<5;mode++){
        nzp_mblur_params p;nzp_mblur_resolve(&p,mode,0,0,0,0,0,100);NZP_VK_SetMotionBlur(&p);NZP_VK_SetMotionBlurTestFrameTime(16.6f);NZP_VK_ResetHistory();
        std::vector<double> wall,cpu;
        for(int t=0;t<46;t++){
            const double w0=wall_ms(),c0=cpu_ms();
            if(!NZP_VK_BeginFrame(0,100))return 1;NZP_VK_SetCamera(4,4096,1.2f);
            const float step=0.1f;auto u=uniform(t*step);auto pu=uniform(t*step-step);std::copy(pu.mvp,pu.mvp+16,u.prev_mvp);
            const float h[9]={1,0,-step,0,1,0,0,0,1};NZP_VK_SetReprojection(h);
            const unsigned char w4[4]={255,255,255,255};const float a=-3.f,b=3.f;
            nzp_vk_vertex v[]={{{a,a,0},{0,0},{w4[0],w4[1],w4[2],w4[3]}},{{b,a,0},{1,0},{w4[0],w4[1],w4[2],w4[3]}},{{b,b,0},{1,1},{w4[0],w4[1],w4[2],w4[3]}},{{a,a,0},{0,0},{w4[0],w4[1],w4[2],w4[3]}},{{b,b,0},{1,1},{w4[0],w4[1],w4[2],w4[3]}},{{a,b,0},{0,1},{w4[0],w4[1],w4[2],w4[3]}}};
            NZP_VK_Draw(v,6,4,&u,&s,1);
            NZP_VK_EndFrame();NZP_VK_Finish();
            if(t>=6){wall.push_back(wall_ms()-w0);cpu.push_back(cpu_ms()-c0);}
        }
        std::sort(wall.begin(),wall.end());std::sort(cpu.begin(),cpu.end());
        printf("%-7s %9.2f %9.2f %12.2f\n",names[mode],wall[wall.size()/2],cpu[cpu.size()/2],NZP_VK_MotionBlurBytes()/1048576.0);
    }
    NZP_VK_Shutdown();SDL_DestroyWindow(win);SDL_Quit();return 0;
}
