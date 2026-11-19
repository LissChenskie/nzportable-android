// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
/* Motion blur settings shared by the menu/cvar layer and both renderers.
 * Pure arithmetic: no GL/Vulkan, so tests/test_motion_blur.c can check it on the host.
 *
 * Modes: 0 OFF, 1 LOW, 2 MEDIUM, 3 HIGH, 4 ULTRA, 5 CUSTOM.
 * Presets ignore the individual cvars; CUSTOM uses them. */
#include <math.h>

#define NZP_MBLUR_OFF 0
#define NZP_MBLUR_CUSTOM 5
#define NZP_MBLUR_MIN_SAMPLES 4
#define NZP_MBLUR_MAX_SAMPLES 16   /* the shader loops are bounded by this */

typedef struct {
    int enabled;       /* 0: no motion-vector target, no pass, no cost at all */
    float intensity;   /* 0..1: shutter time as a fraction of 1/60 s */
    float camera;      /* 0..1: gain of the rotation part of the camera motion */
    float movement;    /* 0..1: gain of the translation (parallax) part */
    int samples;       /* upper bound of taps per pixel, 4..16 (fewer are used on slow pixels) */
    int quality;       /* 0 fast: velocity of the pixel only; 1 high: every tap is checked against it */
    int scale;         /* 100, 75 or 50: resolution of the blur pass relative to the display */
} nzp_mblur_params;

static inline float nzp_mblur_clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}

static inline int nzp_mblur_snap_scale(int scale){
    return scale>=88?100:scale>=63?75:50;
}

static inline void nzp_mblur_resolve(nzp_mblur_params *out,int mode,float intensity,float camera,float movement,
                                     int samples,int quality,int scale) {
    static const nzp_mblur_params presets[5]={
        {0,0.f,0.f,0.f,0,0,100},
        {1,0.30f,1.0f,0.50f,6,0,50},   /* LOW:    half-resolution pass, cheap gather */
        {1,0.50f,1.0f,0.70f,8,1,50},   /* MEDIUM: half-resolution pass, per-tap velocity check */
        {1,0.75f,1.0f,0.85f,10,1,75},
        {1,1.00f,1.0f,1.00f,12,1,100}, /* ULTRA:  full resolution, folded into the final composite */
    };
    if(mode<0||mode>NZP_MBLUR_CUSTOM)mode=NZP_MBLUR_OFF;
    if(mode<NZP_MBLUR_CUSTOM)*out=presets[mode];
    else{
        out->enabled=1;out->intensity=nzp_mblur_clampf(intensity,0.f,1.f);
        out->camera=nzp_mblur_clampf(camera,0.f,1.f);out->movement=nzp_mblur_clampf(movement,0.f,1.f);
        out->samples=samples<NZP_MBLUR_MIN_SAMPLES?NZP_MBLUR_MIN_SAMPLES:samples>NZP_MBLUR_MAX_SAMPLES?NZP_MBLUR_MAX_SAMPLES:samples;
        out->quality=quality>0?1:0;out->scale=nzp_mblur_snap_scale(scale);
    }
    /* Nothing to draw: switch the whole effect off instead of paying for a no-op. */
    if(out->intensity<=0.001f||(out->camera<=0.001f&&out->movement<=0.001f))out->enabled=0;
    if(!out->enabled){out->intensity=out->camera=out->movement=0.f;out->samples=0;}
}

/* Blur length scale for a fixed shutter TIME, so the look does not depend on the frame rate:
 * the engine reports the displacement over one frame; a shutter open for T seconds smears
 * velocity*T = displacement*T/dt. dt is the real frame interval of that displacement. */
#define NZP_MBLUR_DT_MIN_MS 4.f
#define NZP_MBLUR_DT_MAX_MS 100.f
#define NZP_MBLUR_SCALE_MAX 3.f
static inline float nzp_mblur_shutter_scale(float intensity,float dt_ms) {
    const float dt=nzp_mblur_clampf(dt_ms,NZP_MBLUR_DT_MIN_MS,NZP_MBLUR_DT_MAX_MS);
    const float s=intensity*(1000.f/60.f)/dt;
    return s>NZP_MBLUR_SCALE_MAX?NZP_MBLUR_SCALE_MAX:s;
}
/* Asymptotic cap of the streak length in display pixels (soft: see blur.frag). */
static inline float nzp_mblur_max_length_px(float intensity,int display_height) {
    return (0.02f+0.045f*nzp_mblur_clampf(intensity,0.f,1.f))*(float)display_height;
}
