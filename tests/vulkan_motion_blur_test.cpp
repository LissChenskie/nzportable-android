// SPDX-License-Identifier: GPL-2.0-or-later
// Camera motion blur end to end on a software Vulkan device (lavapipe).
// This checks behaviour and image properties. It says nothing about speed on a Mali GPU.
#include <SDL.h>
#include "vulkan/renderer.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
static const int W=256,H=192;
typedef std::vector<unsigned char> Image;
#define CHECK(cond,...) do{if(!(cond)){printf("FAIL line %d: %s  ",__LINE__,#cond);printf(__VA_ARGS__);printf("\n");exit(1);}}while(0)

static nzp_vk_uniform uniform(float tx,float ty=0.f){nzp_vk_uniform u{};for(int i=0;i<4;i++)u.mvp[i*5]=u.mv[i*5]=u.tm[i*5]=1;u.params[0]=u.params[1]=1;u.params[2]=-1;u.mvp[12]=tx;u.mvp[13]=ty;return u;}
static void quad(const nzp_vk_uniform &u,const nzp_vk_raster &s,float x0,float y0,float x1,float y1,unsigned texture){
    const unsigned char w[4]={255,255,255,255};
    nzp_vk_vertex v[]={{{x0,y0,0},{0,0},{w[0],w[1],w[2],w[3]}},{{x1,y0,0},{1,0},{w[0],w[1],w[2],w[3]}},{{x1,y1,0},{1,1},{w[0],w[1],w[2],w[3]}},
        {{x0,y0,0},{0,0},{w[0],w[1],w[2],w[3]}},{{x1,y1,0},{1,1},{w[0],w[1],w[2],w[3]}},{{x0,y1,0},{0,1},{w[0],w[1],w[2],w[3]}}};
    NZP_VK_Draw(v,6,4,&u,&s,texture);
}
static nzp_mblur_params preset(int mode){nzp_mblur_params p;nzp_mblur_resolve(&p,mode,0,0,0,0,0,100);return p;}
static nzp_mblur_params custom(float i,float c,float m,int samples,int quality,int scale){nzp_mblur_params p;nzp_mblur_resolve(&p,NZP_MBLUR_CUSTOM,i,c,m,samples,quality,scale);return p;}

