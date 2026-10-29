// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <math.h>
#include <string.h>
static inline void nzp_identity(float *m) {
    memset(m, 0, 16 * sizeof(float)); m[0]=m[5]=m[10]=m[15]=1;
}
static inline void nzp_multiply(float *out, const float *a, const float *b) {
    float result[16];
    for (int c=0;c<4;c++) for (int r=0;r<4;r++) {
        result[c*4+r]=0;
        for (int k=0;k<4;k++) result[c*4+r]+=a[k*4+r]*b[c*4+k];
    }
    memcpy(out,result,sizeof(result));
}
// Bound BOTH dimensions while preserving the real landscape aspect ratio.
static inline void nzp_render_size(int w, int h, int max_w, int max_h, int *rw, int *rh) {
    if (w<1) w=1; if (h<1) h=1;
    float s=fminf(1.f,fminf((float)max_w/w,(float)max_h/h));
    *rw=(int)(w*s); *rh=(int)(h*s);
    if (*rw>1) *rw &= ~1;
    if (*rh>1) *rh &= ~1;
    if (*rw<1) *rw=1;
    if (*rh<1) *rh=1;
}

// Render percentage is a deliberate user option. No thermal/device heuristics.
// At 100%, the exact EGL drawable dimensions must be preserved.
static inline void nzp_scale_size(int width,int height,int percent,int *rw,int *rh) {
    if(width<1)width=1;
    if(height<1)height=1;
    if(percent<60 || percent>100)percent=100;
    *rw=percent==100?width:(int)lroundf(width*(percent/100.0f));
    *rh=percent==100?height:(int)lroundf(height*(percent/100.0f));
    if(*rw<1)*rw=1;
    if(*rh<1)*rh=1;
}
// UI is drawn on the actual native surface, independent of world FBO size.
// Leave 20% vertical headroom for enlarged mobile menu rows.
static inline int nzp_ui_scale(int native_height,int percentage) {
    if(native_height<1)native_height=1;
    if(percentage<75 || percentage>150)percentage=100;
    int scale=(int)((native_height/300.0f)*(percentage/100.0f));
    int upper=native_height/240;
    if(upper<1)upper=1;
    if(scale<1)scale=1;
    if(scale>upper)scale=upper;
    return scale;
}
