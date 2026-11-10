// SPDX-License-Identifier: GPL-2.0-or-later
#include "fsr.h"
#include <SDL.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef NZP_FSR
#include <cstddef>
#include <cwchar>
#include <host/ffxm_fsr2.h>
#include <host/backends/vk/ffxm_vk.h>
namespace nzp_fsr {
using namespace arm;
struct Context {
    FfxmFsr2Context fsr{};
    std::vector<unsigned char> scratch;
    VkDeviceContext device_context{};
    bool created{};
    int display_w{},display_h{};
};
static void message(FfxmMsgType type,const wchar_t *text){
    SDL_Log("NZP_FSR %s: %ls",type==FFXM_MESSAGE_TYPE_ERROR?"error":"warning",text);
}
bool available(){return true;}
Context *create(VkPhysicalDevice physical,VkDevice device,int rw,int rh,int dw,int dh,int quality){
    auto *c=new Context;
    c->device_context={device,physical,vkGetDeviceProcAddr};
    const size_t bytes=ffxmGetScratchMemorySizeVK(physical,FFXM_FSR2_CONTEXT_COUNT);
    c->scratch.resize(bytes);
    FfxmFsr2ContextDescription d{};
    d.qualityMode=(FfxmFsr2ShaderQualityMode)quality;
    // Standard (non-inverted, finite) depth, fixed exposure. The HDR input path is used even for our
    // 8-bit content: it is the configuration validated under the Vulkan validation layers, while the
    // LDR permutation produced dark, grid-patterned output there.
    d.flags=FFXM_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;
    d.maxRenderSize={(uint32_t)rw,(uint32_t)rh};d.displaySize={(uint32_t)dw,(uint32_t)dh};
    d.fpMessage=message;
    FfxmDevice dev=ffxmGetDeviceVK(&c->device_context);
    const FfxmErrorCode interface_result=ffxmGetInterfaceVK(&d.backendInterface,dev,c->scratch.data(),bytes,FFXM_FSR2_CONTEXT_COUNT);
    const FfxmErrorCode create_result=interface_result==FFXM_OK?ffxmFsr2ContextCreate(&c->fsr,&d):interface_result;
    if(interface_result!=FFXM_OK||create_result!=FFXM_OK){
        SDL_Log("NZP_FSR context creation failed: interface=%d create=%d (scratch %zu bytes)",(int)interface_result,(int)create_result,bytes);
        delete c;return nullptr;
    }
    c->created=true;c->display_w=dw;c->display_h=dh;
    SDL_Log("NZP_FSR Arm ASR %d.%d.%d render=%dx%d display=%dx%d quality=%d",FFXM_FSR2_VERSION_MAJOR,
            FFXM_FSR2_VERSION_MINOR,FFXM_FSR2_VERSION_PATCH,rw,rh,dw,dh,quality);
    return c;
}
void destroy(Context *c){
    if(!c)return;
    if(c->created)ffxmFsr2ContextDestroy(&c->fsr);
    delete c;
}
int phase_count(int rw,int dw){return ffxmFsr2GetJitterPhaseCount(rw,dw);}
void jitter(int index,int count,float *x,float *y){ffxmFsr2GetJitterOffset(x,y,index,count);}
static FfxmResource image(VkImage handle,FfxmSurfaceFormat format,int w,int h,FfxmResourceUsage usage,FfxmResourceStates state){
    FfxmResourceDescription d{};
    d.type=FFXM_RESOURCE_TYPE_TEXTURE2D;d.format=format;d.width=w;d.height=h;d.depth=1;d.mipCount=1;d.flags=FFXM_RESOURCE_FLAGS_NONE;d.usage=usage;
    return ffxmGetResourceVK((void*)handle,d,nullptr,state);
}
bool dispatch(Context *c,const Inputs &in){
    FfxmFsr2DispatchDescription d{};
    d.commandList=ffxmGetCommandListVK(in.cmd);
    d.color=image(in.color,FFXM_SURFACE_FORMAT_R8G8B8A8_UNORM,in.render_w,in.render_h,FFXM_RESOURCE_USAGE_READ_ONLY,FFXM_RESOURCE_STATE_COMPUTE_READ);
    d.depth=image(in.depth,FFXM_SURFACE_FORMAT_R32_FLOAT,in.render_w,in.render_h,FFXM_RESOURCE_USAGE_DEPTHTARGET,FFXM_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors=image(in.motion,FFXM_SURFACE_FORMAT_R16G16_FLOAT,in.render_w,in.render_h,FFXM_RESOURCE_USAGE_READ_ONLY,FFXM_RESOURCE_STATE_COMPUTE_READ);
    d.output=image(in.output,FFXM_SURFACE_FORMAT_R8G8B8A8_UNORM,c->display_w,c->display_h,FFXM_RESOURCE_USAGE_RENDERTARGET,FFXM_RESOURCE_STATE_PIXEL_WRITE);
    d.output.description.width=c->display_w;d.output.description.height=c->display_h;
    d.jitterOffset={in.jitter_x,in.jitter_y};
    d.motionVectorScale={1.f,1.f}; // vectors are already in render-resolution pixels
    d.renderSize={(uint32_t)in.render_w,(uint32_t)in.render_h};
    d.enableSharpening=in.sharpness>0.f;d.sharpness=in.sharpness;
    d.frameTimeDelta=in.frame_time_ms;d.preExposure=1.f;d.reset=in.reset;
    d.cameraNear=in.near_plane;d.cameraFar=in.far_plane;d.cameraFovAngleVertical=in.fov_y;
    return ffxmFsr2ContextDispatch(&c->fsr,&d)==FFXM_OK;
}
}
#else
namespace nzp_fsr {
struct Context{};
bool available(){return false;}
Context *create(VkPhysicalDevice,VkDevice,int,int,int,int,int){return nullptr;}
void destroy(Context *){}
int phase_count(int,int){return 1;}
void jitter(int,int,float *x,float *y){*x=*y=0;}
bool dispatch(Context *,const Inputs &){return false;}
}
#endif
