// SPDX-License-Identifier: GPL-2.0-or-later
// Pre-rotation end to end: the same scene rendered into a 0/90/180/270 degree
// pre-rotated target must read back identically in logical orientation. Real
// rotated surfaces do not exist on Mesa software Vulkan, so the offscreen
// benchmark target is used; it exercises the same vertex shader, viewport,
// scissor, clear and readback code paths as a rotated swapchain.
#include <SDL.h>
#include "vulkan/renderer.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
static nzp_vk_uniform uniform(){nzp_vk_uniform u{};for(int i=0;i<4;i++)u.mvp[i*5]=u.mv[i*5]=u.tm[i*5]=1;u.params[2]=-1;return u;}
static void quad(const nzp_vk_uniform &u,const nzp_vk_raster &s,float x0,float y0,float x1,float y1,unsigned char r,unsigned char g,unsigned char b){
    nzp_vk_vertex v[]={{{x0,y0,0},{0,0},{r,g,b,255}},{{x1,y0,0},{1,0},{r,g,b,255}},{{x1,y1,0},{1,1},{r,g,b,255}},
        {{x0,y0,0},{0,0},{r,g,b,255}},{{x1,y1,0},{1,1},{r,g,b,255}},{{x0,y1,0},{0,1},{r,g,b,255}}};
    NZP_VK_Draw(v,6,4,&u,&s,0);
}
static std::vector<unsigned char> render(int rotation,int percent,int W,int H){
    NZP_VK_SetTestRotation(rotation);
    assert(NZP_VK_BeginFrame(0,percent));
    int w,h;NZP_VK_GetSize(&w,&h);assert(w==W&&h==H); // logical size never swaps
    assert(NZP_VK_GetRotation()==(rotation<0?0:rotation*90));
    auto u=uniform();nzp_vk_raster s{};s.blend_src=1;s.blend_dst=0;s.depth_func=0x203;s.depth_far=1;s.depth_write=1;
    s.viewport[2]=W;s.viewport[3]=H;
    quad(u,s,-1,-1,1,1,10,20,30);
    // Asymmetric content in the full viewport: red top-left, green bottom-right.
    quad(u,s,-1,.5f,-.5f,1,255,0,0);      // GL y-up: top-left of the screen
    quad(u,s,.25f,-1,1,-.25f,0,255,0);    // bottom-right
    // Partial viewport (e.g. letterboxed 3D view) in GL bottom-left coordinates.
    s.viewport[0]=8;s.viewport[1]=6;s.viewport[2]=40;s.viewport[3]=24;
    quad(u,s,-1,-1,1,1,0,0,255);
    quad(u,s,-1,-1,-.5f,-.5f,255,255,0);
    s.viewport[0]=0;s.viewport[1]=0;s.viewport[2]=W;s.viewport[3]=H;
    NZP_VK_BeginUI(); // with percent<100 this draws the rotated upscale quad
    u=uniform();s.viewport[0]=W-16;s.viewport[1]=H-10;s.viewport[2]=16;s.viewport[3]=10;
    quad(u,s,-1,-1,1,1,255,0,255); // native HUD patch, GL top-right
    NZP_VK_RequestCapture(nullptr);NZP_VK_EndFrame();
    assert(NZP_VK_ValidationErrors()==0);
    std::vector<unsigned char> px(size_t(W)*H*4);assert(NZP_VK_ReadPixels(0,0,W,H,px.data()));return px;
}
int main(){
    const int W=96,H=64;
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    SDL_Window *win=SDL_CreateWindow("rot",0,0,W,H,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);if(!win)return 1;
    assert(NZP_VK_Init(win,"build/host-tests",1));NZP_VK_SetBenchmark(1);
    for(int percent:{100,70}){
        auto ref=render(-1,percent,W,H);
        // Sanity: the scene is not trivially uniform.
        auto at=[&](const std::vector<unsigned char>&p,int x,int y,int c){return p[(size_t(y)*W+x)*4+c];};
        assert(at(ref,4,H-4,0)==255||percent<100);          // red, GL top-left
        assert(at(ref,W-2,H-2,0)==255&&at(ref,W-2,H-2,2)==255); // magenta HUD top-right
        for(int rotation=1;rotation<4;rotation++){
            auto got=render(rotation,percent,W,H);int bad=0;
            for(size_t i=0;i<ref.size();i++)if(abs(int(ref[i])-int(got[i]))>2)bad++;
            if(bad){fprintf(stderr,"rotation %d percent %d differs in %d bytes\n",rotation*90,percent,bad);return 1;}
        }
    }
    NZP_VK_Shutdown();SDL_DestroyWindow(win);SDL_Quit();
    puts("Vulkan pre-rotation (90/180/270, partial viewports, scaled world + HUD) passed");
}
