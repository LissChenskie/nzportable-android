// SPDX-License-Identifier: GPL-2.0-or-later
// The shader rotation (NDC), viewport rectangle rotation and pixel readback
// mapping must describe the same transform for every pre-rotation.
#include "vulkan/prerotate.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
    const int lw=96,lh=64;
    for(int rot=0;rot<4;rot++){
        const int pw=nzp_rot_odd(rot)?lh:lw,ph=nzp_rot_odd(rot)?lw:lh;
        float m[4];nzp_rot_matrix(rot,m);
        /* Several viewports, including a partial one (HUD/letterbox). */
        const float rects[3][4]={{0,0,lw,lh},{10,5,40,30},{lw-20,lh-12,20,12}};
        for(int r=0;r<3;r++){
            float vx=rects[r][0],vy=rects[r][1],vw=rects[r][2],vh=rects[r][3],pr[4];
            nzp_rot_rect(rot,vx,vy,vw,vh,lw,lh,pr);
            assert(pr[0]>=-1e-4f&&pr[1]>=-1e-4f&&pr[0]+pr[2]<=pw+1e-4f&&pr[1]+pr[3]<=ph+1e-4f);
            for(int i=0;i<=8;i++)for(int j=0;j<=8;j++){
                float nx=-1+2*i/8.f,ny=-1+2*j/8.f; /* logical NDC, y down */
                float lxf=vx+(nx+1)*.5f*vw,lyf=vy+(ny+1)*.5f*vh; /* logical pixel position */
                float sx=m[0]*nx+m[1]*ny,sy=m[2]*nx+m[3]*ny;     /* shader output */
                float pxf=pr[0]+(sx+1)*.5f*pr[2],pyf=pr[1]+(sy+1)*.5f*pr[3];
                float ex,ey;
                switch(rot){
                case 1:ex=lh-lyf;ey=lxf;break;
                case 2:ex=lw-lxf;ey=lh-lyf;break;
                case 3:ex=lyf;ey=lw-lxf;break;
                default:ex=lxf;ey=lyf;}
                assert(fabsf(pxf-ex)<1e-3f&&fabsf(pyf-ey)<1e-3f);
            }
        }
        /* Pixel readback mapping is a bijection onto the physical image. */
        static unsigned char seen[96*64];for(int i=0;i<pw*ph;i++)seen[i]=0;
        for(int y=0;y<lh;y++)for(int x=0;x<lw;x++){
            int px,py;nzp_rot_pixel(rot,x,y,lw,lh,&px,&py);
            assert(px>=0&&px<pw&&py>=0&&py<ph);assert(!seen[py*pw+px]);seen[py*pw+px]=1;
        }
    }
    /* Corners: 90 deg clockwise moves top-left to top-right. */
    int px,py;nzp_rot_pixel(1,0,0,lw,lh,&px,&py);assert(px==lh-1&&py==0);
    nzp_rot_pixel(3,0,0,lw,lh,&px,&py);assert(px==0&&py==lw-1);
    puts("pre-rotation matrix, viewport and readback mapping tests passed");
    return 0;
}
