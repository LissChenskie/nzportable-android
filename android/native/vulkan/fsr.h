// SPDX-License-Identifier: GPL-2.0-or-later
// Thin wrapper around Arm Accuracy Super Resolution (Arm ASR), the Mali-oriented
// derivative of AMD FidelityFX Super Resolution 2.2 (the temporal upscaler that
// FSR 3 builds on). Both are MIT licensed. Frame generation is NOT included.
// Without NZP_FSR every function is a stub that reports "unavailable".
#pragma once
#include "vendor/volk.h"
namespace nzp_fsr {
struct Context;
bool available();
// quality: 1 quality, 2 balanced, 3 performance, 4 ultra performance (Arm ASR shader quality mode).
Context *create(VkPhysicalDevice physical,VkDevice device,int render_w,int render_h,int display_w,int display_h,int quality);
void destroy(Context *);
int phase_count(int render_w,int display_w);
void jitter(int index,int phase_count,float *x,float *y);
struct Inputs {
    VkCommandBuffer cmd;
    VkImage color,depth,motion,output;   // color/motion: SHADER_READ_ONLY, depth: DEPTH_STENCIL_READ_ONLY, output: COLOR_ATTACHMENT
    int render_w,render_h;
    float jitter_x,jitter_y;             // pixels, same values used to jitter the projection
    float frame_time_ms,near_plane,far_plane,fov_y,sharpness;
    bool reset;
};
// Records the upscale into inputs.cmd (outside any render pass). Resources are returned
// to the layouts they were passed in. False if the library reported an error.
bool dispatch(Context *,const Inputs &);
}