struct Opt {
    int percent=100,upscaler=0,frames=6,rotation=-1;
    nzp_mblur_params mb{};
    float step=0.15f;    // camera pan per frame in NDC (19 px at this size)
    float rot_gain=1.f;  // 1: the reprojection matrix describes the pan (pure rotation); 0: it does not (all of it is "translation")
    bool weapon=false,hud=false,jump=false,vertical=false; // vertical: pan up/down instead of sideways
    float dt=16.6f;
};
static Image run(const Opt &o){
    NZP_VK_SetMotionBlur(&o.mb);NZP_VK_SetUpscaler(o.upscaler);NZP_VK_ResetHistory();NZP_VK_SetMotionBlurTestFrameTime(o.dt);
    NZP_VK_SetTestRotation(o.rotation);
    nzp_vk_raster s{};s.blend_src=1;s.blend_dst=0;s.depth_func=0x203;s.depth_far=1;s.depth_write=1;s.viewport[2]=W;s.viewport[3]=H;
    Image px(size_t(W)*H*4);
    for(int t=0;t<o.frames;t++){
        CHECK(NZP_VK_BeginFrame(0,o.percent),"BeginFrame");
        NZP_VK_SetCamera(4,4096,1.2f);
        const bool last=t==o.frames-1;
        float tx=t*o.step,prev=tx-o.step;
        if(o.jump&&last){tx=0.9f;NZP_VK_ResetHistory();} // teleport: engine reports a reset together with the jump
        auto u=o.vertical?uniform(0.f,tx):uniform(tx);auto pu=o.vertical?uniform(0.f,prev):uniform(prev);std::copy(pu.mvp,pu.mvp+16,u.prev_mvp);
        const float g=o.step*o.rot_gain;const float h[9]={1,0,o.vertical?0.f:-g,0,1,o.vertical?g:0.f,0,0,1};NZP_VK_SetReprojection(h);
        quad(u,s,-3.f,-3.f,3.f,3.f,1);
        if(o.weapon){auto w=uniform(0.f);w.motion[0]=1.f;quad(w,s,-0.9f,-0.9f,-0.3f,-0.3f,2);} // camera-locked, high contrast
        if(o.hud){NZP_VK_BeginUI();nzp_vk_raster hs=s;hs.depth=0;hs.depth_write=0;quad(uniform(0.f),hs,0.3f,-0.9f,0.9f,-0.3f,3);}
        if(last)NZP_VK_RequestCapture(nullptr);
        NZP_VK_EndFrame();
        CHECK(NZP_VK_ValidationErrors()==0,"validation errors");
    }
    CHECK(NZP_VK_ReadPixels(0,0,W,H,px.data()),"ReadPixels");
    return px;
}
// RMS neighbour difference in a rectangle (gradient energy): a blur lowers it along its axis. The mean absolute
// difference would not do: for a monotonic edge the total variation is the same however much it is blurred.
static double sharp(const Image &a,int x0,int y0,int x1,int y1,bool horizontal){
    double sum=0;int n=0;
    for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){
        const int dx=horizontal,dy=!horizontal;
        const double d=int(a[(size_t(y)*W+x)*4+1])-int(a[(size_t(y+dy)*W+x+dx)*4+1]);sum+=d*d;n++;
    }
    return std::sqrt(sum/n);
}
static double mad(const Image &a,const Image &b,int x0=0,int y0=0,int x1=W,int y1=H){
    double sum=0;int n=0;
    for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)for(int c=0;c<3;c++){sum+=std::abs(int(a[(size_t(y)*W+x)*4+c])-int(b[(size_t(y)*W+x)*4+c]));n++;}
    return sum/n;
}
static double max_diff(const Image &a,const Image &b){int m=0;for(size_t i=0;i<a.size();i++)if(i%4!=3)m=std::max(m,std::abs(int(a[i])-int(b[i])));return m;}
static double red_minus_green(const Image &a,int x0,int y0,int x1,int y1){
    double sum=0;int n=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){sum+=int(a[(size_t(y)*W+x)*4])-int(a[(size_t(y)*W+x)*4+1]);n++;}return sum/n;
}
static double brightness(const Image &a,int x0,int y0,int x1,int y1){
    double sum=0;int n=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){for(int c=0;c<3;c++)sum+=a[(size_t(y)*W+x)*4+c];n+=3;}return sum/n;
}
static void dump(const char *name,const Image &px){
    if(!SDL_getenv("NZP_MB_DUMP"))return;
    char path[160];snprintf(path,sizeof(path),"%s/mb_%s.ppm",SDL_getenv("NZP_MB_DUMP"),name);FILE *f=fopen(path,"wb");if(!f)return;
    fprintf(f,"P6\n%d %d\n255\n",W,H);for(int y=H-1;y>=0;y--)for(int x=0;x<W;x++)fwrite(&px[(size_t(y)*W+x)*4],1,3,f);fclose(f);
}
// ReadPixels is bottom-left origin (GL), so row 0 is the bottom: the weapon and the HUD sit in rows ~10..67.
// Background measuring window: clear of both.
static const int BX0=100,BY0=85,BX1=250,BY1=185;

