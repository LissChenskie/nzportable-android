// SPDX-License-Identifier: GPL-2.0-or-later
// Temporal upscaling (Arm ASR, FSR 2.2 class) end to end on a software Vulkan device.
// Checks: it activates without validation errors, it reconstructs detail that a
// bilinear upscale cannot, and the motion-vector/jitter conventions are right
// (correct vectors must beat deliberately inverted ones while the view pans).
#include <SDL.h>
#include "vulkan/renderer.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
static const int W=256,H=192;
static nzp_vk_uniform uniform(float tx){nzp_vk_uniform u{};for(int i=0;i<4;i++)u.mvp[i*5]=u.mv[i*5]=u.tm[i*5]=1;u.params[0]=u.params[1]=1;u.params[2]=-1;u.mvp[12]=tx;return u;}
static void quad(const nzp_vk_uniform &u,const nzp_vk_raster &s){
    const unsigned char w[4]={255,255,255,255};const float a=-1.6f,b=1.6f;
    nzp_vk_vertex v[]={{{a,a,0},{0,0},{w[0],w[1],w[2],w[3]}},{{b,a,0},{1,0},{w[0],w[1],w[2],w[3]}},{{b,b,0},{1,1},{w[0],w[1],w[2],w[3]}},
        {{a,a,0},{0,0},{w[0],w[1],w[2],w[3]}},{{b,b,0},{1,1},{w[0],w[1],w[2],w[3]}},{{a,b,0},{0,1},{w[0],w[1],w[2],w[3]}}};
    NZP_VK_Draw(v,6,4,&u,&s,1);
}
// mode: -1 percent 100 (truth), 0 bilinear at percent, 1 FSR. tx(t): pan position at frame t.
static std::vector<unsigned char> run(int percent,int mode,int frames,float step,int history_sign){
    NZP_VK_SetUpscaler(mode);NZP_VK_ResetHistory();
    nzp_vk_raster s{};s.blend_src=1;s.blend_dst=0;s.depth_func=0x203;s.depth_far=1;s.depth_write=1;s.viewport[2]=W;s.viewport[3]=H;
    std::vector<unsigned char> px(size_t(W)*H*4);
    for(int t=0;t<frames;t++){
        assert(NZP_VK_BeginFrame(0,percent));
        if(mode==1)assert(NZP_VK_FSRActive()==(percent<100));
        if(mode==2)assert(NZP_VK_UpscalerState()==(percent<100?1:4));
        NZP_VK_SetCamera(4,4096,1.2f);
        auto u=uniform(t*step);
        // prev_mvp: where the same geometry was last frame. history_sign=-1 inverts it on purpose.
        auto prev=uniform(t*step-history_sign*step);for(int i=0;i<16;i++)u.prev_mvp[i]=prev.mvp[i];
        {   /* Lite fast: previous position of the same content (NDC x moved by step each frame). */
            const float h[9]={1,0,-history_sign*step,0,1,0,0,0,1};NZP_VK_SetReprojection(h);
        }
        quad(u,s);
        if(t==frames-1)NZP_VK_RequestCapture(nullptr);
        NZP_VK_EndFrame();
        assert(NZP_VK_ValidationErrors()==0);
    }
    assert(NZP_VK_ReadPixels(0,0,W,H,px.data()));return px;
}
static void dump(const char *name,const std::vector<unsigned char> &px){
    if(!SDL_getenv("NZP_FSR_DUMP"))return;
    char path[128];snprintf(path,sizeof(path),"/tmp/fsr_%s.ppm",name);FILE *f=fopen(path,"wb");if(!f)return;
    fprintf(f,"P6\n%d %d\n255\n",W,H);for(int i=0;i<W*H;i++)fwrite(&px[size_t(i)*4],1,3,f);fclose(f);
}
// Error over "detail" pixels only: where the reference differs from its 5x5 local mean (thin lines, edges).
// A smooth gradient is reproduced well by any upscaler; temporal reconstruction matters for detail.
static double detail_mae(const std::vector<unsigned char> &a,const std::vector<unsigned char> &ref){
    double sum=0;int n=0;
    for(int y=8;y<H-8;y++)for(int x=8;x<W-8;x++){
        double mean=0;for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)mean+=ref[(size_t(y+dy)*W+x+dx)*4+1];
        mean/=25;
        if(std::abs(ref[(size_t(y)*W+x)*4+1]-mean)<30)continue;
        for(int c=0;c<3;c++){sum+=std::abs(int(a[(size_t(y)*W+x)*4+c])-int(ref[(size_t(y)*W+x)*4+c]));n++;}
    }
    return n?sum/n:0;
}
static double mae(const std::vector<unsigned char> &a,const std::vector<unsigned char> &b){return detail_mae(a,b);}
int main(){
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    SDL_Window *win=SDL_CreateWindow("fsr",0,0,W,H,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);if(!win)return 1;
    assert(NZP_VK_Init(win,"build/host-tests",1));NZP_VK_SetBenchmark(1);
    // 256x192 test card: smooth gradient, 8-texel checker, and 1-texel lines that are thinner than a
    // pixel of the 50% render. Linear filtering, as for most non-pixel-art content.
    std::vector<unsigned char> tex(size_t(W)*H*4);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){
        int v=40+x+y/2;                                  /* gradient */
        if((x/8+y/8)%2)v+=70;                            /* checker */
        if((x+2*y)%17==0||(2*x-y+200)%23==0)v=250;       /* thin lines */
        v=std::min(255,std::max(0,v));
        auto *q=&tex[(size_t(y)*W+x)*4];q[0]=v;q[1]=std::min(255,v+20);q[2]=v/2+30;q[3]=255;
    }
    NZP_VK_Texture(1,0,0,0,W,H,0x1908,0x1401,tex.data(),1,4);
    NZP_VK_TextureFilter(1,0x2801,0x2601);NZP_VK_TextureFilter(1,0x2800,0x2601);
    NZP_VK_TextureFilter(1,0x2802,0x812F);NZP_VK_TextureFilter(1,0x2803,0x812F);
    if(!SDL_getenv("NZP_EXPECT_FSR")){ /* library not built in: only the bilinear path exists */ }
    // Static scene.
    auto truth=run(100,false,1,0.f,1);
    auto bilinear=run(50,false,1,0.f,1);
    auto upscaled=run(50,true,24,0.f,1);
    dump("truth",truth);dump("bilinear",bilinear);dump("fsr",upscaled);
    const int fsr_on=NZP_VK_FSRActive();
    double e_bi=mae(bilinear,truth),e_fsr=mae(upscaled,truth);
    printf("static: bilinear MAE %.2f, FSR MAE %.2f (active=%d)\n",e_bi,e_fsr,fsr_on);
    if(!fsr_on){puts("FSR not compiled in; temporal upscaling checks skipped");NZP_VK_Shutdown();SDL_DestroyWindow(win);SDL_Quit();return 0;}
    // A swapchain rebuild (here forced via the pre-rotation setting) must not rebuild the upscaler:
    // creating it compiles dozens of pipelines, and doing it every frame made the game run at ~1 fps.
    {
        const int before=NZP_VK_FSRContextsCreated();assert(before==1);
        for(int i=0;i<3;i++){NZP_VK_SetPreRotation(i&1);(void)run(50,true,2,0.f,1);assert(NZP_VK_FSRActive());}
        assert(NZP_VK_FSRContextsCreated()==before);
        assert(NZP_VK_FSRState()==1);
        NZP_VK_SetUpscaler(0);assert(NZP_VK_BeginFrame(0,50));assert(NZP_VK_UpscalerState()==0&&!NZP_VK_FSRActive());NZP_VK_EndFrame();
        NZP_VK_SetUpscaler(1);
    }
    assert(e_fsr<e_bi*1.4); /* sanity: not broken (detail this thin is not recoverable by any 2x upscaler) */
    {   /* The outermost pixels must not be darker than their neighbours (kernel edge energy loss). */
        double edge=0,inner=0;for(int x=0;x<W;x++)for(int c=0;c<3;c++){edge+=upscaled[(size_t(0)*W+x)*4+c];inner+=upscaled[(size_t(4)*W+x)*4+c];}
        printf("border row mean ratio %.2f\n",edge/inner);assert(edge/inner>0.9);
    }
    // Panning view: 0.9 render-pixels per frame.
    const float step=2.f*0.9f/(W/2);const int frames=24;
    auto truth_pan=run(100,false,frames,step,1);
    auto good=run(50,true,frames,step,1);
    auto bad=run(50,true,frames,step,-1);
    double e_good=mae(good,truth_pan),e_bad=mae(bad,truth_pan),e_bi_pan=mae(run(50,false,frames,step,1),truth_pan);
    printf("panning: bilinear MAE %.2f, FSR correct vectors %.2f, FSR inverted vectors %.2f\n",e_bi_pan,e_good,e_bad);
    assert(e_good<e_bad*0.9);   /* motion vector sign is right */
    assert(e_good<e_bi_pan*1.4);
    assert(NZP_VK_ValidationErrors()==0);
    {   // FSR Lite: same scenes.
        auto lite_static=run(50,2,24,0.f,1);dump("lite",lite_static);
        const double e_lite=mae(lite_static,truth);
        auto lite_good=run(50,2,frames,step,1),lite_bad=run(50,2,frames,step,-1);
        const double l_good=mae(lite_good,truth_pan),l_bad=mae(lite_bad,truth_pan);
        printf("lite: static MAE %.2f (bilinear %.2f); panning correct %.2f inverted %.2f (bilinear %.2f)\n",e_lite,e_bi,l_good,l_bad,e_bi_pan);
        assert(e_lite<e_bi);         /* reconstructs more than bilinear on a static view */
        assert(l_good<l_bad*0.9);    /* motion-vector and jitter conventions are right */
        assert(l_good<e_bi_pan*1.1); /* and it does not lose to bilinear while panning */
        {   double edge=0,inner=0;for(int x=0;x<W;x++)for(int c=0;c<3;c++){edge+=lite_static[size_t(x)*4+c];inner+=lite_static[(size_t(4)*W+x)*4+c];}
            assert(edge/inner>0.9);  /* no dark border */
        }
        {   const int created=NZP_VK_FSRContextsCreated();
            for(int i=0;i<3;i++){NZP_VK_SetPreRotation(i&1);(void)run(50,2,3,0.f,1);}
            assert(NZP_VK_FSRContextsCreated()==created); /* Lite never creates an Arm ASR context */
            NZP_VK_SetUpscaler(2);assert(NZP_VK_BeginFrame(0,100));assert(NZP_VK_UpscalerState()==4);NZP_VK_EndFrame(); /* at 100% nothing runs */
        }
        assert(NZP_VK_ValidationErrors()==0);
    }
    {   // FSR Lite fast variant (what ARM Mali devices use by default): 2 fetches, rotation-only reprojection.
        NZP_VK_SetLiteQuality(1);
        auto fast_static=run(50,2,24,0.f,1);dump("litefast",fast_static);
        const double e_fast=mae(fast_static,truth);
        auto fast_good=run(50,2,frames,step,1),fast_bad=run(50,2,frames,step,-1);
        const double f_good=mae(fast_good,truth_pan),f_bad=mae(fast_bad,truth_pan);
        printf("lite fast: static MAE %.2f (bilinear %.2f); panning correct %.2f inverted %.2f (bilinear %.2f)\n",e_fast,e_bi,f_good,f_bad,e_bi_pan);
        assert(e_fast<e_bi);              /* still reconstructs more than bilinear on a static view */
        assert(f_good<f_bad*0.95);        /* reprojection sign is right */
        assert(f_good<e_bi_pan*1.1);      /* and it does not lose to bilinear while panning */
        {   double edge=0,inner=0;for(int x=0;x<W;x++)for(int c=0;c<3;c++){edge+=fast_static[size_t(x)*4+c];inner+=fast_static[(size_t(4)*W+x)*4+c];}
            assert(edge/inner>0.9);
        }
        assert(NZP_VK_ValidationErrors()==0);
        NZP_VK_SetLiteQuality(0);
    }
    NZP_VK_Shutdown();SDL_DestroyWindow(win);SDL_Quit();
    puts("Vulkan temporal upscaling (Arm ASR) jitter, motion vectors and reconstruction passed");
}
