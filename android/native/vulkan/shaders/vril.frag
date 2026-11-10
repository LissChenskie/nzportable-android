#version 450
precision highp float; precision highp int; // everything not marked mediump stays full precision
// Mali/Bifrost notes (also valid on other tile-based GPUs):
//  * ALPHA_TEST is a specialization constant. Opaque pipelines contain no
//    discard, so the driver can keep early depth/stencil and forward pixel kill.
//  * Colour/texture arithmetic is mediump (RelaxedPrecision -> fp16 on Mali,
//    twice the ALU rate and half the register pressure). UVs, the lightmap UVs and
//    all fog distance math stay highp: Quake UVs reach large repeat values.
layout(constant_id=0) const bool LIGHTMAPPED=false;
layout(constant_id=1) const bool ALPHA_TEST=true;
layout(set=0,binding=0,std140) uniform State {
    mat4 mvp; mat4 mv; mat4 tm; vec4 fog; vec4 fogcolor; vec4 params;
    mat4 prev_mvp; vec4 motion;
} u;
#ifdef MRT
layout(push_constant) uniform Push { vec4 rot; vec4 jitter_size; } pc;
layout(location=4) in vec4 v_cur;
layout(location=5) in vec4 v_prev;
layout(location=1) out vec2 out_motion; // pixels, previous minus current (Arm ASR/FSR convention)
#endif
layout(set=1,binding=0) uniform mediump sampler2D base_texture;
layout(set=2,binding=0) uniform mediump sampler2D lightmap;
layout(location=3) in vec2 v_lightmap_uv;
layout(location=0) in vec2 v_uv;
layout(location=1) in mediump vec4 v_color;
layout(location=2) in mediump float v_depth;
layout(location=0) out vec4 out_color;
void main() {
    mediump vec4 c=v_color;
    if(u.params.x!=0) {
        mediump vec4 t=texture(base_texture,v_uv);
        c=u.params.y!=0?t:t*c;
    }
    if(ALPHA_TEST && c.a<=u.params.z)discard;
#ifdef MRT
    // motion.x != 0: camera-locked geometry (weapon model) has no motion relative to the view.
    vec2 mv=vec2(0);
    if(u.motion.x<0.5&&v_cur.w>1e-4&&v_prev.w>1e-4)
        mv=(v_prev.xy/v_prev.w-v_cur.xy/v_cur.w)*0.5*pc.jitter_size.zw;
    out_motion=mv;
#endif
    float f=1;
    if(u.fog.x==1)f=clamp((u.fog.z-v_depth)/max(u.fog.z-u.fog.y,0.0001),0,1);
    else if(u.fog.x==2)f=exp(-u.fog.w*v_depth);
    else if(u.fog.x==3){float d=u.fog.w*v_depth;f=exp(-d*d);}
    mediump float fog_factor=clamp(f,0.0,1.0);
    mediump vec3 fog_color=u.fogcolor.rgb;
    if(LIGHTMAPPED) {
        // Fog belongs to the final lit surface, not to the diffuse input.
        // Otherwise the lightmap multiplies the fog color and dark BSP faces
        // appear to have no fog while dynamic/unlightmapped models do.
        // Match the 8-bit diffuse attachment when fog is disabled.
        mediump vec4 base=round(clamp(c,0.0,1.0)*255.0)/255.0;
        mediump vec4 lit=2.0*base*texture(lightmap,v_lightmap_uv);
        out_color=vec4(mix(fog_color,lit.rgb,fog_factor),lit.a);
    } else {
        out_color=vec4(mix(fog_color,c.rgb,fog_factor),c.a);
    }
}
