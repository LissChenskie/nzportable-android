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
    if (*rw>1) *rw &= ~1; if (*rh>1) *rh &= ~1;
    if (*rw<1) *rw=1; if (*rh<1) *rh=1;
}
