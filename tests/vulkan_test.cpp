// SPDX-License-Identifier: GPL-2.0-or-later
// Real Vulkan + validation layers. Mesa software GPU is a correctness target,
// never presented as A51 hardware or a mobile performance measurement.
#include <SDL.h>
#include "vulkan/renderer.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
static nzp_vk_uniform uniform(){nzp_vk_uniform u{};for(int i=0;i<4;i++)u.mvp[i*5]=u.mv[i*5]=u.tm[i*5]=1;u.params[2]=-1;return u;}
static nzp_vk_raster raster(){nzp_vk_raster s{};s.blend_src=0x302;s.blend_dst=0x303;s.depth_func=0x203;s.depth_far=1;s.depth_write=1;s.viewport[2]=64;s.viewport[3]=64;return s;}
static void quad(nzp_vk_uniform &u,nzp_vk_raster &s,unsigned tex,unsigned char r,unsigned char g,unsigned char b,unsigned char a=255){
    nzp_vk_vertex v[]={{{-1,-1,0},{0,0},{r,g,b,a}},{{1,-1,0},{1,0},{r,g,b,a}},{{1,1,0},{1,1},{r,g,b,a}},{{-1,-1,0},{0,0},{r,g,b,a}},{{1,1,0},{1,1},{r,g,b,a}},{{-1,1,0},{0,1},{r,g,b,a}}};
    NZP_VK_Draw(v,6,4,&u,&s,tex);
}
static void pixel(int x,int y,int r,int g,int b,int tolerance=2){unsigned char rgba[4]={};assert(NZP_VK_ReadPixels(x,y,1,1,rgba));
    if(abs(rgba[0]-r)>tolerance||abs(rgba[1]-g)>tolerance||abs(rgba[2]-b)>tolerance){fprintf(stderr,"pixel %d,%d: %u %u %u expected %d %d %d\n",x,y,rgba[0],rgba[1],rgba[2],r,g,b);abort();}}
