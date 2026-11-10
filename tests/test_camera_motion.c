// SPDX-License-Identifier: GPL-2.0-or-later
// Motion-vector history maths used by gles_compat: prev_mvp = P_prev * V_prev * (V_cur^-1 * MV).
#include "render_math.h"
#include <assert.h>
#include <stdio.h>
static void view(float *m,float yaw,float x,float y,float z){ /* rotate about Y then translate by -eye */
    float c=cosf(yaw),s=sinf(yaw),rot[16],tr[16];nzp_identity(rot);nzp_identity(tr);
    rot[0]=c;rot[2]=-s;rot[8]=s;rot[10]=c;tr[12]=-x;tr[13]=-y;tr[14]=-z;nzp_multiply(m,rot,tr);
}
static int near(const float *a,const float *b,float eps){for(int i=0;i<16;i++)if(fabsf(a[i]-b[i])>eps)return 0;return 1;}
int main(void){
    float v[16],inv[16],prod[16],id[16];nzp_identity(id);
    view(v,0.7f,120.f,-30.f,512.f);nzp_rigid_inverse(inv,v);nzp_multiply(prod,inv,v);
    assert(near(prod,id,1e-4f));nzp_multiply(prod,v,inv);assert(near(prod,id,1e-4f));
    /* Eye position is the inverse's translation. */
    assert(fabsf(inv[12]-120.f)<1e-3f&&fabsf(inv[13]+30.f)<1e-3f&&fabsf(inv[14]-512.f)<1e-3f);
    float proj[16]={0};proj[0]=1.2f;proj[5]=1.6f;proj[10]=-1.002f;proj[11]=-1;proj[14]=-8.01f;
    float model[16],mv[16],mvp[16],pv[16],prev[16],tmp[16];
    nzp_identity(model);model[12]=300;model[13]=20;model[14]=-40; /* an entity placed in the world */
    /* Static camera: previous MVP equals the current one => zero motion. */
    nzp_multiply(mv,v,model);nzp_multiply(mvp,proj,mv);
    nzp_multiply(tmp,inv,mv);nzp_multiply(pv,proj,v);nzp_multiply(prev,pv,tmp);
    assert(near(mvp,prev,1e-2f));
    /* Camera moved forward: reprojecting the same world point through last frame's camera must
     * land where last frame's camera really saw it. */
    float v_prev[16],pv_prev[16],point[4]={300.f,20.f,-40.f,1.f};
    view(v_prev,0.7f,120.f,-30.f,500.f);nzp_multiply(pv_prev,proj,v_prev);
    view(v,0.7f,120.f,-30.f,512.f);nzp_rigid_inverse(inv,v);nzp_multiply(mv,v,model);
    nzp_multiply(tmp,inv,mv);nzp_multiply(prev,pv_prev,tmp);
    float want[4],got[4];
    for(int r=0;r<4;r++){want[r]=got[r]=0;for(int c=0;c<4;c++){want[r]+=pv_prev[c*4+r]*point[c];}}
    /* model * origin == point, so prev_mvp * origin must equal P_prev*V_prev*point. */
    float origin[4]={0,0,0,1};for(int r=0;r<4;r++){got[r]=0;for(int c=0;c<4;c++)got[r]+=prev[c*4+r]*origin[c];}
    for(int r=0;r<4;r++)assert(fabsf(want[r]-got[r])<1e-2f);
    puts("camera motion history maths tests passed");
    return 0;
}
