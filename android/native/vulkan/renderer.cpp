// SPDX-License-Identifier: GPL-2.0-or-later
// Native Vulkan 1.1 backend for Vril's shared geometry/material frontend.
#include "renderer.h"
#include "vendor/volk.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vendor/vk_mem_alloc.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <SDL.h>
#include <SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <limits>
#include "vulkan_shaders.h"

namespace {
constexpr unsigned Frames=2;
constexpr size_t ArenaBytes=16*1024*1024;
struct Buffer {
    VkBuffer handle{}; VmaAllocation allocation{}; void *mapped{}; size_t size{};
};
struct Image {
    VkImage handle{}; VmaAllocation allocation{}; VkImageView view{};
    int width{},height{};unsigned levels{};
};
struct TextureGPU { std::shared_ptr<Image> image; VkDescriptorSet descriptor{}; VkSampler sampler{}; };
struct Texture {
    struct Mip {int w{},h{};std::vector<unsigned char> rgba;};
    std::vector<Mip> mips;
    int min=0x2702,mag=0x2601,wrap_s=0x2901,wrap_t=0x2901;
    bool dirty=true,pixel_dirty=true; std::shared_ptr<TextureGPU> gpu;
    std::vector<std::shared_ptr<Image>> image_pool;
};
struct Mesh {std::shared_ptr<Buffer> gpu;std::vector<std::shared_ptr<Buffer>> pool;};
struct Frame {
    VkCommandPool pool{}; VkCommandBuffer upload{},draw{};
    VkFence fence{}; VkSemaphore acquired{};
    Buffer arena; size_t used{};
    std::vector<Buffer> staging;std::vector<size_t> staging_used;unsigned staging_index{};
    VkDescriptorSet uniform_set{};
    std::vector<std::shared_ptr<TextureGPU>> textures;
    std::vector<std::shared_ptr<Buffer>> meshes;
    Image world,world_depth;VkFramebuffer world_fb{};
    Image benchmark_color,benchmark_depth;VkFramebuffer benchmark_fb{};
    VkDescriptorSet world_set{};
    VkQueryPool queries{}; bool submitted{};
};
struct SwapImage {VkImage image{};VkImageView view{}; Image depth; VkFramebuffer fb{};VkSemaphore complete{};};
struct Layout {
    uint32_t stride[4]{};VkFormat format[4]{};
    bool operator<(const Layout &b)const{return std::memcmp(this,&b,sizeof(*this))<0;}
};
struct PipelineKey {
    Layout layout;unsigned mode{},blend_src{},blend_dst{},depth_func{},cull_face{};
    int blend{},depth{},depth_write{},cull{},offset{},lightmapped{};
    bool operator<(const PipelineKey &b)const{return std::memcmp(this,&b,sizeof(*this))<0;}
};
VkInstance instance{}; VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};
VkSurfaceKHR surface{};VkSwapchainKHR swapchain{};VmaAllocator allocator{};
VkPhysicalDeviceProperties properties{};
VkDescriptorPool descriptors{}; VkDescriptorSetLayout uniform_layout{},texture_layout{};
VkPipelineLayout pipeline_layout{};VkPipelineCache pipeline_cache{};
VkShaderModule vertex_shader{},fragment_shader{};
VkRenderPass native_pass{},world_pass{};
VkDebugUtilsMessengerEXT debug{};
std::map<PipelineKey,VkPipeline> pipelines;
std::map<unsigned,Texture> textures;std::map<unsigned,Mesh> meshes;
std::array<Frame,Frames> frames;
std::vector<SwapImage> images;
SDL_Window *window{};std::string cache_path;
unsigned family{},frame_index{},image_index{},validation_errors{};
int width{},height{},world_width{},world_height{},current_vsync=-1,scale=100;
bool active{},recording{},in_pass{},ui{},resize_pending{},capture_requested{},can_capture{},pass_has_draw{};
float pass_clear_color[4],pass_clear_depth;
Buffer readback;std::vector<unsigned char> captured;std::string screenshot_path;
double gpu_ms=-1;bool benchmark{},requested_benchmark{},surface_lost{};unsigned long long completed_frames{};unsigned timestamp_bits=64;
float clear_color[4]={16.f/255,32.f/255,64.f/255,1};float clear_depth=1;
VkFormat color_format{},depth_format{};
Frame &frame(){return frames[frame_index];}
void check(VkResult r,const char *what){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+": "+std::to_string(r));}
#define VK_CHECK(call) check(call,#call)
VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
 VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT *data,void*) {
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++validation_errors;
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,"Vulkan validation: %s",data->pMessage);return VK_FALSE;
}
void destroy(Buffer &b){if(b.handle)vmaDestroyBuffer(allocator,b.handle,b.allocation);b={};}
void destroy(Image &i){if(i.view)vkDestroyImageView(device,i.view,nullptr);if(i.handle)vmaDestroyImage(allocator,i.handle,i.allocation);i={};}
Buffer make_buffer(size_t bytes,VkBufferUsageFlags usage) {
    Buffer b;b.size=bytes;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage;
    VmaAllocationCreateInfo ai{};ai.usage=VMA_MEMORY_USAGE_AUTO;
    ai.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};VK_CHECK(vmaCreateBuffer(allocator,&ci,&ai,&b.handle,&b.allocation,&info));
    b.mapped=info.pMappedData;return b;
}
Image make_image(int w,int h,unsigned levels,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect) {
    Image i;i.width=w;i.height=h;i.levels=levels;VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;
    ci.format=format;ci.extent={(unsigned)w,(unsigned)h,1};ci.mipLevels=levels;ci.arrayLayers=1;
    ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=usage;
    VmaAllocationCreateInfo ai{};ai.usage=VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if(usage&VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)ai.preferredFlags=VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    VK_CHECK(vmaCreateImage(allocator,&ci,&ai,&i.handle,&i.allocation,nullptr));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=i.handle;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
    vi.format=format;vi.subresourceRange={aspect,0,levels,0,1};
    VK_CHECK(vkCreateImageView(device,&vi,nullptr,&i.view));return i;
}
VkDescriptorSet descriptor(VkDescriptorSetLayout layout){
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=descriptors;
    ai.descriptorSetCount=1;ai.pSetLayouts=&layout;VkDescriptorSet set;VK_CHECK(vkAllocateDescriptorSets(device,&ai,&set));return set;
}
size_t allocate(size_t bytes,size_t alignment) {
    auto &f=frame();size_t offset=(f.used+alignment-1)/alignment*alignment;
    if(offset+bytes>f.arena.size)throw std::runtime_error("Vulkan frame arena exhausted; no geometry silently dropped");
    f.used=offset+bytes;return offset;
}
std::pair<VkBuffer,size_t> stage(const void *data,size_t bytes){
    auto &f=frame();
    while(f.staging_index<f.staging.size()){
        size_t off=(f.staging_used[f.staging_index]+3)&~size_t(3);
        if(off+bytes<=f.staging[f.staging_index].size){
            std::memcpy((char*)f.staging[f.staging_index].mapped+off,data,bytes);
            f.staging_used[f.staging_index]=off+bytes;return {f.staging[f.staging_index].handle,off};
        }
        ++f.staging_index;
    }
    f.staging.push_back(make_buffer(std::max<size_t>(4*1024*1024,bytes),VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    f.staging_used.push_back(bytes);std::memcpy(f.staging.back().mapped,data,bytes);return {f.staging.back().handle,0};
}
void barrier(VkCommandBuffer cb,VkImage image,VkImageLayout old,VkImageLayout next,
 VkAccessFlags source,VkAccessFlags dest,VkPipelineStageFlags srcstage,VkPipelineStageFlags dststage,unsigned levels=1){
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=source;b.dstAccessMask=dest;
    b.oldLayout=old;b.newLayout=next;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,1};
    vkCmdPipelineBarrier(cb,srcstage,dststage,0,0,nullptr,0,nullptr,1,&b);
}
VkRenderPass make_pass(bool offscreen) {
    VkAttachmentDescription a[2]{};a[0].format=color_format;a[0].samples=VK_SAMPLE_COUNT_1_BIT;
    a[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    a[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    a[0].finalLayout=offscreen?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    a[1].format=depth_format;a[1].samples=VK_SAMPLE_COUNT_1_BIT;a[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    a[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    a[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;
    sub.pColorAttachments=&color;sub.pDepthStencilAttachment=&depth;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;
    deps[0].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask=deps[0].srcStageMask;
    deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ci.attachmentCount=2;ci.pAttachments=a;
    ci.subpassCount=1;ci.pSubpasses=&sub;ci.dependencyCount=2;ci.pDependencies=deps;
    VkRenderPass p;VK_CHECK(vkCreateRenderPass(device,&ci,nullptr,&p));return p;
}
VkFramebuffer framebuffer(VkImageView color,VkImageView depth,int w,int h,VkRenderPass pass){
    VkImageView views[]={color,depth};VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass=pass;ci.attachmentCount=2;ci.pAttachments=views;ci.width=w;ci.height=h;ci.layers=1;
    VkFramebuffer fb;VK_CHECK(vkCreateFramebuffer(device,&ci,nullptr,&fb));return fb;
}
void destroy_targets(){
    for(auto &f:frames){if(f.benchmark_fb)vkDestroyFramebuffer(device,f.benchmark_fb,nullptr);f.benchmark_fb={};destroy(f.benchmark_color);destroy(f.benchmark_depth);if(f.world_fb)vkDestroyFramebuffer(device,f.world_fb,nullptr);f.world_fb={};
        if(f.world_set)vkFreeDescriptorSets(device,descriptors,1,&f.world_set);f.world_set={};
        destroy(f.world);destroy(f.world_depth);}
    for(auto &i:images){if(i.fb)vkDestroyFramebuffer(device,i.fb,nullptr);destroy(i.depth);
        if(i.view)vkDestroyImageView(device,i.view,nullptr);if(i.complete)vkDestroySemaphore(device,i.complete,nullptr);}
    images.clear();if(swapchain)vkDestroySwapchainKHR(device,swapchain,nullptr);swapchain={};
}
void restore_surface(){
    VK_CHECK(vkDeviceWaitIdle(device));destroy_targets();
    if(surface)vkDestroySurfaceKHR(instance,surface,nullptr);surface=VK_NULL_HANDLE;
    if(!SDL_Vulkan_CreateSurface(window,instance,&surface))throw std::runtime_error(SDL_GetError());
    VkBool32 supported=VK_FALSE;VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical,family,surface,&supported));
    if(!supported)throw std::runtime_error("Restored surface has no compatible presentation queue");
    surface_lost=false;resize_pending=true;
}
VkSampler sampler_for(int min,int mag,int ws,int wt,unsigned levels){
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter=mag==0x2600?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
    ci.minFilter=(min==0x2600||min==0x2700||min==0x2702)?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
    ci.mipmapMode=(min==0x2702||min==0x2703)?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU=ws==0x812f?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.addressModeV=wt==0x812f?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.maxLod=(min==0x2600||min==0x2601)?0.f:float(levels-1);
    VkSampler s;VK_CHECK(vkCreateSampler(device,&ci,nullptr,&s));return s;
}
void write_image_set(VkDescriptorSet set,VkImageView view,VkSampler sampler){
    VkDescriptorImageInfo info{sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=0;w.descriptorCount=1;
    w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&info;vkUpdateDescriptorSets(device,1,&w,0,nullptr);
}
VkSampler upscale_sampler{};
bool recreate(int vsync,int percent){
    int w,h;SDL_Vulkan_GetDrawableSize(window,&w,&h);if(w<=0||h<=0)return false;
    // Only surface/settings transitions drain the device, never the steady frame loop.
    VK_CHECK(vkDeviceWaitIdle(device));destroy_targets();
    VkSurfaceCapabilitiesKHR caps;VkResult caps_result=vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps);
    if(caps_result==VK_ERROR_SURFACE_LOST_KHR){restore_surface();caps_result=vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps);}
    VK_CHECK(caps_result);
    VkExtent2D extent=caps.currentExtent;
    if(extent.width==UINT32_MAX)extent={std::clamp((unsigned)w,caps.minImageExtent.width,caps.maxImageExtent.width),
                                    std::clamp((unsigned)h,caps.minImageExtent.height,caps.maxImageExtent.height)};
    if(!extent.width||!extent.height)return false;
    width=extent.width;height=extent.height;
    uint32_t count=0;VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&count,nullptr));
    std::vector<VkPresentModeKHR> modes(count);VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&count,modes.data()));
    VkPresentModeKHR present=VK_PRESENT_MODE_FIFO_KHR;
    if(!vsync){
        if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_MAILBOX_KHR)!=modes.end())present=VK_PRESENT_MODE_MAILBOX_KHR;
        if(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_IMMEDIATE_KHR)!=modes.end())present=VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    unsigned desired=std::max(3u,caps.minImageCount);if(caps.maxImageCount)desired=std::min(desired,caps.maxImageCount);
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.surface=surface;ci.minImageCount=desired;
    ci.imageFormat=color_format;ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;ci.imageExtent=extent;
    ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    can_capture=(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)!=0;
    if(can_capture)ci.imageUsage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
    // Preserve SDL's logical orientation. Pre-rotating geometry is a separate
    // optimization; never silently present a rotated image on older drivers.
    if(!(caps.supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
        throw std::runtime_error("Surface requires a non-identity transform; use GLES2");
    ci.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if(!(caps.supportedCompositeAlpha&ci.compositeAlpha))ci.compositeAlpha=(VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha&-caps.supportedCompositeAlpha);
    ci.presentMode=present;ci.clipped=VK_TRUE;VK_CHECK(vkCreateSwapchainKHR(device,&ci,nullptr,&swapchain));
    VK_CHECK(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr));std::vector<VkImage> handles(count);
    VK_CHECK(vkGetSwapchainImagesKHR(device,swapchain,&count,handles.data()));images.resize(count);
    for(unsigned j=0;j<count;++j){auto &i=images[j];i.image=handles[j];
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=i.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
        vi.format=color_format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VK_CHECK(vkCreateImageView(device,&vi,nullptr,&i.view));
        i.depth=make_image(width,height,1,depth_format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        i.fb=framebuffer(i.view,i.depth.view,width,height,native_pass);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VK_CHECK(vkCreateSemaphore(device,&si,nullptr,&i.complete));
    }
    scale=percent;world_width=std::max(1,width*percent/100);world_height=std::max(1,height*percent/100);
    if(percent<100)for(auto &f:frames){
        f.world=make_image(world_width,world_height,1,color_format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        f.world_depth=make_image(world_width,world_height,1,depth_format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        f.world_fb=framebuffer(f.world.view,f.world_depth.view,world_width,world_height,world_pass);
        f.world_set=descriptor(texture_layout);write_image_set(f.world_set,f.world.view,upscale_sampler);
    }
    benchmark=requested_benchmark;
    if(benchmark)for(auto &f:frames){
        f.benchmark_color=make_image(width,height,1,color_format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        f.benchmark_depth=make_image(width,height,1,depth_format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        f.benchmark_fb=framebuffer(f.benchmark_color.view,f.benchmark_depth.view,width,height,world_pass);
    }
    current_vsync=vsync;resize_pending=false;
    SDL_Log("NZP_VULKAN surface=%dx%d world=%dx%d present=%d frames=%u images=%u",width,height,world_width,world_height,(int)present,Frames,count);
    return true;
}
VkCompareOp compare(unsigned f){return f>=0x200&&f<=0x207?(VkCompareOp)(f-0x200):VK_COMPARE_OP_LESS_OR_EQUAL;}
VkBlendFactor blend_factor(unsigned f){switch(f){case 0:return VK_BLEND_FACTOR_ZERO;case 1:return VK_BLEND_FACTOR_ONE;
 case 0x300:return VK_BLEND_FACTOR_SRC_COLOR;case 0x301:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
 case 0x302:return VK_BLEND_FACTOR_SRC_ALPHA;case 0x303:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
 case 0x304:return VK_BLEND_FACTOR_DST_ALPHA;case 0x305:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
 case 0x306:return VK_BLEND_FACTOR_DST_COLOR;case 0x307:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
 default:throw std::runtime_error("Unsupported Vril blend factor");}}
VkPrimitiveTopology topology(unsigned m){switch(m){case 0:return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;case 1:return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
 case 3:return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;case 4:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 case 5:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;case 6:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
 default:throw std::runtime_error("Unsupported Vril primitive");}}
VkPipeline pipeline(const Layout &layout,unsigned mode,const nzp_vk_raster &s,int lightmapped=0){
    PipelineKey key{};key.lightmapped=lightmapped;key.layout=layout;key.mode=mode;key.blend=s.blend;key.depth=s.depth;key.depth_write=s.depth_write;
    key.cull=s.cull;key.offset=s.offset;key.blend_src=s.blend_src;key.blend_dst=s.blend_dst;key.depth_func=s.depth_func;key.cull_face=s.cull_face;
    if(!key.blend){key.blend_src=1;key.blend_dst=0;}
    if(!key.depth){key.depth_func=0x203;key.depth_write=0;}
    if(!key.cull)key.cull_face=0x404;
    auto it=pipelines.find(key);if(it!=pipelines.end())return it->second;
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(int i=0;i<2;i++){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module=i?fragment_shader:vertex_shader;stages[i].pName="main";}
    VkBool32 specialized=lightmapped?VK_TRUE:VK_FALSE;
    VkSpecializationMapEntry entry{0,0,sizeof(specialized)};
    VkSpecializationInfo specialization{1,&entry,sizeof(specialized),&specialized};
    stages[0].pSpecializationInfo=stages[1].pSpecializationInfo=&specialization;
    VkVertexInputBindingDescription bindings[4]{};VkVertexInputAttributeDescription attrs[4]{};
    for(int i=0;i<4;i++){bindings[i]={(unsigned)i,layout.stride[i],VK_VERTEX_INPUT_RATE_VERTEX};attrs[i]={(unsigned)i,(unsigned)i,layout.format[i],0};}
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vertex.vertexBindingDescriptionCount=4;
    vertex.pVertexBindingDescriptions=bindings;vertex.vertexAttributeDescriptionCount=4;vertex.pVertexAttributeDescriptions=attrs;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=topology(mode);
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.cullMode=s.cull?(s.cull_face==0x404?VK_CULL_MODE_FRONT_BIT:s.cull_face==0x408?VK_CULL_MODE_FRONT_AND_BACK:VK_CULL_MODE_BACK_BIT):VK_CULL_MODE_NONE;
    raster.depthBiasEnable=s.offset;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depth.depthTestEnable=s.depth;
    depth.depthWriteEnable=s.depth_write;depth.depthCompareOp=compare(s.depth_func);
    VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=15;attachment.blendEnable=s.blend;
    attachment.srcColorBlendFactor=attachment.srcAlphaBlendFactor=blend_factor(s.blend_src);
    attachment.dstColorBlendFactor=attachment.dstAlphaBlendFactor=blend_factor(s.blend_dst);
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blending.attachmentCount=1;blending.pAttachments=&attachment;
    VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dyn.dynamicStateCount=3;dyn.pDynamicStates=dynamic;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.stageCount=2;ci.pStages=stages;
    ci.pVertexInputState=&vertex;ci.pInputAssemblyState=&assembly;ci.pViewportState=&viewport;ci.pRasterizationState=&raster;
    ci.pMultisampleState=&multisample;ci.pDepthStencilState=&depth;ci.pColorBlendState=&blending;ci.pDynamicState=&dyn;
    ci.layout=pipeline_layout;ci.renderPass=native_pass;
    VkPipeline result;VK_CHECK(vkCreateGraphicsPipelines(device,pipeline_cache,1,&ci,nullptr,&result));pipelines.emplace(key,result);
    if(recording)SDL_Log("NZP_VULKAN runtime_pipeline_miss count=%zu",pipelines.size());return result;
}
Layout interleaved(){Layout l{};for(int i=0;i<3;i++)l.stride[i]=sizeof(nzp_vk_vertex);
 l.format[0]=VK_FORMAT_R32G32B32_SFLOAT;l.format[1]=VK_FORMAT_R32G32_SFLOAT;l.format[2]=VK_FORMAT_R8G8B8A8_UNORM;l.stride[3]=l.stride[1];l.format[3]=l.format[1];return l;}
std::shared_ptr<TextureGPU> material(unsigned id){
    auto &t=textures[id];if(!t.dirty&&t.gpu)return t.gpu;
    if(t.mips.empty()||t.mips[0].rgba.empty()){t.mips.resize(1);t.mips[0]={1,1,{255,255,255,255}};}
    unsigned levels=1;while(levels<t.mips.size()&&!t.mips[levels].rgba.empty())++levels;
    auto gpu=std::shared_ptr<TextureGPU>(new TextureGPU,[](TextureGPU *p){
        if(p->descriptor)vkFreeDescriptorSets(device,descriptors,1,&p->descriptor);
        if(p->sampler)vkDestroySampler(device,p->sampler,nullptr);delete p;});
    if(!t.pixel_dirty && t.gpu)gpu->image=t.gpu->image;
    else {
        t.gpu.reset();
        for(auto &old:t.image_pool){
            if(old.use_count()==1&&old->width==t.mips[0].w&&old->height==t.mips[0].h&&old->levels==levels){gpu->image=old;break;}
        }
        bool reused=bool(gpu->image);
        if(!gpu->image){
            gpu->image=std::shared_ptr<Image>(new Image(make_image(t.mips[0].w,t.mips[0].h,levels,VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT)),[](Image *i){destroy(*i);delete i;});
            t.image_pool.push_back(gpu->image);
        }
        barrier(frame().upload,gpu->image->handle,reused?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,reused?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT,
            reused?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,levels);
        for(unsigned i=0;i<levels;i++){
            auto &m=t.mips[i];auto location=stage(m.rgba.data(),m.rgba.size());
            VkBufferImageCopy copy{};copy.bufferOffset=location.second;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,i,0,1};copy.imageExtent={(unsigned)m.w,(unsigned)m.h,1};
            vkCmdCopyBufferToImage(frame().upload,location.first,gpu->image->handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        }
        barrier(frame().upload,gpu->image->handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,levels);
    }
    gpu->sampler=sampler_for(t.min,t.mag,t.wrap_s,t.wrap_t,levels);gpu->descriptor=descriptor(texture_layout);
    write_image_set(gpu->descriptor,gpu->image->view,gpu->sampler);t.gpu=gpu;t.dirty=t.pixel_dirty=false;return gpu;
}
void begin_pass(bool native){
    pass_has_draw=false;std::memcpy(pass_clear_color,clear_color,16);pass_clear_depth=clear_depth;
    VkClearValue values[2]{};std::memcpy(values[0].color.float32,clear_color,16);values[1].depthStencil.depth=clear_depth;
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};bi.renderPass=(native&&!benchmark)?native_pass:world_pass;
    bi.framebuffer=native?(benchmark?frame().benchmark_fb:images[image_index].fb):frame().world_fb;bi.renderArea.extent={(unsigned)(native?width:world_width),(unsigned)(native?height:world_height)};
    bi.clearValueCount=2;bi.pClearValues=values;vkCmdBeginRenderPass(frame().draw,&bi,VK_SUBPASS_CONTENTS_INLINE);in_pass=true;
}
void bind_draw(const Layout &layout,unsigned mode,const nzp_vk_uniform &uniform,const nzp_vk_raster &state,VkDescriptorSet texture,VkDescriptorSet lightmap=VK_NULL_HANDLE){
    pass_has_draw=true;auto cb=frame().draw;vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline(layout,mode,state,lightmap!=VK_NULL_HANDLE));
    float xs=(!ui&&scale<100)?float(world_width)/width:1.f,ys=(!ui&&scale<100)?float(world_height)/height:1.f;
    int target_h=(!ui&&scale<100)?world_height:height;
    VkViewport viewport{state.viewport[0]*xs,target_h-(state.viewport[1]+state.viewport[3])*ys,
        state.viewport[2]*xs,state.viewport[3]*ys,state.depth_near,state.depth_far};
    vkCmdSetViewport(cb,0,1,&viewport);VkRect2D scissor{{0,0},{(unsigned)(!ui&&scale<100?world_width:width),(unsigned)target_h}};
    vkCmdSetScissor(cb,0,1,&scissor);vkCmdSetDepthBias(cb,state.offset_units,0,state.offset_factor);
    size_t uoffset=allocate(sizeof(uniform),std::max<size_t>(16,properties.limits.minUniformBufferOffsetAlignment));
    std::memcpy((char*)frame().arena.mapped+uoffset,&uniform,sizeof(uniform));uint32_t offset=uoffset;
    VkDescriptorSet sets[]={frame().uniform_set,texture,lightmap?lightmap:texture};
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout,0,3,sets,1,&offset);
}
void submit(){
    auto &f=frame();if(in_pass){vkCmdEndRenderPass(f.draw);in_pass=false;}
    if(capture_requested&&(benchmark||can_capture)){
        size_t bytes=size_t(width)*height*4;
        if(readback.size<bytes){destroy(readback);readback=make_buffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);}
        VkImage image=benchmark?f.benchmark_color.handle:images[image_index].image;
        VkImageLayout layout=benchmark?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier(f.draw,image,layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={(unsigned)width,(unsigned)height,1};
        vkCmdCopyImageToBuffer(f.draw,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,&copy);
        barrier(f.draw,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layout,
            VK_ACCESS_TRANSFER_READ_BIT,0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    if(f.queries)vkCmdWriteTimestamp(f.draw,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,f.queries,1);
    VK_CHECK(vkEndCommandBuffer(f.upload));VK_CHECK(vkEndCommandBuffer(f.draw));
    VK_CHECK(vmaFlushAllocation(allocator,f.arena.allocation,0,f.used));
    for(size_t i=0;i<f.staging.size();i++)if(f.staging_used[i])VK_CHECK(vmaFlushAllocation(allocator,f.staging[i].allocation,0,f.staging_used[i]));
    VkCommandBuffer cbs[]={f.upload,f.draw};VkPipelineStageFlags stage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.waitSemaphoreCount=benchmark?0:1;si.pWaitSemaphores=&f.acquired;si.pWaitDstStageMask=&stage;
    si.commandBufferCount=2;si.pCommandBuffers=cbs;si.signalSemaphoreCount=benchmark?0:1;si.pSignalSemaphores=&images[image_index].complete;
    VK_CHECK(vkResetFences(device,1,&f.fence));VK_CHECK(vkQueueSubmit(queue,1,&si,f.fence));f.submitted=true;recording=false;
}
}

int NZP_VK_Init(SDL_Window *w,const char *directory,int enable_validation){
    try{
        window=w;validation_errors=0;cache_path=std::string(directory?directory:".")+"/vulkan-pipeline-cache.bin";
        if(SDL_Vulkan_LoadLibrary(nullptr)!=0)throw std::runtime_error(SDL_GetError());
        volkInitializeCustom((PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr());
    }catch(const std::exception &e){SDL_LogError(SDL_LOG_CATEGORY_RENDER,"Vulkan loader: %s",e.what());return 0;}
    try{
        if(!vkCreateInstance)throw std::runtime_error("Vulkan loader unavailable");
        uint32_t loader=VK_API_VERSION_1_0;if(vkEnumerateInstanceVersion)VK_CHECK(vkEnumerateInstanceVersion(&loader));
        if(loader<VK_API_VERSION_1_1)throw std::runtime_error("Vulkan 1.1 required");
        unsigned count=0;if(!SDL_Vulkan_GetInstanceExtensions(w,&count,nullptr))throw std::runtime_error(SDL_GetError());
        std::vector<const char*> extensions(count);SDL_Vulkan_GetInstanceExtensions(w,&count,extensions.data());
        std::vector<const char*> layers;
        if(enable_validation){uint32_t n=0;vkEnumerateInstanceLayerProperties(&n,nullptr);std::vector<VkLayerProperties> available(n);vkEnumerateInstanceLayerProperties(&n,available.data());
            for(auto &l:available)if(!std::strcmp(l.layerName,"VK_LAYER_KHRONOS_validation"))layers.push_back("VK_LAYER_KHRONOS_validation");
            if(layers.empty())throw std::runtime_error("Requested validation layer unavailable");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);}
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="NZPortable";app.pEngineName="Vril";app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=extensions.size();ci.ppEnabledExtensionNames=extensions.data();
        ci.enabledLayerCount=layers.size();ci.ppEnabledLayerNames=layers.data();VK_CHECK(vkCreateInstance(&ci,nullptr,&instance));volkLoadInstance(instance);
        if(!layers.empty()){VkDebugUtilsMessengerCreateInfoEXT di{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            di.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            di.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
            di.pfnUserCallback=validation;VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance,&di,nullptr,&debug));}
        if(!SDL_Vulkan_CreateSurface(w,instance,&surface))throw std::runtime_error(SDL_GetError());
        uint32_t n=0;VK_CHECK(vkEnumeratePhysicalDevices(instance,&n,nullptr));std::vector<VkPhysicalDevice> devices(n);VK_CHECK(vkEnumeratePhysicalDevices(instance,&n,devices.data()));
        for(auto p:devices){VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(p,&props);if(props.apiVersion<VK_API_VERSION_1_1)continue;
#ifdef __ANDROID__
            if(props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)continue; // host Mesa is a test target only
#endif
            uint32_t e=0;vkEnumerateDeviceExtensionProperties(p,nullptr,&e,nullptr);std::vector<VkExtensionProperties> exts(e);vkEnumerateDeviceExtensionProperties(p,nullptr,&e,exts.data());
            bool swap=false;for(auto &x:exts)if(!std::strcmp(x.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME))swap=true;if(!swap)continue;
            uint32_t q=0;vkGetPhysicalDeviceQueueFamilyProperties(p,&q,nullptr);std::vector<VkQueueFamilyProperties> qs(q);vkGetPhysicalDeviceQueueFamilyProperties(p,&q,qs.data());
            for(unsigned j=0;j<q;j++){VkBool32 present;vkGetPhysicalDeviceSurfaceSupportKHR(p,j,surface,&present);
                if((qs[j].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&present){physical=p;family=j;properties=props;break;}}
            if(physical)break;}
        if(!physical)throw std::runtime_error("No compatible graphics/present queue");
        float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
        const char *ext=VK_KHR_SWAPCHAIN_EXTENSION_NAME;VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qi;
        dc.enabledExtensionCount=1;dc.ppEnabledExtensionNames=&ext;VK_CHECK(vkCreateDevice(physical,&dc,nullptr,&device));volkLoadDevice(device);vkGetDeviceQueue(device,family,0,&queue);
        VmaVulkanFunctions funcs{};funcs.vkGetInstanceProcAddr=vkGetInstanceProcAddr;funcs.vkGetDeviceProcAddr=vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo ac{};ac.instance=instance;ac.physicalDevice=physical;ac.device=device;ac.vulkanApiVersion=VK_API_VERSION_1_1;ac.pVulkanFunctions=&funcs;
        VK_CHECK(vmaCreateAllocator(&ac,&allocator));
        uint32_t formats=0;VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&formats,nullptr));std::vector<VkSurfaceFormatKHR> fs(formats);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&formats,fs.data()));
        for(auto f:fs)if(f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR&&(f.format==VK_FORMAT_R8G8B8A8_UNORM||f.format==VK_FORMAT_B8G8R8A8_UNORM)){color_format=f.format;break;}
        if(fs.size()==1 && fs[0].format==VK_FORMAT_UNDEFINED && fs[0].colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)color_format=VK_FORMAT_R8G8B8A8_UNORM;
        if(!color_format)throw std::runtime_error("No non-sRGB RGBA8 surface format; preserve GLES gamma through fallback");
        for(auto f:{VK_FORMAT_D16_UNORM,VK_FORMAT_D32_SFLOAT}){VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(physical,f,&fp);
            if(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT){depth_format=f;break;}}
        if(!depth_format)throw std::runtime_error("No depth attachment format");
        native_pass=make_pass(false);world_pass=make_pass(true);
        VkDescriptorSetLayoutBinding ub{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lc.bindingCount=1;lc.pBindings=&ub;
        VK_CHECK(vkCreateDescriptorSetLayout(device,&lc,nullptr,&uniform_layout));
        VkDescriptorSetLayoutBinding tb{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};lc.pBindings=&tb;
        VK_CHECK(vkCreateDescriptorSetLayout(device,&lc,nullptr,&texture_layout));
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,Frames},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16384}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;pc.maxSets=16384+Frames;
        pc.poolSizeCount=2;pc.pPoolSizes=sizes;VK_CHECK(vkCreateDescriptorPool(device,&pc,nullptr,&descriptors));
        VkDescriptorSetLayout layouts[]={uniform_layout,texture_layout,texture_layout};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=3;pl.pSetLayouts=layouts;
        VK_CHECK(vkCreatePipelineLayout(device,&pl,nullptr,&pipeline_layout));
        VkShaderModuleCreateInfo sh{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sh.codeSize=sizeof(nzp_vk_vert);sh.pCode=nzp_vk_vert;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&vertex_shader));
        sh.codeSize=sizeof(nzp_vk_frag);sh.pCode=nzp_vk_frag;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&fragment_shader));
        std::vector<unsigned char> cache;FILE *file=std::fopen(cache_path.c_str(),"rb");
        if(file){std::fseek(file,0,SEEK_END);long len=std::ftell(file);std::rewind(file);if(len>=32&&len<32*1024*1024){cache.resize(len);if(std::fread(cache.data(),1,len,file)!=(size_t)len)cache.clear();}std::fclose(file);}
        // VkPipelineCacheHeaderVersionOne: reject incompatible devices/driver UUID.
        if(cache.size()>=32){uint32_t header[4];std::memcpy(header,cache.data(),16);
            if(header[0]<32||header[1]!=VK_PIPELINE_CACHE_HEADER_VERSION_ONE||header[2]!=properties.vendorID||header[3]!=properties.deviceID||
                std::memcmp(cache.data()+16,properties.pipelineCacheUUID,VK_UUID_SIZE))cache.clear();}
        VkPipelineCacheCreateInfo cc{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};cc.initialDataSize=cache.size();cc.pInitialData=cache.data();
        if(vkCreatePipelineCache(device,&cc,nullptr,&pipeline_cache)!=VK_SUCCESS){cc.initialDataSize=0;cc.pInitialData=nullptr;VK_CHECK(vkCreatePipelineCache(device,&cc,nullptr,&pipeline_cache));}
        uint32_t qcount=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&qcount,nullptr);std::vector<VkQueueFamilyProperties> qprops(qcount);vkGetPhysicalDeviceQueueFamilyProperties(physical,&qcount,qprops.data());
        timestamp_bits=qprops[family].timestampValidBits;
        for(auto &f:frames){VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=family;VK_CHECK(vkCreateCommandPool(device,&cp,nullptr,&f.pool));
            VkCommandBufferAllocateInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cb.commandPool=f.pool;cb.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cb.commandBufferCount=2;
            VkCommandBuffer buffers[2];VK_CHECK(vkAllocateCommandBuffers(device,&cb,buffers));f.upload=buffers[0];f.draw=buffers[1];
            VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fc.flags=VK_FENCE_CREATE_SIGNALED_BIT;VK_CHECK(vkCreateFence(device,&fc,nullptr,&f.fence));
            VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VK_CHECK(vkCreateSemaphore(device,&sc,nullptr,&f.acquired));
            f.arena=make_buffer(ArenaBytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            f.uniform_set=descriptor(uniform_layout);VkDescriptorBufferInfo bi{f.arena.handle,0,sizeof(nzp_vk_uniform)};
            VkWriteDescriptorSet dw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};dw.dstSet=f.uniform_set;dw.dstBinding=0;dw.descriptorCount=1;dw.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;dw.pBufferInfo=&bi;vkUpdateDescriptorSets(device,1,&dw,0,nullptr);
            if(qprops[family].timestampValidBits){VkQueryPoolCreateInfo qc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qc.queryType=VK_QUERY_TYPE_TIMESTAMP;qc.queryCount=2;VK_CHECK(vkCreateQueryPool(device,&qc,nullptr,&f.queries));}
        }
        upscale_sampler=sampler_for(0x2601,0x2601,0x812f,0x812f,1);
        // Prewarm common interleaved states. Unseen vertex layouts are logged misses.
        nzp_vk_raster s{};s.blend_src=0x302;s.blend_dst=0x303;s.depth_func=0x203;s.depth_write=1;s.cull_face=0x404;
        for(int layout_index=0;layout_index<3;layout_index++){
            Layout layout=interleaved();
            if(layout_index){layout.stride[0]=layout_index==1?28:12;layout.stride[1]=layout.stride[3]=layout_index==1?28:0;layout.stride[2]=0;}
            for(int depth=0;depth<2;depth++)for(int blend=0;blend<2;blend++)for(int cull=0;cull<3;cull++){
                s.depth=depth;s.blend=blend;s.cull=cull!=0;s.cull_face=cull==2?0x405:0x404;
                for(int write=0;write<2;write++){s.depth_write=write;pipeline(layout,4,s);if(layout_index==1)pipeline(layout,4,s,1);}
            }
        }
        active=true;recreate(1,100);
        SDL_Log("NZP_VULKAN device=%s api=%u.%u driver=%u",properties.deviceName,VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),properties.driverVersion);
        return 1;
    }catch(const std::exception &e){SDL_LogError(SDL_LOG_CATEGORY_RENDER,"Vulkan initialization failed: %s",e.what());NZP_VK_Shutdown();return 0;}
}
int NZP_VK_Active(){return active;}
void NZP_VK_NotifySurfaceLost(){surface_lost=true;resize_pending=true;}
void NZP_VK_SetBenchmark(int enabled){if(requested_benchmark!=bool(enabled)){requested_benchmark=enabled;resize_pending=true;}}
unsigned long long NZP_VK_CompletedFrames(){return completed_frames;}
void NZP_VK_Finish(){
    if(!device)return;VK_CHECK(vkDeviceWaitIdle(device));
    for(auto &f:frames)if(f.submitted){++completed_frames;f.submitted=false;}
}
int NZP_VK_BeginFrame(int vsync,int percent){
    if(!active)return 0;auto &f=frame();
    VK_CHECK(vkWaitForFences(device,1,&f.fence,VK_TRUE,UINT64_MAX));
    bool was_submitted=f.submitted;
    if(was_submitted){++completed_frames;f.submitted=false;}
    if(was_submitted&&f.queries){uint64_t times[2]{};if(vkGetQueryPoolResults(device,f.queries,0,2,sizeof(times),times,8,VK_QUERY_RESULT_64_BIT)==VK_SUCCESS)
        gpu_ms=((times[1]-times[0])&(timestamp_bits<64?((uint64_t(1)<<timestamp_bits)-1):UINT64_MAX))*properties.limits.timestampPeriod/1e6;}
    f.textures.clear();f.meshes.clear();f.used=0;f.staging_index=0;
    std::fill(f.staging_used.begin(),f.staging_used.end(),0);
    int w,h;SDL_Vulkan_GetDrawableSize(window,&w,&h);
    if(w<=0||h<=0)return 0;
    if(surface_lost)restore_surface();
    if(resize_pending||w!=width||h!=height||vsync!=current_vsync||percent!=scale)if(!recreate(vsync,percent))return 0;
    VkResult r=VK_SUCCESS;
    if(!benchmark)r=vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,f.acquired,VK_NULL_HANDLE,&image_index);
    if(r==VK_ERROR_SURFACE_LOST_KHR){surface_lost=true;resize_pending=true;return 0;}
    if(r==VK_ERROR_OUT_OF_DATE_KHR){resize_pending=true;return 0;}if(r!=VK_SUBOPTIMAL_KHR)VK_CHECK(r);else resize_pending=true;
    VK_CHECK(vkResetCommandPool(device,f.pool,0));VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(f.upload,&bi));VK_CHECK(vkBeginCommandBuffer(f.draw,&bi));
    if(f.queries){vkCmdResetQueryPool(f.draw,f.queries,0,2);vkCmdWriteTimestamp(f.draw,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,f.queries,0);}
    recording=true;ui=false;begin_pass(scale==100);return 1;
}
void NZP_VK_BeginUI(){
    if(!recording||ui)return;
    if(scale<100){vkCmdEndRenderPass(frame().draw);in_pass=false;ui=true;begin_pass(true);
        nzp_vk_uniform u{};for(int i=0;i<4;i++)u.mvp[i*5]=u.mv[i*5]=u.tm[i*5]=1;
        u.params[0]=u.params[1]=1;u.params[2]=-1;
        nzp_vk_raster s{};s.blend_src=1;s.blend_dst=0;s.depth_func=0x203;s.depth_far=1;s.viewport[2]=width;s.viewport[3]=height;
        const nzp_vk_vertex quad[]={{{-1,-1,0},{0,1},{255,255,255,255}},{{1,-1,0},{1,1},{255,255,255,255}},{{1,1,0},{1,0},{255,255,255,255}},
            {{-1,-1,0},{0,1},{255,255,255,255}},{{1,1,0},{1,0},{255,255,255,255}},{{-1,1,0},{0,0},{255,255,255,255}}};
        size_t offset=allocate(sizeof(quad),4);std::memcpy((char*)frame().arena.mapped+offset,quad,sizeof(quad));
        VkBuffer buffers[]={frame().arena.handle,frame().arena.handle,frame().arena.handle,frame().arena.handle};VkDeviceSize offsets[]={offset,offset+12,offset+20,offset+12};
        bind_draw(interleaved(),4,u,s,frame().world_set);vkCmdBindVertexBuffers(frame().draw,0,4,buffers,offsets);vkCmdDraw(frame().draw,6,1,0,0);
    }else ui=true;
}
void NZP_VK_EndFrame(){
    if(!recording)return;NZP_VK_BeginUI();submit();
    if(capture_requested&&(benchmark||can_capture)){
        VK_CHECK(vkWaitForFences(device,1,&frame().fence,VK_TRUE,UINT64_MAX));
        VK_CHECK(vmaInvalidateAllocation(allocator,readback.allocation,0,VK_WHOLE_SIZE));
        captured.resize(size_t(width)*height*4);auto *src=(const unsigned char*)readback.mapped;
        // Match GL's bottom-left readback origin and RGBA component order.
        for(int y=0;y<height;y++)for(int x=0;x<width;x++){
            auto *out=captured.data()+(size_t(y)*width+x)*4;const auto *p=src+(size_t(height-1-y)*width+x)*4;
            out[0]=p[color_format==VK_FORMAT_B8G8R8A8_UNORM?2:0];out[1]=p[1];out[2]=p[color_format==VK_FORMAT_B8G8R8A8_UNORM?0:2];out[3]=p[3];
        }
        if(!screenshot_path.empty()){
            FILE *file=std::fopen(screenshot_path.c_str(),"wb");if(file){unsigned char header[18]={};header[2]=2;header[12]=width&255;header[13]=width>>8;header[14]=height&255;header[15]=height>>8;header[16]=24;
                std::fwrite(header,1,18,file);for(size_t i=0;i<captured.size();i+=4){unsigned char bgr[]={captured[i+2],captured[i+1],captured[i]};std::fwrite(bgr,1,3,file);}std::fclose(file);}
            screenshot_path.clear();
        }
    }
    capture_requested=false;
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.waitSemaphoreCount=1;pi.pWaitSemaphores=&images[image_index].complete;
    pi.swapchainCount=1;pi.pSwapchains=&swapchain;pi.pImageIndices=&image_index;
    auto r=benchmark?VK_SUCCESS:vkQueuePresentKHR(queue,&pi);if(r==VK_ERROR_SURFACE_LOST_KHR){surface_lost=true;resize_pending=true;}
    else if(r==VK_ERROR_OUT_OF_DATE_KHR||r==VK_SUBOPTIMAL_KHR)resize_pending=true;else VK_CHECK(r);
    frame_index=(frame_index+1)%Frames;
}
void NZP_VK_Draw(const nzp_vk_vertex *v,int count,unsigned mode,const nzp_vk_uniform *u,const nzp_vk_raster *s,unsigned tex){
    if(!recording||!count)return;auto gpu=material(tex);frame().textures.push_back(gpu);
    size_t offset=allocate(count*sizeof(*v),4);std::memcpy((char*)frame().arena.mapped+offset,v,count*sizeof(*v));
    bind_draw(interleaved(),mode,*u,*s,gpu->descriptor);
    VkBuffer buffers[]={frame().arena.handle,frame().arena.handle,frame().arena.handle,frame().arena.handle};VkDeviceSize offsets[]={offset,offset+12,offset+20,offset+12};
    vkCmdBindVertexBuffers(frame().draw,0,4,buffers,offsets);vkCmdDraw(frame().draw,count,1,0,0);
}
static int draw_buffers(const nzp_vk_array *a,unsigned ib,size_t io,int count,unsigned mode,const nzp_vk_uniform *u,const nzp_vk_raster *s,unsigned tex,const float *uv,const unsigned char *color,const nzp_vk_array *lm,unsigned lmtex){
    if(!recording)return 1;Layout layout{};VkBuffer buffers[4];VkDeviceSize offsets[4];
    for(int i=0;i<3;i++){
        if(a[i].buffer){auto it=meshes.find(a[i].buffer);if(it==meshes.end()||!it->second.gpu)return 0;
            frame().meshes.push_back(it->second.gpu);buffers[i]=it->second.gpu->handle;offsets[i]=a[i].offset;
            layout.stride[i]=a[i].stride?a[i].stride:a[i].size*(a[i].type==0x1406?4:a[i].type==0x1402?2:1);
            if(a[i].type==0x1406)layout.format[i]=a[i].size==2?VK_FORMAT_R32G32_SFLOAT:a[i].size==3?VK_FORMAT_R32G32B32_SFLOAT:VK_FORMAT_R32G32B32A32_SFLOAT;
            else if(a[i].type==0x1401&&i==2)layout.format[i]=VK_FORMAT_R8G8B8A8_UNORM;
            else return 0;
        }else{
            if(i==0)return 0;size_t off=allocate(i==1?8:4,4);std::memcpy((char*)frame().arena.mapped+off,i==1?(const void*)uv:(const void*)color,i==1?8:4);
            buffers[i]=frame().arena.handle;offsets[i]=off;layout.stride[i]=0;layout.format[i]=i==1?VK_FORMAT_R32G32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
        }
    }
    layout.stride[3]=layout.stride[1];layout.format[3]=layout.format[1];buffers[3]=buffers[1];offsets[3]=offsets[1];
    std::shared_ptr<TextureGPU> lightmap;
    if(lm){auto it=meshes.find(lm->buffer);if(it==meshes.end()||!it->second.gpu)return 0;
        frame().meshes.push_back(it->second.gpu);buffers[3]=it->second.gpu->handle;offsets[3]=lm->offset;
        layout.stride[3]=lm->stride;layout.format[3]=VK_FORMAT_R32G32_SFLOAT;
        lightmap=material(lmtex);frame().textures.push_back(lightmap);
    }
    auto gpu=material(tex);frame().textures.push_back(gpu);nzp_vk_uniform uniforms=*u;uniforms.params[3]=lm?1.f:0.f;
    bind_draw(layout,mode,uniforms,*s,gpu->descriptor,lightmap?lightmap->descriptor:VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(frame().draw,0,4,buffers,offsets);
    if(ib){auto it=meshes.find(ib);if(it==meshes.end()||!it->second.gpu)return 0;frame().meshes.push_back(it->second.gpu);
        vkCmdBindIndexBuffer(frame().draw,it->second.gpu->handle,io,VK_INDEX_TYPE_UINT16);vkCmdDrawIndexed(frame().draw,count,1,0,0,0);
    }else vkCmdDraw(frame().draw,count,1,0,0);
    return 1;
}
int NZP_VK_DrawBuffers(const nzp_vk_array *a,unsigned ib,size_t offset,int count,unsigned mode,const nzp_vk_uniform *u,const nzp_vk_raster *s,unsigned tex,const float *uv,const unsigned char *color){
    return draw_buffers(a,ib,offset,count,mode,u,s,tex,uv,color,nullptr,0);
}
int NZP_VK_DrawLightmappedBuffers(const nzp_vk_array *a,const nzp_vk_array *lm,unsigned ib,size_t offset,int count,const nzp_vk_uniform *u,const nzp_vk_raster *s,unsigned tex,unsigned lmtex,const float *uv,const unsigned char *color){
    return draw_buffers(a,ib,offset,count,4,u,s,tex,uv,color,lm,lmtex);
}
void NZP_VK_Buffer(unsigned id,size_t bytes,const void *data,int){
    if(!bytes){meshes.erase(id);return;}
    auto &mesh=meshes[id];mesh.gpu.reset();std::shared_ptr<Buffer> b;
    for(auto &old:mesh.pool)if(old.use_count()==1&&old->size>=bytes){b=old;break;}
    if(!b){
        b=std::shared_ptr<Buffer>(new Buffer(make_buffer(bytes,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT)),[](Buffer *b){destroy(*b);delete b;});
        mesh.pool.push_back(b);
    }
    if(data)std::memcpy(b->mapped,data,bytes);else std::memset(b->mapped,0,bytes);
    VK_CHECK(vmaFlushAllocation(allocator,b->allocation,0,bytes));mesh.gpu=b;
}
void NZP_VK_DeleteBuffer(unsigned id){meshes.erase(id);}
void NZP_VK_Texture(unsigned id,int level,int x,int y,int w,int h,unsigned format,unsigned type,const void *pixels,int replace,int alignment){
    if(level<0||w<=0||h<=0)return;
    auto &t=textures[id];if(t.mips.size()<unsigned(level+1))t.mips.resize(level+1);auto &m=t.mips[level];
    if(replace){m.w=w;m.h=h;m.rgba.resize(size_t(w)*h*4);x=y=0;}
    if(x<0||y<0||x+w>m.w||y+h>m.h)throw std::runtime_error("Invalid texture subregion");
    int components=format==0x1909?1:4;int pixelbytes=type==0x1401?components:2;
    size_t row=(size_t(w)*pixelbytes+alignment-1)/alignment*alignment;
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){
        unsigned char *out=m.rgba.data()+((size_t(y+j)*m.w)+x+i)*4;
        if(!pixels){std::memset(out,0,4);continue;}
        auto p=(const unsigned char*)pixels+j*row+i*pixelbytes;
        if(type==0x1401){if(components==1){out[0]=out[1]=out[2]=*p;out[3]=255;}else std::memcpy(out,p,4);}
        else {uint16_t v;std::memcpy(&v,p,2);
            if(type==0x8033){out[0]=((v>>12)&15)*17;out[1]=((v>>8)&15)*17;out[2]=((v>>4)&15)*17;out[3]=(v&15)*17;}
            else if(type==0x8034){for(int k=0;k<3;k++){unsigned n=(v>>(11-5*k))&31;out[k]=(n<<3)|(n>>2);}out[3]=(v&1)?255:0;}
            else throw std::runtime_error("Unsupported texture pixel type");}
    }
    t.dirty=t.pixel_dirty=true;
}
void NZP_VK_TextureFilter(unsigned id,unsigned name,int value){auto &t=textures[id];int *p=name==0x2801?&t.min:name==0x2800?&t.mag:name==0x2802?&t.wrap_s:name==0x2803?&t.wrap_t:nullptr;
 if(p&&*p!=value){*p=value;t.dirty=true;}}