static void end(){NZP_VK_RequestCapture(nullptr);NZP_VK_EndFrame();assert(NZP_VK_ValidationErrors()==0);}
int main(){
    if(SDL_Init(SDL_INIT_VIDEO)!=0){fprintf(stderr,"SDL init: %s\n",SDL_GetError());return 1;}SDL_Window *w=SDL_CreateWindow("Vulkan tests",0,0,64,64,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);if(!w){fprintf(stderr,"SDL window: %s\n",SDL_GetError());return 1;}
    assert(NZP_VK_Init(w,"build/host-tests",1));auto u=uniform();auto s=raster();
    assert(NZP_VK_BeginFrame(0,100));quad(u,s,0,255,0,0);end();pixel(32,32,255,0,0);
    // Identical repeated draws must reuse uniform data and GPU command state.
    assert(NZP_VK_BeginFrame(0,100));
    quad(u,s,0,255,0,0);quad(u,s,0,255,0,0);
    nzp_vk_command_stats cmd{};NZP_VK_GetCommandStats(&cmd);
    assert(cmd.draws==2 && cmd.pipeline_binds==1);
    assert(cmd.viewport_sets==1 && cmd.scissor_sets==1);
    assert(cmd.uniform_uploads==1 && cmd.uniform_reuses==1 && cmd.descriptor_binds==1);
    end();pixel(32,32,255,0,0);
    // A changed uniform must be rebound even though the material is unchanged.
    assert(NZP_VK_BeginFrame(0,100));
    auto shifted=u;shifted.mvp[12]=.1f;
    quad(u,s,0,255,0,0);quad(shifted,s,0,255,0,0);
    NZP_VK_GetCommandStats(&cmd);assert(cmd.uniform_uploads==2 && cmd.descriptor_binds==2);
    end();
    // Culling, depth conversion and original non-premultiplied blending.
    assert(NZP_VK_BeginFrame(0,100));quad(u,s,0,0,0,255);s.blend=1;quad(u,s,0,255,0,0,128);s.blend=0;end();pixel(32,32,128,0,127);
    // Fog uses eye-space depth, independently of projection.
    assert(NZP_VK_BeginFrame(0,100));u.fog[0]=1;u.fog[2]=1;u.fogcolor[0]=1;u.mv[14]=-.5f;quad(u,s,0,0,0,255);end();pixel(32,32,128,0,128);u=uniform();
    // Upload, subregion snapshot and an in-flight resource lifetime test.
    const unsigned char green[]={0,255,0,255},red[]={255,0,0,255};
    NZP_VK_Texture(5,0,0,0,1,1,0x1908,0x1401,green,1,1);
    NZP_VK_TextureFilter(5,0x2801,0x2600);NZP_VK_TextureFilter(5,0x2800,0x2600);u.params[0]=u.params[1]=1;
    assert(NZP_VK_BeginFrame(0,100));quad(u,s,5,255,255,255);NZP_VK_Texture(5,0,0,0,1,1,0x1908,0x1401,red,0,1);
    s.viewport[2]=32;quad(u,s,5,255,255,255);s.viewport[2]=64;end();pixel(16,32,255,0,0);pixel(48,32,0,255,0);
    // Dynamic texture changes, deletion, native HUD over voluntarily scaled 3D.
    assert(NZP_VK_BeginFrame(0,75));quad(u,s,5,255,255,255);NZP_VK_DeleteTexture(5);NZP_VK_BeginUI();u=uniform();s.viewport[2]=16;s.viewport[3]=16;
    quad(u,s,0,255,255,0);end();pixel(8,8,255,255,0);pixel(48,48,255,0,0);s=raster();
    // Persistent mesh buffers, indexed draw and zero-stride constant attributes.
    float positions[]={-1,-1,0,1,-1,0,1,1,0,-1,1,0};unsigned short indices[]={0,1,2,0,2,3};
    NZP_VK_Buffer(10,sizeof(positions),positions,1);NZP_VK_Buffer(11,sizeof(indices),indices,0);
    nzp_vk_array arrays[3]={{10,0,3,0x1406,0},{},{}};float uv[2]={};unsigned char blue[]={0,0,255,255};
    assert(NZP_VK_BeginFrame(0,100));assert(NZP_VK_DrawBuffers(arrays,11,0,6,4,&u,&s,0,uv,blue));NZP_VK_DeleteBuffer(10);NZP_VK_DeleteBuffer(11);end();pixel(32,32,0,0,255);
    // Combined lightmap must match the original two-pass framebuffer math,
    // including fog applied before lightmap multiplication.
    const unsigned char base[]={100,150,200,255},light[]={77,103,129,255};
    NZP_VK_Texture(21,0,0,0,1,1,0x1908,0x1401,base,1,1);
    NZP_VK_Texture(22,0,0,0,1,1,0x1908,0x1401,light,1,1);
    NZP_VK_TextureFilter(21,0x2801,0x2601);NZP_VK_TextureFilter(22,0x2801,0x2601);
    u=uniform();u.params[0]=u.params[1]=1;
    assert(NZP_VK_BeginFrame(0,100));quad(u,s,21,255,255,255);
    auto lighting=uniform();lighting.params[0]=lighting.params[1]=1;
    auto multiply=s;multiply.blend=1;multiply.blend_src=0x306;multiply.blend_dst=0x300;
    quad(lighting,multiply,22,255,255,255);end();unsigned char reference[64*64*4];assert(NZP_VK_ReadPixels(0,0,64,64,reference));
    NZP_VK_Buffer(30,sizeof(positions),positions,1);NZP_VK_Buffer(31,sizeof(indices),indices,0);
    float coordinates[]={0,0,1,0,1,1,0,1};NZP_VK_Buffer(32,sizeof(coordinates),coordinates,1);
    nzp_vk_array dual[3]={{30,0,3,0x1406,0},{32,0,2,0x1406,8},{}};
    nzp_vk_array lmuv={32,0,2,0x1406,8};unsigned char white[]={255,255,255,255};
    assert(NZP_VK_BeginFrame(0,100));assert(NZP_VK_DrawLightmappedBuffers(dual,&lmuv,31,0,6,&u,&s,21,22,uv,white));end();
    unsigned char combined[sizeof(reference)];assert(NZP_VK_ReadPixels(0,0,64,64,combined));
    for(size_t i=0;i<sizeof(reference);i++)assert(abs(int(combined[i])-int(reference[i]))<=1);
    // Regression: lightmapped static geometry must remain fogged even if
    // the lightmap is dark. Unlit fog color must not be multiplied away.
    u.fog[0]=1;u.fog[1]=0;u.fog[2]=1;u.fogcolor[0]=1;u.fogcolor[1]=0;u.fogcolor[2]=0;
    u.mv[14]=-.5f;
    assert(NZP_VK_BeginFrame(0,100));
    assert(NZP_VK_DrawLightmappedBuffers(dual,&lmuv,31,0,6,&u,&s,21,22,uv,white));
    end();pixel(32,32,158,61,101,4);
    NZP_VK_DeleteTexture(21);NZP_VK_DeleteTexture(22);NZP_VK_DeleteBuffer(30);NZP_VK_DeleteBuffer(31);NZP_VK_DeleteBuffer(32);u=uniform();
    // Offscreen rendering does not acquire/present swapchain images. Completion
    // counts advance only after the GPU fence, and the same pixels are retained.
    NZP_VK_SetBenchmark(1);auto completed=NZP_VK_CompletedFrames();
    for(int i=0;i<20;i++){assert(NZP_VK_BeginFrame(0,100));quad(u,s,0,0,255,0);if(i==19)end();else NZP_VK_EndFrame();}
    NZP_VK_Finish();assert(NZP_VK_CompletedFrames()>=completed+20);pixel(32,32,0,255,0);NZP_VK_SetBenchmark(0);
    // Recreate the SDL Vulkan surface while retaining textures, device and pipelines.
    NZP_VK_NotifySurfaceLost();assert(NZP_VK_BeginFrame(0,100));quad(u,s,0,0,255,0);end();pixel(32,32,0,255,0);
    // Stress two frame slots, vsync transition and voluntary resolution changes.
    for(int i=0;i<120;i++){assert(NZP_VK_BeginFrame(i<4?1:0,i%3==0?80:100));quad(u,s,0,0,255,0);NZP_VK_EndFrame();}
    assert(NZP_VK_ValidationErrors()==0);NZP_VK_Shutdown();SDL_DestroyWindow(w);
    // Pipeline cache reload and complete cleanup/reinitialization.
    w=SDL_CreateWindow("Vulkan reload",0,0,64,64,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);if(!w){fprintf(stderr,"SDL window: %s\n",SDL_GetError());return 1;}assert(NZP_VK_Init(w,"build/host-tests",1));
    assert(NZP_VK_BeginFrame(0,100));quad(u,s,0,0,255,0);end();pixel(32,32,0,255,0);
    NZP_VK_Shutdown();SDL_DestroyWindow(w);SDL_Quit();puts("Vulkan rendering, pixels, lifetimes, cache reload and validation passed");
}
