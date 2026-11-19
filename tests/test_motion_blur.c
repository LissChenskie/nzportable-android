// SPDX-License-Identifier: GPL-2.0-or-later
// Motion blur settings: presets, OFF, CUSTOM, sanitising and the frame-rate independent shutter.
#include "motion_blur.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static int near(float a,float b){return fabsf(a-b)<1e-4f;}
int main(void){
    nzp_mblur_params p;
    /* OFF: completely off, all derived values zero (the renderers then do no work at all). */
    nzp_mblur_resolve(&p,NZP_MBLUR_OFF,1,1,1,16,1,100);
    CHECK(!p.enabled&&p.samples==0&&p.intensity==0.f&&p.camera==0.f&&p.movement==0.f);
    /* The presets get stronger and more expensive, ULTRA runs at full resolution without a separate pass. */
    float last_intensity=0;int last_samples=0;
    for(int mode=1;mode<=4;mode++){
        nzp_mblur_resolve(&p,mode,0,0,0,0,0,100); /* cvars are ignored by presets */
        CHECK(p.enabled);CHECK(p.intensity>last_intensity);CHECK(p.samples>last_samples);
        CHECK(p.samples>=NZP_MBLUR_MIN_SAMPLES&&p.samples<=NZP_MBLUR_MAX_SAMPLES);
        CHECK(p.scale==100||p.scale==75||p.scale==50);
        last_intensity=p.intensity;last_samples=p.samples;
    }
    CHECK(p.scale==100&&p.quality==1);
    nzp_mblur_resolve(&p,1,0,0,0,0,0,100);CHECK(p.scale==50&&p.quality==0);
    /* CUSTOM uses the cvars and limits them. */
    nzp_mblur_resolve(&p,NZP_MBLUR_CUSTOM,0.6f,0.5f,0.25f,10,1,75);
    CHECK(p.enabled&&near(p.intensity,0.6f)&&near(p.camera,0.5f)&&near(p.movement,0.25f)&&p.samples==10&&p.quality==1&&p.scale==75);
    nzp_mblur_resolve(&p,NZP_MBLUR_CUSTOM,9.f,-1.f,3.f,99,7,13);
    CHECK(p.intensity==1.f&&p.camera==0.f&&p.movement==1.f&&p.samples==NZP_MBLUR_MAX_SAMPLES&&p.quality==1&&p.scale==50);
    nzp_mblur_resolve(&p,NZP_MBLUR_CUSTOM,0.5f,1.f,1.f,-5,0,100);CHECK(p.samples==NZP_MBLUR_MIN_SAMPLES);
    /* Nothing to draw = off, instead of a pass that does nothing. */
    nzp_mblur_resolve(&p,NZP_MBLUR_CUSTOM,0.f,1.f,1.f,8,1,100);CHECK(!p.enabled);
    nzp_mblur_resolve(&p,NZP_MBLUR_CUSTOM,0.5f,0.f,0.f,8,1,100);CHECK(!p.enabled);
    nzp_mblur_resolve(&p,NZP_MBLUR_CUSTOM,0.5f,0.f,0.3f,8,1,100);CHECK(p.enabled); /* movement alone is enough */
    /* A corrupt config can never select an undefined mode. */
    nzp_mblur_resolve(&p,-3,1,1,1,8,1,100);CHECK(!p.enabled);nzp_mblur_resolve(&p,42,1,1,1,8,1,100);CHECK(!p.enabled);
    /* Effect resolution snaps to the supported steps. */
    CHECK(nzp_mblur_snap_scale(100)==100&&nzp_mblur_snap_scale(90)==100&&nzp_mblur_snap_scale(80)==75&&nzp_mblur_snap_scale(65)==75&&nzp_mblur_snap_scale(60)==50&&nzp_mblur_snap_scale(50)==50&&nzp_mblur_snap_scale(1)==50);
    /* Fixed shutter time: displacement per frame * scale = velocity * T, independent of the frame rate. */
    const float v=1000.f; /* pixels per second */
    float len30=v*(1.f/30)*nzp_mblur_shutter_scale(0.5f,1000.f/30),len60=v*(1.f/60)*nzp_mblur_shutter_scale(0.5f,1000.f/60),len120=v*(1.f/120)*nzp_mblur_shutter_scale(0.5f,1000.f/120);
    printf("streak at 30/60/120 FPS: %.2f %.2f %.2f px\n",len30,len60,len120);
    CHECK(fabsf(len30-len60)<0.01f&&fabsf(len60-len120)<0.01f);
    CHECK(near(len60,1000.f*0.5f/60.f));
    /* A hitch frame or an absurd interval cannot blow it up, and a very high frame rate is bounded. */
    CHECK(nzp_mblur_shutter_scale(1.f,100000.f)==nzp_mblur_shutter_scale(1.f,100.f));
    CHECK(nzp_mblur_shutter_scale(1.f,0.f)<=NZP_MBLUR_SCALE_MAX&&nzp_mblur_shutter_scale(1.f,-5.f)<=NZP_MBLUR_SCALE_MAX);
    CHECK(nzp_mblur_shutter_scale(0.f,16.6f)==0.f);
    CHECK(nzp_mblur_shutter_scale(1.f,16.66f)>nzp_mblur_shutter_scale(0.5f,16.66f));
    /* Longest streak grows with intensity and the screen size. */
    CHECK(nzp_mblur_max_length_px(1.f,2400)>nzp_mblur_max_length_px(0.3f,2400));
    CHECK(near(nzp_mblur_max_length_px(1.f,2400),2*nzp_mblur_max_length_px(1.f,1200)));
    puts("motion blur settings passed");
    return 0;
}