int main(){
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    SDL_Window *win=SDL_CreateWindow("mb",0,0,W,H,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);if(!win)return 1;
    CHECK(NZP_VK_Init(win,"build/host-tests",1),"init");NZP_VK_SetBenchmark(1);
    std::vector<unsigned char> tex(size_t(W)*H*4),weapon(64*64*4),hud(64*64*4);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){ /* gradient, 8-texel checker, 1-texel lines */
        int v=40+x/2+y/3;if((x/8+y/8)%2)v+=70;if((x+2*y)%17==0||(2*x-y+200)%23==0)v=250;v=std::min(255,std::max(0,v));
        auto *q=&tex[(size_t(y)*W+x)*4];q[0]=v;q[1]=std::min(255,v+20);q[2]=v/2+30;q[3]=255;}
    for(int y=0;y<64;y++)for(int x=0;x<64;x++){auto *q=&weapon[(size_t(y)*64+x)*4];const bool on=(x/3)%2==0;q[0]=on?255:20;q[1]=on?110:20;q[2]=on?255:20;q[3]=255;} /* pink stripes: red-green is +145, a background pixel has -20 */
    for(int y=0;y<64;y++)for(int x=0;x<64;x++){auto *q=&hud[(size_t(y)*64+x)*4];const bool on=(x%2)==0;q[0]=on?255:0;q[1]=on?255:0;q[2]=on?255:0;q[3]=255;} /* 1-texel white/black stripes */
    NZP_VK_Texture(1,0,0,0,W,H,0x1908,0x1401,tex.data(),1,4);
    NZP_VK_Texture(2,0,0,0,64,64,0x1908,0x1401,weapon.data(),1,4);
    NZP_VK_Texture(3,0,0,0,64,64,0x1908,0x1401,hud.data(),1,4);
    for(unsigned id=1;id<=3;id++){NZP_VK_TextureFilter(id,0x2801,0x2600);NZP_VK_TextureFilter(id,0x2800,0x2600);NZP_VK_TextureFilter(id,0x2802,0x812F);NZP_VK_TextureFilter(id,0x2803,0x812F);}
    if(NZP_VK_MotionBlurState()!=0||NZP_VK_MotionBlurBytes()!=0){puts("FAIL: not off at start");return 1;}

    Opt off;off.weapon=off.hud=true;                          // reference: same camera, blur disabled
    Opt ultra=off;ultra.mb=preset(4);                         // ULTRA: full resolution, folded into the composite
    Opt still=ultra;still.step=0.f;                            // blur enabled, camera not moving

    // 1. OFF costs nothing and a still camera is not blurred.
    Image ref_off=run(off);CHECK(NZP_VK_MotionBlurState()==0&&NZP_VK_MotionBlurBytes()==0,"off state");
    Image ref_still=run(Opt{[&]{Opt o=off;o.step=0.f;return o;}()});
    Image blur_still=run(still);
    CHECK(NZP_VK_MotionBlurState()==1,"on state");
    const double still_diff=max_diff(ref_still,blur_still);
    printf("still camera: max pixel difference blur on vs off = %.0f\n",still_diff);
    CHECK(still_diff<=1.0,"a static scene must not be blurred");

    // 2. Panning: blurred along the motion axis only.
    Image pan_off=run(off),pan_on=run(ultra);
    dump("pan_off",pan_off);dump("pan_ultra",pan_on);
    const double sh_off=sharp(pan_off,BX0,BY0,BX1,BY1,true),sh_on=sharp(pan_on,BX0,BY0,BX1,BY1,true);
    const double sv_off=sharp(pan_off,BX0,BY0,BX1,BY1,false),sv_on=sharp(pan_on,BX0,BY0,BX1,BY1,false);
    printf("pan: horizontal sharpness off %.2f -> ultra %.2f (%.0f%%), vertical %.2f -> %.2f (%.0f%%)\n",sh_off,sh_on,100*sh_on/sh_off,sv_off,sv_on,100*sv_on/sv_off);
    CHECK(sh_on<0.6*sh_off,"panning must blur along the motion");
    CHECK(sh_on/sh_off<0.6*(sv_on/sv_off),"the blur must be directional (the test card has diagonal lines, so only the ratio is meaningful)");
    {Opt vo=off;vo.vertical=true;Opt vu=ultra;vu.vertical=true;Image va=run(vo),vb=run(vu);dump("vpan_ultra",vb);
     const double vh=sharp(vb,BX0,BY0,BX1,BY1,true)/sharp(va,BX0,BY0,BX1,BY1,true),vv=sharp(vb,BX0,BY0,BX1,BY1,false)/sharp(va,BX0,BY0,BX1,BY1,false);
     printf("vertical pan: horizontal sharpness %.0f%%, vertical %.0f%%\n",100*vh,100*vv);
     CHECK(vv<0.6*vh,"a vertical pan must blur vertically");}

    // 3. Weapon (camera-locked) and HUD stay sharp; its colour must not leak into the background (quality 1 vs 0).
    const double w_off=sharp(pan_off,20,15,85,62,true),w_on=sharp(pan_on,20,15,85,62,true);
    const double hud_diff=mad(pan_off,pan_on,170,15,240,62);
    printf("weapon sharpness off %.2f ultra %.2f; HUD region mean difference %.3f\n",w_off,w_on,hud_diff);
    CHECK(w_on>0.95*w_off,"the camera-locked weapon must not be blurred");
    CHECK(hud_diff<0.5,"the HUD must not be blurred");
    Opt q0=ultra;q0.mb=custom(1.f,1.f,1.f,12,0,100);Opt q1=ultra;q1.mb=custom(1.f,1.f,1.f,12,1,100);
    Image im_q0=run(q0),im_q1=run(q1);dump("q0",im_q0);dump("q1",im_q1);
    const double base=red_minus_green(pan_off,90,11,93,66),c0=red_minus_green(im_q0,90,11,93,66),c1=red_minus_green(im_q1,90,11,93,66);
    printf("weapon halo (red-green next to the weapon): clean %.1f, quality 0: %.1f, quality 1: %.1f\n",base,c0,c1);
    CHECK(std::fabs(c0-base)>2.0,"test is meaningless if fast mode shows no leak at all");
    CHECK(std::fabs(c1-base)<0.5*std::fabs(c0-base),"per-tap velocity check must remove most of the weapon halo");

    // 4. Camera and Movement gains are independent.
    //    rot_gain 1: the pan is pure rotation. rot_gain 0: the reprojection says "no rotation", so all of it is translation.
    Opt rot=off;rot.mb=custom(1.f,1.f,0.f,12,1,100);Opt rot_no_cam=off;rot_no_cam.mb=custom(1.f,0.f,1.f,12,1,100);
    Opt tr=off;tr.rot_gain=0.f;tr.mb=custom(1.f,0.f,1.f,12,1,100);Opt tr_no_move=tr;tr_no_move.mb=custom(1.f,1.f,0.f,12,1,100);
    const double r_cam=sharp(run(rot),BX0,BY0,BX1,BY1,true),r_nocam=sharp(run(rot_no_cam),BX0,BY0,BX1,BY1,true);
    const double t_move=sharp(run(tr),BX0,BY0,BX1,BY1,true),t_nomove=sharp(run(tr_no_move),BX0,BY0,BX1,BY1,true);
    printf("sharpness (off %.2f): rotation with camera gain %.2f / without %.2f; translation with movement gain %.2f / without %.2f\n",sh_off,r_cam,r_nocam,t_move,t_nomove);
    CHECK(r_cam<0.8*sh_off&&r_nocam>0.97*sh_off,"camera gain must control rotation blur");
    CHECK(t_move<0.8*sh_off&&t_nomove>0.97*sh_off,"movement gain must control translation blur");

    // 5. Frame-rate independence: the same motion at 60 and 120 FPS gives (almost) the same picture.
    Opt f60=off;f60.mb=preset(3);f60.frames=7;f60.step=0.12f;f60.dt=16.6f;   // both end at the same camera position: (7-1)*0.12 = (13-1)*0.06
    Opt f120=f60;f120.frames=13;f120.step=0.06f;f120.dt=8.3f;
    Opt f60_wrong=f120;f60_wrong.dt=16.6f; // same displacement per frame but the wrong interval: must look different
    Image a=run(f60),b=run(f120),c=run(f60_wrong);dump("fps60",a);dump("fps120",b);
    const double same=mad(a,b,BX0,BY0,BX1,BY1),different=mad(a,c,BX0,BY0,BX1,BY1);
    printf("fps independence: 60 vs 120 FPS mean difference %.2f, 60 FPS vs wrong interval %.2f\n",same,different);
    CHECK(same<0.5*different,"blur length must follow velocity, not the frame rate");

    // 6. Settings really change the picture; resolutions all blur; memory accounting is exact.
    // Blur Samples is an upper bound (a short streak needs few taps), so use a fast pan for a long streak.
    Opt few=off;few.step=0.6f;few.mb=custom(1.f,1.f,1.f,4,0,100);Opt many=few;many.mb=custom(1.f,1.f,1.f,16,0,100);
    Image im_few=run(few),im_many=run(many);
    printf("samples 4 vs 16: mean difference %.2f\n",mad(im_few,im_many,BX0,BY0,BX1,BY1));
    CHECK(mad(im_few,im_many,BX0,BY0,BX1,BY1)>0.2,"Blur Samples must change the result");
    Opt weak=off;weak.mb=custom(0.2f,1.f,1.f,12,1,100);Opt strong=off;strong.mb=custom(1.f,1.f,1.f,12,1,100);
    const double s_weak=sharp(run(weak),BX0,BY0,BX1,BY1,true),s_strong=sharp(run(strong),BX0,BY0,BX1,BY1,true);
    printf("intensity 0.2 -> sharpness %.2f, 1.0 -> %.2f (off %.2f)\n",s_weak,s_strong,sh_off);
    CHECK(s_weak>s_strong&&s_weak<sh_off,"Blur Intensity must scale the effect");
    size_t bytes100=0,bytes50=0;
    for(int scale:{100,75,50}){
        Opt o=off;o.mb=custom(1.f,1.f,1.f,12,1,scale);Image im=run(o);dump(scale==100?"s100":scale==75?"s75":"s50",im);
        const double sh=sharp(im,BX0,BY0,BX1,BY1,true);
        printf("effect resolution %d%%: sharpness %.2f, extra memory %zu bytes, state %d\n",scale,sh,NZP_VK_MotionBlurBytes(),NZP_VK_MotionBlurState());
        CHECK(sh<0.85*sh_off,"every effect resolution must blur");
        CHECK(hud_diff<0.5&&mad(pan_off,im,170,15,240,62)<0.5,"HUD stays sharp at every effect resolution");
        if(scale==100)bytes100=NZP_VK_MotionBlurBytes();if(scale==50)bytes50=NZP_VK_MotionBlurBytes();
    }
    CHECK(bytes100==size_t(W)*H*4*2,"motion-vector target accounting");
    CHECK(bytes50==bytes100+size_t(W/2)*(H/2)*4*2,"half-resolution target accounting");

    // 7. Borders: no smear, no darkening at the screen edges.
    const double edge_off=brightness(pan_off,0,85,4,185)+brightness(pan_off,W-4,85,W,185),edge_on=brightness(pan_on,0,85,4,185)+brightness(pan_on,W-4,85,W,185);
    printf("edge brightness off %.1f blur %.1f\n",edge_off/2,edge_on/2);
    CHECK(std::fabs(edge_on-edge_off)<0.04*edge_off,"borders must keep their brightness");

    // 8. A teleport/reset frame is not blurred (it would be one huge smear).
    Opt jump=off;jump.mb=preset(4);jump.jump=true;Opt jump_off=off;jump_off.jump=true;
    Image j_on=run(jump),j_off=run(jump_off);
    printf("reset frame: max difference blur on vs off %.0f\n",max_diff(j_on,j_off));
    CHECK(max_diff(j_on,j_off)<=1.0,"the frame after a history reset must not be blurred");

    // 9. Runtime toggling without a restart; OFF releases everything.
    {Opt o=off;o.frames=3;
     for(int k=0;k<3;k++){o.mb=k==1?preset(2):preset(0);run(o);
         CHECK(NZP_VK_MotionBlurState()==(k==1?1:0),"toggle state %d",k);CHECK((NZP_VK_MotionBlurBytes()!=0)==(k==1),"toggle memory %d",k);}}

    // 10. With each upscaler (temporal Lite fast / FSR 1.0 / Arm ASR) the blur runs on top of it.
    for(int mode:{2,3,1}){
        if(!NZP_VK_UpscalerSupported(mode)){printf("upscaler %d unsupported here: skipped\n",mode);continue;}
        Opt u0=off;u0.percent=mode==2?100:75;u0.upscaler=mode;u0.frames=mode==1?8:6;Opt u1=u0;u1.mb=preset(4);
        Image p0=run(u0),p1=run(u1);
        const double a0=sharp(p0,BX0,BY0,BX1,BY1,true),a1=sharp(p1,BX0,BY0,BX1,BY1,true);
        printf("upscaler %d: sharpness %.2f -> with blur %.2f (blur state %d, upscaler state %d)\n",mode,a0,a1,NZP_VK_MotionBlurState(),NZP_VK_UpscalerState());
        CHECK(a1<a0*0.9,"blur must show on top of upscaler %d",mode);
        CHECK(mad(p0,p1,170,15,240,62)<0.5,"HUD stays sharp with upscaler %d",mode);
    }
    NZP_VK_SetUpscaler(0);

    // 11. Pre-rotation: the picture and the blur direction follow the logical orientation.
    {Opt o=ultra;o.rotation=-1;Image r0=run(o);o.rotation=1;Image r1=run(o);o.rotation=-1;
     printf("rotation test: mean difference between 0 and 90 degrees %.2f\n",mad(r0,r1));
     CHECK(mad(r0,r1)<1.0,"blur must not depend on the surface rotation");}
    NZP_VK_SetTestRotation(-1);

    // 12. Lower render scale (3D below 100%) with the blur.
    {Opt o=off;o.percent=75;Opt p=o;p.mb=preset(3);const double s0=sharp(run(o),BX0,BY0,BX1,BY1,true),s1=sharp(run(p),BX0,BY0,BX1,BY1,true);
     printf("render scale 75%%: sharpness %.2f -> with blur %.2f\n",s0,s1);CHECK(s1<0.9*s0,"blur at a lowered render scale");}

    CHECK(NZP_VK_ValidationErrors()==0,"validation errors at the end");
    puts("Vulkan motion blur: still image, direction, weapon and HUD, camera/movement gains, frame rate, settings, memory, borders, reset, toggling, upscalers, rotation passed");
    NZP_VK_Shutdown();SDL_DestroyWindow(win);SDL_Quit();return 0;
}
