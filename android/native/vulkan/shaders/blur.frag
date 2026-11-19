#version 450
precision highp float; precision highp int;
// Camera motion blur, one gather pass.
//
// Two variants, chosen at pipeline creation (specialisation constant USE_MV):
//
// USE_MV = false  "fast": camera rotation only. v = shutter * camera_gain * rotation, with the rotation analytic from the
//                 3x3 reprojection of the vertex shader (depth independent). No motion-vector image exists at all: the
//                 renderer draws the camera-locked weapon AFTER this pass, so the gather never sees it. One colour fetch
//                 for a still pixel, 1 + N for a moving one.
// USE_MV = true   adds translation (parallax) from the motion-vector target of the world pass (camera motion of static
//                 geometry, camera-locked weapon = exactly 0):
//   translation = total - rotation
//   v = shutter * (camera_gain * rotation + movement_gain * translation)
// Taps are spread over [-v/2, v/2] and averaged (a box filter = an open shutter).
//
// Cost on Mali: 1 colour + 1 motion-vector fetch for a static pixel (early out), up to
// 1 + N colour fetches (+ N motion-vector fetches with quality 1) for a fast one. N follows the
// streak length (one tap per ~3 px) and never exceeds the setting.
layout(constant_id=0) const bool LOWRES=false; // true: writes the blur and its weight (a) for the composite
layout(constant_id=1) const bool USE_MV=true;  // false: rotation only, the motion-vector image is not read
layout(set=0,binding=0) uniform mediump sampler2D src_tex;
layout(set=0,binding=1) uniform sampler2D mv_tex;   // RG16F: pixels of the world image, previous - current
layout(push_constant) uniform Push { vec4 rot; vec4 uvr; vec4 h0; vec4 h1; vec4 h2; vec4 a; vec4 b; vec4 c; } pc;
layout(location=0) in vec2 v_uv;
layout(location=1) in vec3 v_q;
layout(location=2) in vec2 v_n;
layout(location=0) out vec4 out_color;
const float MIN_PX=0.75;   // below this a pixel is left alone (no blur of still pictures)
const float FULL_PX=3.0;   // and fades in until here: no popping when the camera starts moving
const float PX_PER_TAP=2.0;
const float JITTER=0.75;   // fraction of a tap cell the noise may move a tap (less grain, still no banding)
void main() {
    mediump vec3 c=texture(src_tex,v_uv).rgb;
    vec2 mv_px=USE_MV?texture(mv_tex,v_uv).xy:vec2(1.0);
    // Exactly zero = camera-locked (weapon) or nothing drawn: never smear it.
    if(dot(mv_px,mv_px)<1e-8||pc.c.x<1.5){out_color=vec4(c,LOWRES?0.0:1.0);return;}
    vec2 world=pc.a.xy,display=pc.a.zw;
    vec2 v_rot=v_q.z>1e-4?0.5*(v_q.xy/v_q.z-v_n):vec2(0.0);   // NDC -> uv is 0.5
    vec2 v;
    if(USE_MV){
        vec2 v_tot=mv_px/world;
        if(pc.c.z<0.5)v_rot=v_tot; // the engine reported no camera this frame: treat all motion as camera motion
        v=pc.b.x*(pc.b.y*v_rot+pc.b.z*(v_tot-v_rot));
    }else v=pc.b.x*pc.b.y*v_rot;
    float len_px=length(v*display);
    // Soft cap: linear for short streaks, asymptotically pc.b.w. No hard clamp, so no visible kink,
    // and a one-frame jump (respawn, snap turn) cannot smear the whole screen.
    float shrink=1.0/(1.0+len_px/pc.b.w);
    v*=shrink;len_px*=shrink;
    float fade=smoothstep(MIN_PX,FULL_PX,len_px);
    if(fade<=0.0){out_color=vec4(c,LOWRES?0.0:1.0);return;}
    float nf=clamp(ceil(len_px/PX_PER_TAP),2.0,pc.c.x);
    int n=int(nf);
    // Interleaved gradient noise moves the taps inside their cell: banding becomes fine, static
    // (so it cannot flicker) grain. Computed from the logical display position, so it does not change with the
    // surface rotation or with the resolution of the blur pass.
    float jitter=0.5+JITTER*(fract(52.9829189*fract(dot(floor(v_uv*display),vec2(0.06711056,0.00583715))))-0.5);
    mediump vec3 acc=c;float wsum=1.0;
    float lc=length(mv_px)+0.5;
    for(int i=0;i<16;i++){
        if(i>=n)break;
        vec2 uv=v_uv+v*((float(i)+jitter)/nf-0.5);
        // Taps off the screen carry nothing: drop them instead of smearing the border pixels.
        if(any(lessThan(uv,vec2(0.0)))||any(greaterThan(uv,vec2(1.0))))continue;
        float w=1.0;
        if(USE_MV&&pc.c.y>0.5){ // reject taps that move differently (weapon, nearer geometry): no halo
            // Measured every tap: checking only every second one halves the motion-vector fetches but removes only
            // ~55% of the weapon halo instead of ~90% (tests/vulkan_motion_blur_test.cpp).
            vec2 d=texture(mv_tex,uv).xy-mv_px;
            w=1.0-smoothstep(0.5*lc,lc,length(d));
        }
        acc+=w*texture(src_tex,uv).rgb;wsum+=w;
    }
    mediump vec3 blurred=acc/wsum;
    out_color=LOWRES?vec4(blurred,fade):vec4(mix(c,blurred,fade),1.0);
}
