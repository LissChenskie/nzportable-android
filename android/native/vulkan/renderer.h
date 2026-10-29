// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
struct SDL_Window;
#ifdef __cplusplus
extern "C" {
#endif
/* Shared Vril render frontend sends native render data to this backend.
 * No GL context, GL driver calls or GL-on-Vulkan library is involved. */
typedef struct {float xyz[3],uv[2]; unsigned char color[4];} nzp_vk_vertex;
typedef struct {
    float mvp[16],mv[16],tm[16],fog[4],fogcolor[4],params[4];
} nzp_vk_uniform;
typedef struct {
    int blend,depth,cull,offset;
    unsigned blend_src,blend_dst,depth_func,cull_face;
    int depth_write;
    float depth_near,depth_far,offset_factor,offset_units;
    int viewport[4];
} nzp_vk_raster;
typedef struct {unsigned buffer;size_t offset;int size;unsigned type;int stride;} nzp_vk_array;
/* Command counts describe CPU-side Vulkan recording, not GPU time or FPS. */
typedef struct {
    unsigned draws,pipeline_binds,viewport_sets,scissor_sets,depth_bias_sets;
    unsigned uniform_uploads,uniform_reuses,descriptor_binds;
} nzp_vk_command_stats;
/* CPU-side API blocking durations only. Excludes GPU execution and display. */
typedef struct {
    double fence_wait_ms,acquire_ms,queue_submit_ms,present_ms;
} nzp_vk_frame_timings;
int NZP_VK_Init(struct SDL_Window *window,const char *cache_directory,int validation);
void NZP_VK_Shutdown(void);
int NZP_VK_Active(void);
void NZP_VK_NotifySurfaceLost(void);
void NZP_VK_SetBenchmark(int enabled);
void NZP_VK_Finish(void);
unsigned long long NZP_VK_CompletedFrames(void);
int NZP_VK_BeginFrame(int vsync,int percent);
void NZP_VK_BeginUI(void);
void NZP_VK_EndFrame(void);
void NZP_VK_Draw(const nzp_vk_vertex *,int count,unsigned mode,
                 const nzp_vk_uniform *,const nzp_vk_raster *,unsigned texture);
int NZP_VK_DrawBuffers(const nzp_vk_array *,unsigned index_buffer,size_t offset,
                       int count,unsigned mode,const nzp_vk_uniform *,
                       const nzp_vk_raster *,unsigned texture,
                       const float *uv,const unsigned char *color);
int NZP_VK_DrawLightmappedBuffers(const nzp_vk_array *arrays,const nzp_vk_array *lightmap_uv,
    unsigned index_buffer,size_t offset,int count,const nzp_vk_uniform *,const nzp_vk_raster *,
    unsigned base_texture,unsigned lightmap_texture,const float *uv,const unsigned char *color);
void NZP_VK_Texture(unsigned id,int level,int x,int y,int w,int h,
                     unsigned format,unsigned type,const void *pixels,int replace,int alignment);
void NZP_VK_TextureFilter(unsigned id,unsigned name,int value);
void NZP_VK_DeleteTexture(unsigned id);
void NZP_VK_Buffer(unsigned id,size_t bytes,const void *data,int is_static);
void NZP_VK_DeleteBuffer(unsigned id);
void NZP_VK_Clear(unsigned mask,const float *color,float depth);
void NZP_VK_RequestCapture(const char *tga_path);
int NZP_VK_ReadPixels(int x,int y,int w,int h,void *rgba);
void NZP_VK_GetSize(int *w,int *h);
unsigned NZP_VK_MaxTextureSize(void);
double NZP_VK_GPUTime(void);
unsigned NZP_VK_ValidationErrors(void);
void NZP_VK_GetCommandStats(nzp_vk_command_stats *out);
void NZP_VK_GetFrameTimings(nzp_vk_frame_timings *out);
#ifdef __cplusplus
}
#endif
