// SPDX-License-Identifier: GPL-2.0-or-later
// Native Vulkan 1.1 backend for Vril's shared geometry/material frontend.
#include "renderer.h"
#include "prerotate.h"
#include "../jobs.h"
#include "fsr.h"
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
#include <cstdlib>
#include <mutex>
#include <atomic>
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
constexpr size_t StagingChunkBytes=1024*1024;
constexpr unsigned StagingIdleFrameLimit=90;
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
struct DrawState {
    VkPipeline pipeline{};
    VkViewport viewport{};
    VkRect2D scissor{};
    float bias_units{},bias_factor{};
    VkDescriptorSet sets[3]{};
    uint32_t uniform_offset{},bound_offset{};
    nzp_vk_uniform uniform{};
    bool viewport_valid{},scissor_valid{},bias_valid{},sets_valid{},uniform_valid{};
};
struct Frame {
    nzp_vk_command_stats stats{};
    nzp_vk_frame_timings timings{};
    DrawState draw_state{};
    VkCommandPool pool{}; VkCommandBuffer upload{},draw{};
    VkFence fence{}; VkSemaphore acquired{};
    Buffer arena; size_t used{};
    std::vector<Buffer> staging;std::vector<size_t> staging_used;unsigned staging_index{},staging_idle_frames{};
    VkDescriptorSet uniform_set{};
    std::vector<std::shared_ptr<TextureGPU>> textures;
    std::vector<std::shared_ptr<Buffer>> meshes;
    Image world,world_depth,world_mv,fsr_out;VkFramebuffer world_fb{};VkDescriptorSet lite_set[2]{};
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
    int blend{},depth{},depth_write{},cull{},offset{},lightmapped{},alpha_test{},mrt{},jitter{};
    bool operator<(const PipelineKey &b)const{return std::memcmp(this,&b,sizeof(*this))<0;}
};
VkInstance instance{}; VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};
VkSurfaceKHR surface{};VkSwapchainKHR swapchain{};VmaAllocator allocator{};
VkPhysicalDeviceProperties properties{};
VkDescriptorPool descriptors{}; VkDescriptorSetLayout uniform_layout{},texture_layout{};
VkPipelineLayout pipeline_layout{};VkPipelineCache pipeline_cache{};
VkShaderModule vertex_shader{},fragment_shader{},fragment_shader_mrt{};
VkRenderPass native_pass{},world_pass{},fsr_pass{};
VkDebugUtilsMessengerEXT debug{};
std::map<PipelineKey,VkPipeline> pipelines;std::mutex pipeline_mutex;
PipelineKey recent_pipeline_key{};VkPipeline recent_pipeline{};
nzp_vk_command_stats last_command_stats{};nzp_vk_frame_timings last_frame_timings{};
std::map<unsigned,Texture> textures;std::map<unsigned,Mesh> meshes;
Image lite_hist[2];
std::array<Frame,Frames> frames;
std::vector<SwapImage> images;
SDL_Window *window{};std::string cache_path;
unsigned family{},frame_index{},image_index{},validation_errors{};
int width{},height{},world_width{},world_height{},current_vsync=-1,scale=100;
// Pre-rotation: width/height stay logical (SDL window orientation). phys_* is the
// native-pass target size (swapped for 90/270). rotation is the effective value;
// sc_rotation is the real swapchain preTransform, test_rotation is benchmark-only.
int rotation{},sc_rotation{},phys_w{},phys_h{},drawable_w{},drawable_h{},test_rotation=-1;bool prerotate_wanted=true;
// Temporal upscaling (Arm ASR). fsr_active implies the offscreen world pass also writes motion vectors
// and depth is stored/sampled (D32), which costs bandwidth that bilinear upscaling does not.
bool fsr_wanted=false,fsr_active=false,fsr_reset=true,fsr_failed=false,fsr_supported=false;nzp_fsr::Context *fsr_ctx=nullptr;
int fsr_phase=0,fsr_phase_count=1;float jitter_px[2]{},cam_near=4.f,cam_far=4096.f,cam_fov=1.2f;
float fsr_sharpness=0.2f;Uint64 last_frame_ticks=0;
// "FSR Lite": cheap single-pass temporal upscaler for old GPUs (lite.frag). upscaler_mode: 0 off, 1 Arm ASR, 2 Lite.
int upscaler_mode=0;bool lite_active=false,lite_failed=false,lite_supported=false;
VkRenderPass lite_pass{},resolve_pass{};VkShaderModule lite_vert{},lite_frag{};
VkDescriptorSetLayout lite_set_layout{};VkPipelineLayout lite_pipeline_layout{};VkPipeline lite_pipeline{};
VkSampler nearest_sampler{};VkFramebuffer lite_fb[2]{};VkDescriptorSet lite_ui_set[2]{};
bool lite_history_valid=false;VkDescriptorSet lite_ui_current{};
// Lite "fast" (Mali): 2 fetches/pixel, rotation-only reprojection, no motion-vector target. quality: 0 auto, 1 fast, 2 full.
bool lite_fast=false,is_mali=false,jitter_prewarmed=false;int lite_quality=0;VkShaderModule lite_fast_vert{},lite_fast_frag{};VkPipeline lite_fast_pipeline{};
float reprojection[9]{1,0,0,0,1,0,0,0,1};unsigned long long reprojection_serial=~0ull;
int lite_out=0,lite_state[2]{0,0},lite_phase=0,lite_slow_frames=0,lite_frames_alive=0;float lite_sharpness=0.3f;
int fsr_contexts_created=0,fsr_slow_frames=0,fsr_frames_alive=0,fsr_ctx_rw=0,fsr_ctx_rh=0,fsr_ctx_dw=0,fsr_ctx_dh=0,fsr_ctx_quality=0;
// What the swapchain was built against; a VK_SUBOPTIMAL_KHR only triggers a rebuild if this changed.
VkSurfaceTransformFlagBitsKHR sc_seen_transform{};uint32_t sc_seen_w=0,sc_seen_h=0;unsigned long long frame_serial=0;bool mrt_prewarmed=false;
const VkFormat FsrColorFormat=VK_FORMAT_R8G8B8A8_UNORM,FsrMotionFormat=VK_FORMAT_R16G16_SFLOAT,FsrDepthFormat=VK_FORMAT_D32_SFLOAT;
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
    f.staging.push_back(make_buffer(std::max<size_t>(StagingChunkBytes,bytes),VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
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
// World pass for the temporal upscaler: colour + motion vectors + stored depth, all left in
// shader-readable layouts for Arm ASR (depth: DEPTH_STENCIL_READ_ONLY, as the library expects).
VkRenderPass make_fsr_pass(bool store_depth){
    VkAttachmentDescription a[3]{};
    for(int i=0;i<3;i++){a[i].samples=VK_SAMPLE_COUNT_1_BIT;a[i].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a[i].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        a[i].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a[i].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a[i].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;}
    a[0].format=FsrColorFormat;a[0].finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    a[1].format=FsrMotionFormat;a[1].finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    a[2].format=FsrDepthFormat;
    // Arm ASR samples the depth; Lite does not, so its depth stays in tile memory (never written back).
    a[2].storeOp=store_depth?VK_ATTACHMENT_STORE_OP_STORE:VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[2].finalLayout=store_depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colors[2]{{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},depth{2,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=2;sub.pColorAttachments=colors;sub.pDepthStencilAttachment=&depth;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;
    deps[0].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask=deps[0].srcStageMask;
    deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ci.attachmentCount=3;ci.pAttachments=a;
    ci.subpassCount=1;ci.pSubpasses=&sub;ci.dependencyCount=2;ci.pDependencies=deps;
    VkRenderPass p;VK_CHECK(vkCreateRenderPass(device,&ci,nullptr,&p));return p;
}
// Lite resolve pass: one RGBA8 target that is sampled afterwards (and next frame as history).
VkRenderPass make_resolve_pass(){
    VkAttachmentDescription a{};a.format=FsrColorFormat;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;a.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;deps[0].srcStageMask=deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ci.attachmentCount=1;ci.pAttachments=&a;ci.subpassCount=1;ci.pSubpasses=&sub;ci.dependencyCount=2;ci.pDependencies=deps;
    VkRenderPass p;VK_CHECK(vkCreateRenderPass(device,&ci,nullptr,&p));return p;
}
VkFramebuffer framebuffer1(VkImageView color,int w,int h,VkRenderPass pass){
    VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};ci.renderPass=pass;ci.attachmentCount=1;ci.pAttachments=&color;ci.width=w;ci.height=h;ci.layers=1;
    VkFramebuffer fb;VK_CHECK(vkCreateFramebuffer(device,&ci,nullptr,&fb));return fb;
}
VkFramebuffer framebuffer3(VkImageView color,VkImageView motion,VkImageView depth,int w,int h,VkRenderPass pass){
    VkImageView views[]={color,motion,depth};VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass=pass;ci.attachmentCount=3;ci.pAttachments=views;ci.width=w;ci.height=h;ci.layers=1;
    VkFramebuffer fb;VK_CHECK(vkCreateFramebuffer(device,&ci,nullptr,&fb));return fb;
}
VkFramebuffer framebuffer(VkImageView color,VkImageView depth,int w,int h,VkRenderPass pass){
    VkImageView views[]={color,depth};VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass=pass;ci.attachmentCount=2;ci.pAttachments=views;ci.width=w;ci.height=h;ci.layers=1;
    VkFramebuffer fb;VK_CHECK(vkCreateFramebuffer(device,&ci,nullptr,&fb));return fb;
}
void destroy_fsr_context(){if(fsr_ctx){nzp_fsr::destroy(fsr_ctx);fsr_ctx=nullptr;}fsr_ctx_rw=fsr_ctx_rh=fsr_ctx_dw=fsr_ctx_dh=0;}
void destroy_targets(){
    for(auto &f:frames){if(f.benchmark_fb)vkDestroyFramebuffer(device,f.benchmark_fb,nullptr);f.benchmark_fb={};destroy(f.benchmark_color);destroy(f.benchmark_depth);if(f.world_fb)vkDestroyFramebuffer(device,f.world_fb,nullptr);f.world_fb={};
        if(f.world_set)vkFreeDescriptorSets(device,descriptors,1,&f.world_set);f.world_set={};
        for(auto &set:f.lite_set){if(set)vkFreeDescriptorSets(device,descriptors,1,&set);set={};}
        destroy(f.world);destroy(f.world_depth);destroy(f.world_mv);destroy(f.fsr_out);}
    for(int k=0;k<2;k++){if(lite_fb[k])vkDestroyFramebuffer(device,lite_fb[k],nullptr);lite_fb[k]={};
        if(lite_ui_set[k])vkFreeDescriptorSets(device,descriptors,1,&lite_ui_set[k]);lite_ui_set[k]={};destroy(lite_hist[k]);lite_state[k]=0;}
    lite_active=false;lite_history_valid=false;
    fsr_active=false; // the FSR context itself survives swapchain rebuilds (it is expensive to create)
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
void prewarm_pipelines(bool mrt,bool jitter=false);
void write_lite_set(VkDescriptorSet set,VkImageView cur,VkImageView mv,VkImageView history);
// A driver may keep reporting VK_SUBOPTIMAL_KHR forever (for example when it prefers a different
// pre-transform). Rebuilding on every such report meant a swapchain rebuild per frame; only rebuild
// when the surface really differs from what the swapchain was built for.
void note_suboptimal(){
    VkSurfaceCapabilitiesKHR caps;
    if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps)!=VK_SUCCESS){resize_pending=true;return;}
    if(caps.currentTransform!=sc_seen_transform||caps.currentExtent.width!=sc_seen_w||caps.currentExtent.height!=sc_seen_h){resize_pending=true;return;}
    static bool logged=false;
    if(!logged){logged=true;SDL_Log("NZP_VULKAN ignoring persistent VK_SUBOPTIMAL_KHR (surface unchanged, transform=%d)",(int)caps.currentTransform);}
}
bool recreate(int vsync,int percent){
    int w,h;SDL_Vulkan_GetDrawableSize(window,&w,&h);if(w<=0||h<=0)return false;
    drawable_w=w;drawable_h=h;
    // Only surface/settings transitions drain the device, never the steady frame loop.
    VK_CHECK(vkDeviceWaitIdle(device));destroy_targets();
    VkSurfaceCapabilitiesKHR caps;VkResult caps_result=vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps);
    if(caps_result==VK_ERROR_SURFACE_LOST_KHR){restore_surface();caps_result=vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps);}
    VK_CHECK(caps_result);
    auto rotation_of=[](VkSurfaceTransformFlagsKHR t)->int{
        return t==VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR?1:t==VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR?2:
               t==VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR?3:0;};
    auto pick_extent=[&](int rot){
        VkExtent2D e=caps.currentExtent;
        if(e.width==UINT32_MAX){
            unsigned ew=nzp_rot_odd(rot)?h:w,eh=nzp_rot_odd(rot)?w:h;
            e={std::clamp(ew,caps.minImageExtent.width,caps.maxImageExtent.width),
               std::clamp(eh,caps.minImageExtent.height,caps.maxImageExtent.height)};
        }
        return e;};
    // Mali/Android: presenting with preTransform == currentTransform avoids a
    // compositor/display-processor rotation pass. Fall back to identity only if
    // the driver exposes it; otherwise use whatever the surface requires
    // (previously this was a hard failure that forced GLES2).
    const bool identity_ok=(caps.supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)!=0;
    const int current_rot=(caps.supportedTransforms&caps.currentTransform)?rotation_of(caps.currentTransform):0;
    sc_rotation=0;
    if(current_rot&&(prerotate_wanted||!identity_ok))sc_rotation=current_rot;
    else if(!identity_ok)throw std::runtime_error("Surface has no usable transform; use GLES2");
    VkExtent2D extent=pick_extent(sc_rotation);
    if(sc_rotation){
        // The logical size derived from a rotated extent must equal SDL's drawable size.
        unsigned lw=nzp_rot_odd(sc_rotation)?extent.height:extent.width,lh=nzp_rot_odd(sc_rotation)?extent.width:extent.height;
        if((int)lw!=w||(int)lh!=h){
            SDL_Log("NZP_VULKAN prerotate_disabled extent=%ux%u drawable=%dx%d rot=%d",extent.width,extent.height,w,h,sc_rotation);
            if(identity_ok){sc_rotation=0;extent=pick_extent(0);}
        }
    }
    if(!extent.width||!extent.height)return false;
    sc_seen_transform=caps.currentTransform;sc_seen_w=caps.currentExtent.width;sc_seen_h=caps.currentExtent.height;
    width=nzp_rot_odd(sc_rotation)?extent.height:extent.width;height=nzp_rot_odd(sc_rotation)?extent.width:extent.height;
    const bool bench=requested_benchmark;
    rotation=(bench&&test_rotation>=0)?test_rotation:sc_rotation;
    phys_w=nzp_rot_odd(rotation)?height:width;phys_h=nzp_rot_odd(rotation)?width:height;
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
    ci.preTransform=sc_rotation==1?VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:sc_rotation==2?VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
        sc_rotation==3?VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if(!(caps.supportedCompositeAlpha&ci.compositeAlpha))ci.compositeAlpha=(VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha&-caps.supportedCompositeAlpha);
    ci.presentMode=present;ci.clipped=VK_TRUE;VK_CHECK(vkCreateSwapchainKHR(device,&ci,nullptr,&swapchain));
    VK_CHECK(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr));std::vector<VkImage> handles(count);
    VK_CHECK(vkGetSwapchainImagesKHR(device,swapchain,&count,handles.data()));images.resize(count);
    for(unsigned j=0;j<count;++j){auto &i=images[j];i.image=handles[j];
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=i.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
        vi.format=color_format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VK_CHECK(vkCreateImageView(device,&vi,nullptr,&i.view));
        i.depth=make_image(extent.width,extent.height,1,depth_format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        i.fb=framebuffer(i.view,i.depth.view,extent.width,extent.height,native_pass);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VK_CHECK(vkCreateSemaphore(device,&si,nullptr,&i.complete));
    }
    scale=percent;world_width=std::max(1,width*percent/100);world_height=std::max(1,height*percent/100);
    fsr_active=false;
    if(percent<100&&fsr_wanted&&!fsr_failed&&fsr_supported&&nzp_fsr::available()){
        // Quality mode follows the render ratio; Arm ASR trades shader cost for stability.
        const float ratio=float(width)/float(world_width);
        const int quality=ratio>=1.9f?3:ratio>=1.5f?2:1;
        // Creating the context compiles dozens of pipelines (seconds on Mali): reuse it across
        // swapchain rebuilds when nothing about the upscale itself changed.
        if(fsr_ctx&&(fsr_ctx_rw!=world_width||fsr_ctx_rh!=world_height||fsr_ctx_dw!=width||fsr_ctx_dh!=height||fsr_ctx_quality!=quality))
            destroy_fsr_context();
        if(!fsr_ctx){
            fsr_ctx=nzp_fsr::create(physical,device,world_width,world_height,width,height,quality);
            if(fsr_ctx){++fsr_contexts_created;fsr_ctx_rw=world_width;fsr_ctx_rh=world_height;fsr_ctx_dw=width;fsr_ctx_dh=height;fsr_ctx_quality=quality;}
        }
        if(fsr_ctx){fsr_active=true;fsr_reset=true;fsr_phase=0;fsr_slow_frames=0;fsr_frames_alive=0;
            fsr_phase_count=std::max(1,nzp_fsr::phase_count(world_width,width));
            if(!mrt_prewarmed){prewarm_pipelines(true,false);mrt_prewarmed=true;}}
        else fsr_failed=true; // fall back to bilinear for the rest of the session
    }else destroy_fsr_context();
    lite_active=false;
    if(percent<100&&upscaler_mode==2&&!lite_failed&&lite_supported){
        lite_active=true;fsr_reset=true;lite_phase=0;lite_slow_frames=0;lite_frames_alive=0;lite_history_valid=false;lite_out=0;
        // Mali (G72 etc.): texture-fetch bound, so use the 2-fetch variant unless the user asked for the full one.
        lite_fast=lite_quality==1||(lite_quality==0&&is_mali);
        if(lite_fast){if(!jitter_prewarmed){prewarm_pipelines(false,true);jitter_prewarmed=true;}}
        else if(!mrt_prewarmed){prewarm_pipelines(true,false);mrt_prewarmed=true;}
        SDL_Log("NZP_VULKAN lite mode=%s",lite_fast?"fast":"quality");
    }
    if(percent<100)for(auto &f:frames){
        const VkFormat world_format=(fsr_active||(lite_active&&!lite_fast))?FsrColorFormat:color_format;
        f.world=make_image(world_width,world_height,1,world_format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        if(fsr_active){
            f.world_depth=make_image(world_width,world_height,1,FsrDepthFormat,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
            f.world_mv=make_image(world_width,world_height,1,FsrMotionFormat,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
            f.fsr_out=make_image(width,height,1,FsrColorFormat,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
            f.world_fb=framebuffer3(f.world.view,f.world_mv.view,f.world_depth.view,world_width,world_height,fsr_pass);
            f.world_set=descriptor(texture_layout);write_image_set(f.world_set,f.fsr_out.view,upscale_sampler);
        }else if(lite_active&&!lite_fast){
            // Depth never leaves tile memory: Lite does not read it.
            f.world_depth=make_image(world_width,world_height,1,FsrDepthFormat,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
            f.world_mv=make_image(world_width,world_height,1,FsrMotionFormat,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
            f.world_fb=framebuffer3(f.world.view,f.world_mv.view,f.world_depth.view,world_width,world_height,lite_pass);
        }else{
            f.world_depth=make_image(world_width,world_height,1,depth_format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
            f.world_fb=framebuffer(f.world.view,f.world_depth.view,world_width,world_height,world_pass);
            f.world_set=descriptor(texture_layout);write_image_set(f.world_set,f.world.view,upscale_sampler);
        }
    }
    if(lite_active){
        for(int k=0;k<2;k++){
            lite_hist[k]=make_image(width,height,1,FsrColorFormat,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
            lite_fb[k]=framebuffer1(lite_hist[k].view,width,height,resolve_pass);
            lite_ui_set[k]=descriptor(texture_layout);write_image_set(lite_ui_set[k],lite_hist[k].view,upscale_sampler);
            lite_state[k]=0;
        }
        for(auto &f:frames)for(int k=0;k<2;k++){f.lite_set[k]=descriptor(lite_set_layout);write_lite_set(f.lite_set[k],f.world.view,lite_fast?f.world.view:f.world_mv.view,lite_hist[k^1].view);}
    }
    benchmark=requested_benchmark;
    if(benchmark)for(auto &f:frames){
        f.benchmark_color=make_image(phys_w,phys_h,1,color_format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        f.benchmark_depth=make_image(phys_w,phys_h,1,depth_format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        f.benchmark_fb=framebuffer(f.benchmark_color.view,f.benchmark_depth.view,phys_w,phys_h,world_pass);
    }
    current_vsync=vsync;resize_pending=false;
    SDL_Log("NZP_VULKAN surface=%dx%d world=%dx%d present=%d frames=%u images=%u rotation=%d fsr=%d",width,height,world_width,world_height,(int)present,Frames,count,rotation*90,(int)fsr_active);
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
PipelineKey make_key(const Layout &layout,unsigned mode,const nzp_vk_raster &s,int lightmapped,int alpha_test,int mrt,int jitter){
    PipelineKey key{};key.lightmapped=lightmapped;key.alpha_test=alpha_test;key.mrt=mrt;key.jitter=jitter;key.layout=layout;key.mode=mode;key.blend=s.blend;key.depth=s.depth;key.depth_write=s.depth_write;
    key.cull=s.cull;key.offset=s.offset;key.blend_src=s.blend_src;key.blend_dst=s.blend_dst;key.depth_func=s.depth_func;key.cull_face=s.cull_face;
    if(!key.blend){key.blend_src=1;key.blend_dst=0;}
    if(!key.depth){key.depth_func=0x203;key.depth_write=0;}
    if(!key.cull)key.cull_face=0x404;
    return key;
}
// Builds one pipeline. Touches only immutable objects (shader modules, layout, render
// pass) and the pipeline cache, which Vulkan synchronizes internally, so several
// threads may call it concurrently (used to prewarm on all cores).
VkPipeline create_pipeline(const Layout &layout,unsigned mode,const nzp_vk_raster &s,int lightmapped,int alpha_test,int mrt,int jitter){
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(int i=0;i<2;i++){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module=i?(mrt?fragment_shader_mrt:fragment_shader):vertex_shader;stages[i].pName="main";}
    // constant_id 0: lightmapped BSP surface, constant_id 1: alpha test.
    // Opaque pipelines are compiled without discard so Mali can use early-Z
    // and forward pixel kill; only GL_ALPHA_TEST draws pay for discard.
    // constant_id 2: motion vectors/jitter (temporal upscaler world pass only).
    VkBool32 specialized[4]={lightmapped?VK_TRUE:VK_FALSE,alpha_test?VK_TRUE:VK_FALSE,mrt?VK_TRUE:VK_FALSE,jitter?VK_TRUE:VK_FALSE};
    VkSpecializationMapEntry entry[4]={{0,0,sizeof(VkBool32)},{1,sizeof(VkBool32),sizeof(VkBool32)},{2,2*sizeof(VkBool32),sizeof(VkBool32)},{3,3*sizeof(VkBool32),sizeof(VkBool32)}};
    VkSpecializationInfo specialization{4,entry,sizeof(specialized),specialized};
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
    VkPipelineColorBlendAttachmentState attachment[2]{};attachment[0].colorWriteMask=15;attachment[0].blendEnable=s.blend;
    // Second target (motion vectors): translucent surfaces keep the vector of what is behind them.
    attachment[1].colorWriteMask=s.blend?0:(VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT);
    attachment[0].srcColorBlendFactor=attachment[0].srcAlphaBlendFactor=blend_factor(s.blend_src);
    attachment[0].dstColorBlendFactor=attachment[0].dstAlphaBlendFactor=blend_factor(s.blend_dst);
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blending.attachmentCount=mrt?2:1;blending.pAttachments=attachment;
    VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dyn.dynamicStateCount=3;dyn.pDynamicStates=dynamic;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.stageCount=2;ci.pStages=stages;
    ci.pVertexInputState=&vertex;ci.pInputAssemblyState=&assembly;ci.pViewportState=&viewport;ci.pRasterizationState=&raster;
    ci.pMultisampleState=&multisample;ci.pDepthStencilState=&depth;ci.pColorBlendState=&blending;ci.pDynamicState=&dyn;
    ci.layout=pipeline_layout;ci.renderPass=mrt?fsr_pass:native_pass;
    VkPipeline result;VK_CHECK(vkCreateGraphicsPipelines(device,pipeline_cache,1,&ci,nullptr,&result));
    return result;
}
// Looks up or creates a pipeline. Thread-safe; only the render thread (main_thread)
// uses the one-entry recent cache and reports runtime misses.
VkPipeline pipeline(const Layout &layout,unsigned mode,const nzp_vk_raster &s,int lightmapped=0,int alpha_test=0,bool main_thread=true,int mrt=0,int jitter=0){
    PipelineKey key=make_key(layout,mode,s,lightmapped,alpha_test,mrt,jitter);
    // The current material often repeats for many adjacent BSP surfaces.
    if(main_thread&&recent_pipeline&&!std::memcmp(&key,&recent_pipeline_key,sizeof(key)))return recent_pipeline;
    {std::lock_guard<std::mutex> lock(pipeline_mutex);
     auto it=pipelines.find(key);if(it!=pipelines.end()){
        if(main_thread){recent_pipeline_key=key;recent_pipeline=it->second;}
        return it->second;}}
    VkPipeline result=create_pipeline(layout,mode,s,lightmapped,alpha_test,mrt,jitter);
    size_t count;
    {std::lock_guard<std::mutex> lock(pipeline_mutex);
     auto inserted=pipelines.emplace(key,result);
     if(!inserted.second){vkDestroyPipeline(device,result,nullptr);result=inserted.first->second;} // lost a creation race
     count=pipelines.size();}
    if(main_thread){recent_pipeline_key=key;recent_pipeline=result;
        if(recording)SDL_Log("NZP_VULKAN runtime_pipeline_miss count=%zu",count);}
    return result;
}
Layout interleaved(){Layout l{};for(int i=0;i<3;i++)l.stride[i]=sizeof(nzp_vk_vertex);
 l.format[0]=VK_FORMAT_R32G32B32_SFLOAT;l.format[1]=VK_FORMAT_R32G32_SFLOAT;l.format[2]=VK_FORMAT_R8G8B8A8_UNORM;l.stride[3]=l.stride[1];l.format[3]=l.format[1];return l;}
float halton(int index,int base){float f=1.f,r=0.f;while(index>0){f/=base;r+=f*float(index%base);index/=base;}return r;}
void write_lite_set(VkDescriptorSet set,VkImageView cur,VkImageView mv,VkImageView history){
    VkDescriptorImageInfo info[3]={{upscale_sampler,cur,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{nearest_sampler,mv,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {upscale_sampler,history,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet w[3]{};
    for(int k=0;k<3;k++){w[k]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w[k].dstSet=set;w[k].dstBinding=k;w[k].descriptorCount=1;
        w[k].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w[k].pImageInfo=&info[k];}
    vkUpdateDescriptorSets(device,3,w,0,nullptr);
}
void create_lite_objects(){
    VkDescriptorSetLayoutBinding bindings[3]{};
    for(int k=0;k<3;k++){bindings[k].binding=k;bindings[k].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;bindings[k].descriptorCount=1;bindings[k].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;}
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=3;dl.pBindings=bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(device,&dl,nullptr,&lite_set_layout));
    // Both Lite shaders share one layout; always pushed with both stages (the fast vertex shader reads it too).
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,96};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&lite_set_layout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
    VK_CHECK(vkCreatePipelineLayout(device,&pl,nullptr,&lite_pipeline_layout));
    VkShaderModuleCreateInfo sh{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sh.codeSize=sizeof(nzp_vk_lite_vert);sh.pCode=nzp_vk_lite_vert;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&lite_vert));
    sh.codeSize=sizeof(nzp_vk_lite_frag);sh.pCode=nzp_vk_lite_frag;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&lite_frag));
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(int k=0;k<2;k++){stages[k]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[k].stage=k?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[k].module=k?lite_frag:lite_vert;stages[k].pName="main";}
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.lineWidth=1.f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att{};att.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&att;
    VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};ds.dynamicStateCount=2;ds.pDynamicStates=dyn;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.stageCount=2;ci.pStages=stages;ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;
    ci.pViewportState=&vp;ci.pRasterizationState=&rs;ci.pMultisampleState=&ms;ci.pColorBlendState=&cb;ci.pDynamicState=&ds;ci.layout=lite_pipeline_layout;ci.renderPass=resolve_pass;
    VK_CHECK(vkCreateGraphicsPipelines(device,pipeline_cache,1,&ci,nullptr,&lite_pipeline));
    sh.codeSize=sizeof(nzp_vk_lite_fast_vert);sh.pCode=nzp_vk_lite_fast_vert;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&lite_fast_vert));
    sh.codeSize=sizeof(nzp_vk_lite_fast_frag);sh.pCode=nzp_vk_lite_fast_frag;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&lite_fast_frag));
    stages[0].module=lite_fast_vert;stages[1].module=lite_fast_frag;
    VK_CHECK(vkCreateGraphicsPipelines(device,pipeline_cache,1,&ci,nullptr,&lite_fast_pipeline));
    nearest_sampler=sampler_for(0x2600,0x2600,0x812f,0x812f,1);
}
// Runs the Lite upscaler between the world pass and the HUD pass; the HUD quad then samples
// lite_hist[out]. Two history images alternate; every transition is explicit because they are
// shared by consecutive frames in flight (write-after-read as well as read-after-write).
void record_lite(){
    auto &f=frame();auto cb=f.draw;
    const int out=lite_out,previous=out^1;lite_out^=1;
    const Uint64 now=SDL_GetPerformanceCounter();
    float dt=last_frame_ticks?float(1000.0*(now-last_frame_ticks)/SDL_GetPerformanceFrequency()):16.6f;
    last_frame_ticks=now;dt=std::min(std::max(dt,0.5f),100.f);
    const bool have_history=!fsr_reset&&lite_history_valid;
    barrier(cb,lite_hist[out].handle,lite_state[out]?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if(lite_state[previous]==0)barrier(cb,lite_hist[previous].handle,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        0,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    else barrier(cb,lite_hist[previous].handle,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    lite_state[previous]=1;
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};bi.renderPass=resolve_pass;bi.framebuffer=lite_fb[out];
    bi.renderArea.extent={(unsigned)width,(unsigned)height};vkCmdBeginRenderPass(cb,&bi,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,lite_fast?lite_fast_pipeline:lite_pipeline);
    VkViewport viewport{0,0,float(width),float(height),0.f,1.f};VkRect2D scissor{{0,0},{(unsigned)width,(unsigned)height}};
    vkCmdSetViewport(cb,0,1,&viewport);vkCmdSetScissor(cb,0,1,&scissor);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,lite_pipeline_layout,0,1,&f.lite_set[out],0,nullptr);
    if(lite_fast){
        // Columns of the reprojection matrix (row-major input), then jitter/render size and display size/blend.
        const bool fresh=reprojection_serial==frame_serial; // the engine reported this frame's camera
        const float *r=reprojection;
        const float pc[20]={r[0],r[3],r[6],0.f, r[1],r[4],r[7],0.f, r[2],r[5],r[8],0.f,
            jitter_px[0],jitter_px[1],float(world_width),float(world_height),
            float(width),float(height),(have_history&&fresh)?0.85f:0.f,0.f};
        vkCmdPushConstants(cb,lite_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pc),pc);
    }else{
        const float pc[8]={jitter_px[0],jitter_px[1],float(world_width),float(world_height),float(width),float(height),have_history?0.9f:0.f,lite_sharpness};
        vkCmdPushConstants(cb,lite_pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pc),pc);
    }
    vkCmdDraw(cb,3,1,0,0);vkCmdEndRenderPass(cb);
    lite_state[out]=1;lite_history_valid=true;fsr_reset=false;lite_ui_current=lite_ui_set[out];
    if(++lite_frames_alive>45){
        if(dt>180.f)++lite_slow_frames;else if(lite_slow_frames>0)lite_slow_frames-=2;
        if(lite_slow_frames>=20){SDL_Log("NZP_FSR Lite frames take %.0f ms; turning it off for this session",dt);lite_failed=true;resize_pending=true;}
    }
}
// Creates the common pipeline set in parallel. mrt=true prepares the temporal-upscaler
// variants (second colour target) the first time FSR is activated.
void prewarm_pipelines(bool mrt,bool jitter){
    nzp_vk_raster s{};s.blend_src=0x302;s.blend_dst=0x303;s.depth_func=0x203;s.depth_write=1;s.cull_face=0x404;
    struct Request{Layout layout;nzp_vk_raster raster;unsigned mode;int lightmapped,alpha_test;};
    std::vector<Request> requests;
    for(int layout_index=0;layout_index<3;layout_index++){
        Layout layout=interleaved();
        if(layout_index){layout.stride[0]=layout_index==1?28:12;layout.stride[1]=layout.stride[3]=layout_index==1?28:0;layout.stride[2]=0;}
        for(int depth=0;depth<2;depth++)for(int blend=0;blend<2;blend++)for(int cull=0;cull<3;cull++){
            s.depth=depth;s.blend=blend;s.cull=cull!=0;s.cull_face=cull==2?0x405:0x404;
            for(int write=0;write<2;write++){s.depth_write=write;requests.push_back({layout,s,4,0,0});if(layout_index==1)requests.push_back({layout,s,4,1,0});}
            // GL_ALPHA_TEST sprites, fences and alias models.
            if(depth&&!blend)requests.push_back({layout,s,4,0,1});
        }
    }
    // Cold-cache pipeline compilation is the slowest part of startup on Mali.
    // Nothing is rendering yet, so let every core (including the big ones) help.
    struct Context{std::vector<Request> *requests;int mrt,jitter;std::atomic<int> failed{0};} context{&requests,mrt?1:0,jitter?1:0};
    nzp_jobs_init();nzp_jobs_set_burst(1);
    nzp_parallel_for((int)requests.size(),[](int index,void *user){
        auto *c=(Context*)user;const Request &r=(*c->requests)[index];
        try{pipeline(r.layout,r.mode,r.raster,r.lightmapped,r.alpha_test,false,c->mrt,c->jitter);}catch(...){c->failed++;}
    },&context);
    nzp_jobs_set_burst(0);
    if(context.failed)throw std::runtime_error("Pipeline prewarm failed");
}
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
    // Dynamic states survive pipeline switches, but not a new command buffer;
    // explicitly invalidate here to also cover the offscreen-to-HUD pass.
    frame().draw_state=DrawState{};
    pass_has_draw=false;std::memcpy(pass_clear_color,clear_color,16);pass_clear_depth=clear_depth;
    const bool motion_pass=!native&&(fsr_active||(lite_active&&!lite_fast));
    VkClearValue values[3]{};std::memcpy(values[0].color.float32,clear_color,16);
    if(motion_pass)values[2].depthStencil.depth=clear_depth;else values[1].depthStencil.depth=clear_depth;
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass=motion_pass?(fsr_active?fsr_pass:lite_pass):(native&&!benchmark)?native_pass:world_pass;
    bi.framebuffer=native?(benchmark?frame().benchmark_fb:images[image_index].fb):frame().world_fb;bi.renderArea.extent={(unsigned)(native?phys_w:world_width),(unsigned)(native?phys_h:world_height)};
    bi.clearValueCount=motion_pass?3:2;bi.pClearValues=values;vkCmdBeginRenderPass(frame().draw,&bi,VK_SUBPASS_CONTENTS_INLINE);in_pass=true;
    // Push constants: vertex-shader pre-rotation (only for passes that target the rotated
    // swapchain/benchmark image; the offscreen world image stays logical) and the
    // temporal upscaler's jitter (NDC, y down) and render size.
    struct {float rot[4];float jitter_size[4];} pc{};nzp_rot_matrix(native?rotation:0,pc.rot);
    if(motion_pass||(!native&&lite_active&&lite_fast)){pc.jitter_size[0]=2.f*jitter_px[0]/world_width;pc.jitter_size[1]=2.f*jitter_px[1]/world_height;
        pc.jitter_size[2]=float(world_width);pc.jitter_size[3]=float(world_height);}
    vkCmdPushConstants(frame().draw,pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pc),&pc);
}
void bind_draw(const Layout &layout,unsigned mode,const nzp_vk_uniform &uniform_in,const nzp_vk_raster &state,VkDescriptorSet texture,VkDescriptorSet lightmap=VK_NULL_HANDLE){
    pass_has_draw=true;
    auto &f=frame();auto &cached=f.draw_state;auto cb=f.draw;
    const bool motion_draw=(fsr_active||(lite_active&&!lite_fast))&&!ui&&scale<100;
    const bool jitter_draw=lite_active&&lite_fast&&!ui&&scale<100;
    // Callers that do not track history (tests, UI) leave prev_mvp zero: treat as "no motion".
    nzp_vk_uniform with_history;const nzp_vk_uniform *uniform_ptr=&uniform_in;
    if(motion_draw){static const float zero[16]{};
        if(!std::memcmp(uniform_in.prev_mvp,zero,sizeof(zero))){with_history=uniform_in;std::memcpy(with_history.prev_mvp,uniform_in.mvp,64);uniform_ptr=&with_history;}}
    const nzp_vk_uniform &uniform=*uniform_ptr;
    const VkPipeline wanted=pipeline(layout,mode,state,lightmap!=VK_NULL_HANDLE,uniform.params[2]>=0.f,true,motion_draw?1:0,jitter_draw?1:0);
    if(cached.pipeline!=wanted){
        vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,wanted);
        cached.pipeline=wanted;++f.stats.pipeline_binds;
    }
    const bool offscreen=!ui&&scale<100;
    float xs=offscreen?float(world_width)/width:1.f,ys=offscreen?float(world_height)/height:1.f;
    int target_h=offscreen?world_height:height;
    VkViewport viewport{state.viewport[0]*xs,target_h-(state.viewport[1]+state.viewport[3])*ys,
        state.viewport[2]*xs,state.viewport[3]*ys,state.depth_near,state.depth_far};
    VkRect2D scissor{{0,0},{(unsigned)(offscreen?world_width:phys_w),(unsigned)(offscreen?world_height:phys_h)}};
    if(!offscreen&&rotation){
        float r[4];nzp_rot_rect(rotation,viewport.x,viewport.y,viewport.width,viewport.height,float(width),float(height),r);
        viewport.x=r[0];viewport.y=r[1];viewport.width=r[2];viewport.height=r[3];
    }
    if(!cached.viewport_valid||std::memcmp(&cached.viewport,&viewport,sizeof(viewport))){
        vkCmdSetViewport(cb,0,1,&viewport);
        cached.viewport=viewport;cached.viewport_valid=true;++f.stats.viewport_sets;
    }
    if(!cached.scissor_valid||std::memcmp(&cached.scissor,&scissor,sizeof(scissor))){
        vkCmdSetScissor(cb,0,1,&scissor);
        cached.scissor=scissor;cached.scissor_valid=true;++f.stats.scissor_sets;
    }
    // No depth-bias dynamic state is needed for pipelines with depth bias disabled.
    if(state.offset&&(!cached.bias_valid||cached.bias_units!=state.offset_units||cached.bias_factor!=state.offset_factor)){
        vkCmdSetDepthBias(cb,state.offset_units,0,state.offset_factor);
        cached.bias_units=state.offset_units;cached.bias_factor=state.offset_factor;
        cached.bias_valid=true;++f.stats.depth_bias_sets;
    }
    // Consecutive material draws typically share an MVP/fog block. Keep its
    // dynamic UBO offset alive for this command buffer instead of copying it.
    uint32_t offset;
    if(cached.uniform_valid&&!std::memcmp(&cached.uniform,&uniform,sizeof(uniform))){
        offset=cached.uniform_offset;++f.stats.uniform_reuses;
    }else{
        size_t uoffset=allocate(sizeof(uniform),std::max<size_t>(16,properties.limits.minUniformBufferOffsetAlignment));
        std::memcpy((char*)f.arena.mapped+uoffset,&uniform,sizeof(uniform));
        offset=static_cast<uint32_t>(uoffset);
        cached.uniform=uniform;cached.uniform_offset=offset;cached.uniform_valid=true;
        ++f.stats.uniform_uploads;
    }
    VkDescriptorSet sets[]={f.uniform_set,texture,lightmap?lightmap:texture};
    if(!cached.sets_valid||cached.bound_offset!=offset||std::memcmp(cached.sets,sets,sizeof(sets))){
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout,0,3,sets,1,&offset);
        std::memcpy(cached.sets,sets,sizeof(sets));
        cached.bound_offset=offset;cached.sets_valid=true;++f.stats.descriptor_binds;
    }
    ++f.stats.draws;
}
void submit(){
    auto &f=frame();if(in_pass){vkCmdEndRenderPass(f.draw);in_pass=false;}
    if(capture_requested&&(benchmark||can_capture)){
        size_t bytes=size_t(phys_w)*phys_h*4;
        if(readback.size<bytes){destroy(readback);readback=make_buffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);}
        VkImage image=benchmark?f.benchmark_color.handle:images[image_index].image;
        VkImageLayout layout=benchmark?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier(f.draw,image,layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={(unsigned)phys_w,(unsigned)phys_h,1};
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
    VK_CHECK(vkResetFences(device,1,&f.fence));
    const Uint64 submit_start=SDL_GetPerformanceCounter();
    VK_CHECK(vkQueueSubmit(queue,1,&si,f.fence));
    f.timings.queue_submit_ms=1000.0*(SDL_GetPerformanceCounter()-submit_start)/SDL_GetPerformanceFrequency();
    f.submitted=true;recording=false;
}
}

