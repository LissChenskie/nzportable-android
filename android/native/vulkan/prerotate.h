// SPDX-License-Identifier: GPL-2.0-or-later
// Surface pre-rotation maths shared by the Vulkan backend and host tests.
//
// Convention (matches VK_SURFACE_TRANSFORM_ROTATE_*_BIT_KHR): the presented
// image is the logical image rotated clockwise by rot*90 degrees. When the
// swapchain preTransform equals the surface currentTransform the presentation
// engine/compositor performs no extra rotation (and Mali avoids a GPU or
// display-processor rotation pass).
//   logical (lx,ly) in a LWxLH image, origin top-left, y down.
//   rot 1: physical = (LH-ly, lx)       physical size LHxLW
//   rot 2: physical = (LW-lx, LH-ly)    physical size LWxLH
//   rot 3: physical = (ly, LW-lx)       physical size LHxLW
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
static inline int nzp_rot_odd(int rot){return rot&1;}
/* Clip-space 2x2: x'=m[0]*x+m[1]*y, y'=m[2]*x+m[3]*y on Vulkan NDC (y down). */
static inline void nzp_rot_matrix(int rot,float m[4]){
    static const float t[4][4]={{1,0,0,1},{0,-1,1,0},{-1,0,0,-1},{0,1,-1,0}};
    for(int i=0;i<4;i++)m[i]=t[rot&3][i];
}
/* Rotate a logical-framebuffer rectangle (top-left origin) into the physical one. */
static inline void nzp_rot_rect(int rot,float x,float y,float w,float h,float lw,float lh,float out[4]){
    switch(rot&3){
    case 1:out[0]=lh-(y+h);out[1]=x;out[2]=h;out[3]=w;break;
    case 2:out[0]=lw-(x+w);out[1]=lh-(y+h);out[2]=w;out[3]=h;break;
    case 3:out[0]=y;out[1]=lw-(x+w);out[2]=h;out[3]=w;break;
    default:out[0]=x;out[1]=y;out[2]=w;out[3]=h;break;
    }
}
/* Map a logical pixel index to the physical pixel that holds it. */
static inline void nzp_rot_pixel(int rot,int lx,int ly,int lw,int lh,int *px,int *py){
    switch(rot&3){
    case 1:*px=lh-1-ly;*py=lx;break;
    case 2:*px=lw-1-lx;*py=lh-1-ly;break;
    case 3:*px=ly;*py=lw-1-lx;break;
    default:*px=lx;*py=ly;break;
    }
}
#ifdef __cplusplus
}
#endif