void NZP_VK_DeleteTexture(unsigned id){textures.erase(id);}
void NZP_VK_Clear(unsigned mask,const float *color,float depth){
    if(color)std::memcpy(clear_color,color,16);clear_depth=depth;if(!recording||!in_pass)return;
    if(!pass_has_draw){
        if((mask&0x4000)&&!std::memcmp(pass_clear_color,clear_color,16))mask&=~0x4000u;
        if((mask&0x100)&&pass_clear_depth==depth)mask&=~0x100u;
    }
    VkClearAttachment a[2]{};unsigned n=0;
    if(mask&0x4000){std::memcpy(pass_clear_color,clear_color,16);a[n].aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;std::memcpy(a[n].clearValue.color.float32,clear_color,16);n++;}
    if(mask&0x100){pass_clear_depth=depth;a[n].aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;a[n].clearValue.depthStencil.depth=depth;n++;}
    if(n){VkClearRect r{{{0,0},{(unsigned)(!ui&&scale<100?world_width:width),(unsigned)(!ui&&scale<100?world_height:height)}},0,1};vkCmdClearAttachments(frame().draw,n,a,1,&r);}
}
void NZP_VK_RequestCapture(const char *path){capture_requested=true;screenshot_path=path?path:"";}
int NZP_VK_ReadPixels(int x,int y,int w,int h,void *rgba){
    if(!rgba||x<0||y<0||x+w>width||y+h>height||captured.size()!=size_t(width)*height*4)return 0;
    for(int j=0;j<h;j++)std::memcpy((unsigned char*)rgba+size_t(j)*w*4,captured.data()+(size_t(y+j)*width+x)*4,size_t(w)*4);
    return 1;
}
void NZP_VK_GetSize(int *w,int *h){if(w)*w=width;if(h)*h=height;}
unsigned NZP_VK_MaxTextureSize(){return properties.limits.maxImageDimension2D;}
double NZP_VK_GPUTime(){return gpu_ms;}
unsigned NZP_VK_ValidationErrors(){return validation_errors;}
void NZP_VK_Shutdown(){
    active=false;
    if(device){vkDeviceWaitIdle(device);
        if(pipeline_cache){size_t size=0;if(vkGetPipelineCacheData(device,pipeline_cache,&size,nullptr)==VK_SUCCESS&&size<32*1024*1024){std::vector<unsigned char> data(size);
            if(vkGetPipelineCacheData(device,pipeline_cache,&size,data.data())==VK_SUCCESS){std::string temp=cache_path+".tmp";FILE *f=std::fopen(temp.c_str(),"wb");if(f){bool ok=std::fwrite(data.data(),1,size,f)==size;std::fclose(f);if(ok)std::rename(temp.c_str(),cache_path.c_str());}}}}
        destroy(readback);captured.clear();capture_requested=false;screenshot_path.clear();
        destroy_targets();for(auto &f:frames){f.textures.clear();f.meshes.clear();}textures.clear();meshes.clear();
        for(auto &p:pipelines)vkDestroyPipeline(device,p.second,nullptr);pipelines.clear();
        for(auto &f:frames){for(auto &b:f.staging)destroy(b);destroy(f.arena);if(f.queries)vkDestroyQueryPool(device,f.queries,nullptr);if(f.pool)vkDestroyCommandPool(device,f.pool,nullptr);
            if(f.fence)vkDestroyFence(device,f.fence,nullptr);if(f.acquired)vkDestroySemaphore(device,f.acquired,nullptr);f={};}
        if(upscale_sampler)vkDestroySampler(device,upscale_sampler,nullptr);
        if(vertex_shader)vkDestroyShaderModule(device,vertex_shader,nullptr);if(fragment_shader)vkDestroyShaderModule(device,fragment_shader,nullptr);
        if(pipeline_cache)vkDestroyPipelineCache(device,pipeline_cache,nullptr);if(pipeline_layout)vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        if(descriptors)vkDestroyDescriptorPool(device,descriptors,nullptr);if(uniform_layout)vkDestroyDescriptorSetLayout(device,uniform_layout,nullptr);if(texture_layout)vkDestroyDescriptorSetLayout(device,texture_layout,nullptr);
        if(native_pass)vkDestroyRenderPass(device,native_pass,nullptr);if(world_pass)vkDestroyRenderPass(device,world_pass,nullptr);
        if(allocator)vmaDestroyAllocator(allocator);vkDestroyDevice(device,nullptr);}
    if(debug)vkDestroyDebugUtilsMessengerEXT(instance,debug,nullptr);if(surface)vkDestroySurfaceKHR(instance,surface,nullptr);
    if(instance)vkDestroyInstance(instance,nullptr);
    instance={};physical={};device={};surface={};allocator={};debug={};swapchain={};
    descriptors={};uniform_layout={};texture_layout={};pipeline_layout={};pipeline_cache={};vertex_shader={};fragment_shader={};native_pass={};world_pass={};upscale_sampler={};
    frame_index=0;recording=in_pass=ui=resize_pending=false;benchmark=requested_benchmark=surface_lost=false;completed_frames=0;current_vsync=-1;width=height=0;gpu_ms=-1;color_format=depth_format=VK_FORMAT_UNDEFINED;
    SDL_Vulkan_UnloadLibrary();
}