int NZP_VK_Init(SDL_Window *w,const char *directory,int enable_validation){
    try{
        window=w;validation_errors=0;{const char *e=std::getenv("NZP_VK_PREROTATE");if(e&&e[0]=='0')prerotate_wanted=false;}
        cache_path=std::string(directory?directory:".")+"/vulkan-pipeline-cache.bin";
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
        // Optional capabilities of the temporal upscaler (Arm ASR). Its prebuilt Vulkan shaders are
        // all 16-bit HLSL->SPIR-V 1.4, so it needs: fp16 + int16 shader arithmetic, SPIR-V 1.4
        // (VK_KHR_spirv_1_4 and its VK_KHR_shader_float_controls dependency), independent blend for
        // the motion vector target and storage writes in fragment shaders. Anything missing simply
        // leaves FSR unavailable and the renderer on the bilinear upscale.
        std::vector<const char*> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceShaderFloat16Int8Features float16_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
        VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures subgroup_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};
        float16_supported.pNext=&subgroup_supported;
        VkPhysicalDeviceFeatures2 features_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features_supported.pNext=&float16_supported;
        vkGetPhysicalDeviceFeatures2(physical,&features_supported);
        bool has_float16=false,has_spirv14=false,has_float_controls=false,has_subgroup_types=false;
        {uint32_t n=0;vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,nullptr);std::vector<VkExtensionProperties> list(n);
         vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,list.data());
         for(auto &e:list){
            has_float16|=!std::strcmp(e.extensionName,VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
            has_spirv14|=!std::strcmp(e.extensionName,VK_KHR_SPIRV_1_4_EXTENSION_NAME);
            has_float_controls|=!std::strcmp(e.extensionName,VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
            has_subgroup_types|=!std::strcmp(e.extensionName,VK_KHR_SHADER_SUBGROUP_EXTENDED_TYPES_EXTENSION_NAME);}}
        const auto &f=features_supported.features;
        fsr_supported=f.independentBlend&&f.fragmentStoresAndAtomics&&f.shaderInt16&&has_float16&&float16_supported.shaderFloat16&&has_spirv14&&has_float_controls&&has_subgroup_types&&subgroup_supported.shaderSubgroupExtendedTypes;
        // FSR Lite only needs a second colour target with its own write mask.
        lite_supported=f.independentBlend;
        SDL_Log("NZP_VULKAN fsr_supported=%d independentBlend=%d fragmentStoresAndAtomics=%d int16=%d float16=%d/%d spirv1_4=%d float_controls=%d subgroup_ext_types=%d",
                (int)fsr_supported,(int)f.independentBlend,(int)f.fragmentStoresAndAtomics,(int)f.shaderInt16,(int)has_float16,(int)float16_supported.shaderFloat16,(int)has_spirv14,(int)has_float_controls,(int)subgroup_supported.shaderSubgroupExtendedTypes);
        VkPhysicalDeviceShaderFloat16Int8Features float16_enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
        VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures subgroup_enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES};
        VkPhysicalDeviceFeatures2 features_enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        if(lite_supported)features_enabled.features.independentBlend=VK_TRUE;
        if(fsr_supported){
            features_enabled.features.independentBlend=features_enabled.features.fragmentStoresAndAtomics=features_enabled.features.shaderInt16=VK_TRUE;
            float16_enabled.shaderFloat16=VK_TRUE;features_enabled.pNext=&float16_enabled;
            subgroup_enabled.shaderSubgroupExtendedTypes=VK_TRUE;float16_enabled.pNext=&subgroup_enabled;
            device_extensions.push_back(VK_KHR_SHADER_SUBGROUP_EXTENDED_TYPES_EXTENSION_NAME);
            device_extensions.push_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
            device_extensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
            device_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
        }
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qi;
        dc.pNext=&features_enabled;dc.enabledExtensionCount=(uint32_t)device_extensions.size();dc.ppEnabledExtensionNames=device_extensions.data();
        VK_CHECK(vkCreateDevice(physical,&dc,nullptr,&device));
        {VkPhysicalDeviceProperties gpu_properties;vkGetPhysicalDeviceProperties(physical,&gpu_properties);
         is_mali=gpu_properties.vendorID==0x13B5; // ARM
         SDL_Log("NZP_VULKAN gpu=%s vendor=0x%x mali=%d",gpu_properties.deviceName,gpu_properties.vendorID,(int)is_mali);}volkLoadDevice(device);vkGetDeviceQueue(device,family,0,&queue);
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
        native_pass=make_pass(false);world_pass=make_pass(true);fsr_pass=make_fsr_pass(true);lite_pass=make_fsr_pass(false);resolve_pass=make_resolve_pass();
        VkDescriptorSetLayoutBinding ub{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lc.bindingCount=1;lc.pBindings=&ub;
        VK_CHECK(vkCreateDescriptorSetLayout(device,&lc,nullptr,&uniform_layout));
        VkDescriptorSetLayoutBinding tb{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};lc.pBindings=&tb;
        VK_CHECK(vkCreateDescriptorSetLayout(device,&lc,nullptr,&texture_layout));
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,Frames},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16384}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;pc.maxSets=16384+Frames;
        pc.poolSizeCount=2;pc.pPoolSizes=sizes;VK_CHECK(vkCreateDescriptorPool(device,&pc,nullptr,&descriptors));
        VkDescriptorSetLayout layouts[]={uniform_layout,texture_layout,texture_layout};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=3;pl.pSetLayouts=layouts;
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,32};pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
        VK_CHECK(vkCreatePipelineLayout(device,&pl,nullptr,&pipeline_layout));
        VkShaderModuleCreateInfo sh{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sh.codeSize=sizeof(nzp_vk_vert);sh.pCode=nzp_vk_vert;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&vertex_shader));
        sh.codeSize=sizeof(nzp_vk_frag);sh.pCode=nzp_vk_frag;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&fragment_shader));
        sh.codeSize=sizeof(nzp_vk_frag_mrt);sh.pCode=nzp_vk_frag_mrt;VK_CHECK(vkCreateShaderModule(device,&sh,nullptr,&fragment_shader_mrt));
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
        create_lite_objects();
        // Prewarm common interleaved states. Unseen vertex layouts are logged misses.
        nzp_vk_raster s{};s.blend_src=0x302;s.blend_dst=0x303;s.depth_func=0x203;s.depth_write=1;s.cull_face=0x404;
        prewarm_pipelines(false,false);
        active=true;recreate(1,100);
        SDL_Log("NZP_VULKAN device=%s api=%u.%u driver=%u",properties.deviceName,VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),properties.driverVersion);
        return 1;
    }catch(const std::exception &e){SDL_LogError(SDL_LOG_CATEGORY_RENDER,"Vulkan initialization failed: %s",e.what());NZP_VK_Shutdown();return 0;}
}
int NZP_VK_Active(){return active;}
void NZP_VK_NotifySurfaceLost(){surface_lost=true;resize_pending=true;}
void NZP_VK_SetPreRotation(int enabled){if(prerotate_wanted!=bool(enabled)){prerotate_wanted=enabled;resize_pending=true;}}
void NZP_VK_SetTestRotation(int quarter_turns){if(test_rotation!=quarter_turns){test_rotation=quarter_turns;resize_pending=true;}}
int NZP_VK_GetRotation(void){return rotation*90;}
// mode: 0 off, 1 Arm ASR (full FSR 2.2 class), 2 Lite (cheap single pass, any GPU with independent blend).
void NZP_VK_SetUpscaler(int mode){
    if(mode<0||mode>2)mode=0;
    if(upscaler_mode!=mode){upscaler_mode=mode;fsr_wanted=mode==1;fsr_failed=false;lite_failed=false;resize_pending=true;}
}
void NZP_VK_SetFSR(int enabled){NZP_VK_SetUpscaler(enabled?1:0);}
int NZP_VK_FSRActive(void){return fsr_active;}
int NZP_VK_FSRContextsCreated(void){return fsr_contexts_created;}
int NZP_VK_FSRSupported(void){return fsr_supported&&nzp_fsr::available();}
// 0 off, 1 running, 2 unsupported on this device, 3 turned itself off (failure/too slow), 4 on but render scale is 100%
int NZP_VK_FSRState(void){
    if(!fsr_supported||!nzp_fsr::available())return 2;
    if(!fsr_wanted)return 0;
    if(fsr_failed)return 3;
    return fsr_active?1:4;
}
int NZP_VK_UpscalerSupported(int mode){return mode==1?(fsr_supported&&nzp_fsr::available()):mode==2?lite_supported:1;}
// State of the selected mode: 0 off, 1 running, 2 unsupported, 3 turned itself off, 4 waiting for render scale < 100%.
int NZP_VK_UpscalerState(void){
    if(upscaler_mode==0)return 0;
    if(upscaler_mode==1)return NZP_VK_FSRState();
    if(!lite_supported)return 2;
    if(lite_failed)return 3;
    return lite_active?1:4;
}
void NZP_VK_SetLiteQuality(int quality){if(quality<0||quality>2)quality=0;if(lite_quality!=quality){lite_quality=quality;if(lite_active)resize_pending=true;}}
// Row-major 3x3 mapping this frame's Vulkan NDC (x,y,1) to the previous frame's homogeneous NDC for a
// rotation-only camera change. Reported once per rendered frame by the engine.
void NZP_VK_SetReprojection(const float *m){std::memcpy(reprojection,m,sizeof(reprojection));reprojection_serial=frame_serial;}
void NZP_VK_SetLiteSharpness(float value){lite_sharpness=std::min(std::max(value,0.f),1.f);}
void NZP_VK_SetCamera(float near_plane,float far_plane,float fov_y){cam_near=near_plane;cam_far=far_plane;cam_fov=fov_y;}
void NZP_VK_ResetHistory(void){fsr_reset=true;}
void NZP_VK_SetFSRSharpness(float value){fsr_sharpness=std::min(std::max(value,0.f),1.f);}
unsigned long long NZP_VK_FrameSerial(void){return frame_serial;}
void NZP_VK_SetBenchmark(int enabled){if(requested_benchmark!=bool(enabled)){requested_benchmark=enabled;resize_pending=true;}}
unsigned long long NZP_VK_CompletedFrames(){return completed_frames;}
void NZP_VK_Finish(){
    if(!device)return;VK_CHECK(vkDeviceWaitIdle(device));
    for(auto &f:frames)if(f.submitted){++completed_frames;f.submitted=false;}
}
int NZP_VK_BeginFrame(int vsync,int percent){
    if(!active)return 0;auto &f=frame();
    const Uint64 wait_start=SDL_GetPerformanceCounter();
    VK_CHECK(vkWaitForFences(device,1,&f.fence,VK_TRUE,UINT64_MAX));
    const double wait_ms=1000.0*(SDL_GetPerformanceCounter()-wait_start)/SDL_GetPerformanceFrequency();
    f.timings={};f.timings.fence_wait_ms=wait_ms;
    bool was_submitted=f.submitted;
    if(was_submitted){++completed_frames;f.submitted=false;}
    if(was_submitted&&f.queries){uint64_t times[2]{};if(vkGetQueryPoolResults(device,f.queries,0,2,sizeof(times),times,8,VK_QUERY_RESULT_64_BIT)==VK_SUCCESS)
        gpu_ms=((times[1]-times[0])&(timestamp_bits<64?((uint64_t(1)<<timestamp_bits)-1):UINT64_MAX))*properties.limits.timestampPeriod/1e6;}
    f.textures.clear();f.meshes.clear();f.used=0;f.staging_index=0;f.stats={};f.draw_state={};
    // A loading scene can allocate many staging buffers. On the next safe
    // fence completion, release them after a sustained idle period instead
    // of keeping peak map-loading memory for the entire game session.
    bool used_staging=false;
    for(size_t n:f.staging_used)if(n){used_staging=true;break;}
    if(used_staging)f.staging_idle_frames=0;
    else if(!f.staging.empty() && ++f.staging_idle_frames>=StagingIdleFrameLimit){
        for(auto &b:f.staging)destroy(b);
        f.staging.clear();f.staging_used.clear();f.staging_idle_frames=0;
    }
    std::fill(f.staging_used.begin(),f.staging_used.end(),0);
    int w,h;SDL_Vulkan_GetDrawableSize(window,&w,&h);
    if(w<=0||h<=0)return 0;
    if(surface_lost)restore_surface();
    if(resize_pending||w!=drawable_w||h!=drawable_h||vsync!=current_vsync||percent!=scale)if(!recreate(vsync,percent))return 0;
    VkResult r=VK_SUCCESS;
    const Uint64 acquire_start=SDL_GetPerformanceCounter();
    if(!benchmark)r=vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,f.acquired,VK_NULL_HANDLE,&image_index);
    f.timings.acquire_ms=1000.0*(SDL_GetPerformanceCounter()-acquire_start)/SDL_GetPerformanceFrequency();
    if(r==VK_ERROR_SURFACE_LOST_KHR){surface_lost=true;resize_pending=true;return 0;}
    if(r==VK_ERROR_OUT_OF_DATE_KHR){resize_pending=true;return 0;}if(r!=VK_SUBOPTIMAL_KHR)VK_CHECK(r);else note_suboptimal();
    VK_CHECK(vkResetCommandPool(device,f.pool,0));VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(f.upload,&bi));VK_CHECK(vkBeginCommandBuffer(f.draw,&bi));
    if(f.queries){vkCmdResetQueryPool(f.draw,f.queries,0,2);vkCmdWriteTimestamp(f.draw,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,f.queries,0);}
    ++frame_serial;
    if(fsr_active){nzp_fsr::jitter(fsr_phase%fsr_phase_count,fsr_phase_count,&jitter_px[0],&jitter_px[1]);++fsr_phase;}
    else if(lite_active){const int phase=lite_phase++%16+1;jitter_px[0]=halton(phase,2)-0.5f;jitter_px[1]=halton(phase,3)-0.5f;}
    else jitter_px[0]=jitter_px[1]=0.f;
    recording=true;ui=false;begin_pass(scale==100);return 1;
}
// Runs Arm ASR between the world pass and the HUD pass. Output lands in fsr_out, which the
// full-screen quad of the HUD pass samples instead of the raw world image.
void record_fsr(){
    auto &f=frame();auto cb=f.draw;
    const Uint64 now=SDL_GetPerformanceCounter();
    float dt=last_frame_ticks?float(1000.0*(now-last_frame_ticks)/SDL_GetPerformanceFrequency()):16.6f;
    last_frame_ticks=now;dt=std::min(std::max(dt,0.5f),100.f);
    barrier(cb,f.fsr_out.handle,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    nzp_fsr::Inputs in{cb,f.world.handle,f.world_depth.handle,f.world_mv.handle,f.fsr_out.handle,world_width,world_height,
        jitter_px[0],jitter_px[1],dt,cam_near,cam_far,cam_fov,fsr_sharpness,fsr_reset};
    if(!nzp_fsr::dispatch(fsr_ctx,in)){
        // Fall back to the bilinear path from the next frame on.
        SDL_Log("NZP_FSR dispatch failed; disabling temporal upscaling");fsr_failed=true;resize_pending=true;
    }
    fsr_reset=false;
    // Safety net: if temporal upscaling makes the game unplayable on this GPU, switch it off
    // instead of leaving the player at a few frames per second.
    if(++fsr_frames_alive>45){
        if(dt>180.f)++fsr_slow_frames;else if(fsr_slow_frames>0)fsr_slow_frames-=2;
        if(fsr_slow_frames>=20){
            SDL_Log("NZP_FSR frames take %.0f ms with temporal upscaling; turning it off for this session",dt);
            fsr_failed=true;resize_pending=true;
        }
    }
    barrier(cb,f.fsr_out.handle,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}
void NZP_VK_BeginUI(){
    if(!recording||ui)return;
    if(scale<100){vkCmdEndRenderPass(frame().draw);in_pass=false;if(fsr_active)record_fsr();else if(lite_active)record_lite();ui=true;begin_pass(true);
        nzp_vk_uniform u{};for(int i=0;i<4;i++)u.mvp[i*5]=u.mv[i*5]=u.tm[i*5]=1;
        u.params[0]=u.params[1]=1;u.params[2]=-1;
        nzp_vk_raster s{};s.blend_src=1;s.blend_dst=0;s.depth_func=0x203;s.depth_far=1;s.viewport[2]=width;s.viewport[3]=height;
        // The upscaler's outermost output pixels lose energy (kernel support runs off the image),
        // so sample one display pixel inside the border. Invisible, and avoids a dark frame.
        const float iu=fsr_active?1.f/width:0.f,iv=fsr_active?1.f/height:0.f;
        const float u0=iu,u1=1.f-iu,v0=iv,v1=1.f-iv;
        const nzp_vk_vertex quad[]={{{-1,-1,0},{u0,v1},{255,255,255,255}},{{1,-1,0},{u1,v1},{255,255,255,255}},{{1,1,0},{u1,v0},{255,255,255,255}},
            {{-1,-1,0},{u0,v1},{255,255,255,255}},{{1,1,0},{u1,v0},{255,255,255,255}},{{-1,1,0},{u0,v0},{255,255,255,255}}};
        size_t offset=allocate(sizeof(quad),4);std::memcpy((char*)frame().arena.mapped+offset,quad,sizeof(quad));
        VkBuffer buffers[]={frame().arena.handle,frame().arena.handle,frame().arena.handle,frame().arena.handle};VkDeviceSize offsets[]={offset,offset+12,offset+20,offset+12};
        bind_draw(interleaved(),4,u,s,lite_active?lite_ui_current:frame().world_set);vkCmdBindVertexBuffers(frame().draw,0,4,buffers,offsets);vkCmdDraw(frame().draw,6,1,0,0);
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
            // y is bottom-left (GL); the physical image is top-left and possibly pre-rotated.
            int px,py;nzp_rot_pixel(rotation,x,height-1-y,width,height,&px,&py);
            auto *out=captured.data()+(size_t(y)*width+x)*4;const auto *p=src+(size_t(py)*phys_w+px)*4;
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
    const Uint64 present_start=SDL_GetPerformanceCounter();
    auto r=benchmark?VK_SUCCESS:vkQueuePresentKHR(queue,&pi);
    frame().timings.present_ms=benchmark?0:1000.0*(SDL_GetPerformanceCounter()-present_start)/SDL_GetPerformanceFrequency();
    if(r==VK_ERROR_SURFACE_LOST_KHR){surface_lost=true;resize_pending=true;}
    else if(r==VK_ERROR_OUT_OF_DATE_KHR)resize_pending=true;else if(r==VK_SUBOPTIMAL_KHR)note_suboptimal();else VK_CHECK(r);
    last_command_stats=frame().stats;last_frame_timings=frame().timings;
    frame_index=(frame_index+1)%Frames;
}
void NZP_VK_Draw(const nzp_vk_vertex *v,int count,unsigned mode,const nzp_vk_uniform *u,const nzp_vk_raster *s,unsigned tex){
    if(!recording||!count)return;auto gpu=material(tex);
    if(frame().textures.empty() || frame().textures.back()!=gpu)frame().textures.push_back(gpu);
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
    auto gpu=material(tex);if(frame().textures.empty() || frame().textures.back()!=gpu)frame().textures.push_back(gpu);nzp_vk_uniform uniforms=*u;uniforms.params[3]=lm?1.f:0.f;
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
    // RGBA8 is already in the native upload format. Copy contiguous rows
    // instead of calling memcpy four bytes at a time for every texel.
    if(type==0x1401 && format==0x1908 && pixels){
        const size_t bytes=size_t(w)*4;
        if(x==0 && y==0 && w==m.w && h==m.h && row==bytes)
            std::memcpy(m.rgba.data(),pixels,bytes*size_t(h));
        else for(int j=0;j<h;j++)
            std::memcpy(m.rgba.data()+(size_t(y+j)*m.w+x)*4,
                        (const unsigned char*)pixels+size_t(j)*row,bytes);
        t.dirty=t.pixel_dirty=true;
        return;
    }
    if(!pixels && x==0 && y==0 && w==m.w && h==m.h){
        std::memset(m.rgba.data(),0,m.rgba.size());
        t.dirty=t.pixel_dirty=true;
        return;
    }
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
    if(n){VkClearRect r{{{0,0},{(unsigned)(!ui&&scale<100?world_width:phys_w),(unsigned)(!ui&&scale<100?world_height:phys_h)}},0,1};vkCmdClearAttachments(frame().draw,n,a,1,&r);}
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
void NZP_VK_GetCommandStats(nzp_vk_command_stats *out){if(out)*out=device?(recording?frame().stats:last_command_stats):nzp_vk_command_stats{};}
void NZP_VK_GetFrameTimings(nzp_vk_frame_timings *out){if(out)*out=device?last_frame_timings:nzp_vk_frame_timings{};}
void NZP_VK_Shutdown(){
    active=false;
    if(device){vkDeviceWaitIdle(device);
        if(pipeline_cache){size_t size=0;if(vkGetPipelineCacheData(device,pipeline_cache,&size,nullptr)==VK_SUCCESS&&size<32*1024*1024){std::vector<unsigned char> data(size);
            if(vkGetPipelineCacheData(device,pipeline_cache,&size,data.data())==VK_SUCCESS){std::string temp=cache_path+".tmp";FILE *f=std::fopen(temp.c_str(),"wb");if(f){bool ok=std::fwrite(data.data(),1,size,f)==size;std::fclose(f);if(ok)std::rename(temp.c_str(),cache_path.c_str());}}}}
        destroy(readback);captured.clear();capture_requested=false;screenshot_path.clear();
        destroy_fsr_context();destroy_targets();for(auto &f:frames){f.textures.clear();f.meshes.clear();}textures.clear();meshes.clear();
        for(auto &p:pipelines)vkDestroyPipeline(device,p.second,nullptr);pipelines.clear();
        recent_pipeline=VK_NULL_HANDLE;recent_pipeline_key={};
        for(auto &f:frames){for(auto &b:f.staging)destroy(b);destroy(f.arena);if(f.queries)vkDestroyQueryPool(device,f.queries,nullptr);if(f.pool)vkDestroyCommandPool(device,f.pool,nullptr);
            if(f.fence)vkDestroyFence(device,f.fence,nullptr);if(f.acquired)vkDestroySemaphore(device,f.acquired,nullptr);f={};}
        if(upscale_sampler)vkDestroySampler(device,upscale_sampler,nullptr);if(nearest_sampler)vkDestroySampler(device,nearest_sampler,nullptr);
        if(vertex_shader)vkDestroyShaderModule(device,vertex_shader,nullptr);if(fragment_shader)vkDestroyShaderModule(device,fragment_shader,nullptr);if(fragment_shader_mrt)vkDestroyShaderModule(device,fragment_shader_mrt,nullptr);
        if(lite_pipeline)vkDestroyPipeline(device,lite_pipeline,nullptr);if(lite_fast_pipeline)vkDestroyPipeline(device,lite_fast_pipeline,nullptr);
        if(lite_fast_vert)vkDestroyShaderModule(device,lite_fast_vert,nullptr);if(lite_fast_frag)vkDestroyShaderModule(device,lite_fast_frag,nullptr);if(lite_pipeline_layout)vkDestroyPipelineLayout(device,lite_pipeline_layout,nullptr);
        if(lite_set_layout)vkDestroyDescriptorSetLayout(device,lite_set_layout,nullptr);
        if(lite_vert)vkDestroyShaderModule(device,lite_vert,nullptr);if(lite_frag)vkDestroyShaderModule(device,lite_frag,nullptr);
        if(pipeline_cache)vkDestroyPipelineCache(device,pipeline_cache,nullptr);if(pipeline_layout)vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        if(descriptors)vkDestroyDescriptorPool(device,descriptors,nullptr);if(uniform_layout)vkDestroyDescriptorSetLayout(device,uniform_layout,nullptr);if(texture_layout)vkDestroyDescriptorSetLayout(device,texture_layout,nullptr);
        if(native_pass)vkDestroyRenderPass(device,native_pass,nullptr);if(world_pass)vkDestroyRenderPass(device,world_pass,nullptr);if(fsr_pass)vkDestroyRenderPass(device,fsr_pass,nullptr);if(lite_pass)vkDestroyRenderPass(device,lite_pass,nullptr);if(resolve_pass)vkDestroyRenderPass(device,resolve_pass,nullptr);
        if(allocator)vmaDestroyAllocator(allocator);vkDestroyDevice(device,nullptr);}
    if(debug)vkDestroyDebugUtilsMessengerEXT(instance,debug,nullptr);if(surface)vkDestroySurfaceKHR(instance,surface,nullptr);
    if(instance)vkDestroyInstance(instance,nullptr);
    instance={};physical={};device={};surface={};allocator={};debug={};swapchain={};
    descriptors={};uniform_layout={};texture_layout={};pipeline_layout={};pipeline_cache={};vertex_shader={};fragment_shader={};fragment_shader_mrt={};native_pass={};world_pass={};fsr_pass={};lite_pass={};resolve_pass={};lite_vert={};lite_frag={};lite_set_layout={};lite_pipeline_layout={};lite_pipeline={};lite_fast_pipeline={};lite_fast_vert={};lite_fast_frag={};nearest_sampler={};lite_fast=is_mali=jitter_prewarmed=false;lite_active=lite_failed=lite_supported=false;upscaler_mode=0;upscale_sampler={};fsr_active=fsr_failed=fsr_supported=mrt_prewarmed=false;fsr_reset=true;last_frame_ticks=0;
    last_command_stats={};last_frame_timings={};
    frame_index=0;recording=in_pass=ui=resize_pending=false;benchmark=requested_benchmark=surface_lost=false;completed_frames=0;current_vsync=-1;width=height=phys_w=phys_h=drawable_w=drawable_h=rotation=sc_rotation=0;gpu_ms=-1;color_format=depth_format=VK_FORMAT_UNDEFINED;
    SDL_Vulkan_UnloadLibrary();
}
