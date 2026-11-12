#version 450
precision highp float; precision highp int;
// "FSR Lite": a deliberately cheap single-pass temporal upscaler for old tile-based GPUs.
// Seven texture fetches per display pixel (5 current-frame taps for sharpening and the clamp
// box, 1 motion vector, 1 history), no depth input, no compute, no extra passes.
//   cur  : jittered world colour at render resolution (linear)
//   mv   : motion in render pixels, previous minus current (nearest)
//   hist : previous output at display resolution (linear)
layout(set=0,binding=0) uniform mediump sampler2D cur_tex;
layout(set=0,binding=1) uniform sampler2D mv_tex;
layout(set=0,binding=2) uniform mediump sampler2D hist_tex;
layout(push_constant) uniform Push {
    vec4 a; // jitter px (y down) xy, render size zw
    vec4 b; // display size xy, history blend z (0 = no history), sharpness w
} pc;
layout(location=0) out vec4 out_color;
void main() {
    vec2 render_size=pc.a.zw;
    vec2 uv=gl_FragCoord.xy/pc.b.xy;
    // The world was rendered shifted by +jitter, so the content that belongs at uv sits at uv+jitter.
    vec2 cuv=uv+pc.a.xy/render_size;
    vec2 px=1.0/render_size;
    mediump vec3 c=texture(cur_tex,cuv).rgb;
    mediump vec3 n=texture(cur_tex,cuv+vec2(0.0,-px.y)).rgb;
    mediump vec3 s=texture(cur_tex,cuv+vec2(0.0,px.y)).rgb;
    mediump vec3 e=texture(cur_tex,cuv+vec2(px.x,0.0)).rgb;
    mediump vec3 w=texture(cur_tex,cuv+vec2(-px.x,0.0)).rgb;
    mediump vec3 lo=min(c,min(min(n,s),min(e,w)));
    mediump vec3 hi=max(c,max(max(n,s),max(e,w)));
    // Unsharp mask on the current frame, kept inside the local range so it cannot ring.
    mediump vec3 sharp=clamp(c+(c-(n+s+e+w)*0.25)*pc.b.w,lo,hi);
    float history=pc.b.z;
    if(history>0.0){
        vec2 mv=texture(mv_tex,cuv).rg;
        vec2 huv=uv+mv/render_size;
        if(huv.x<0.0||huv.y<0.0||huv.x>1.0||huv.y>1.0)history=0.0; // newly revealed area
        mediump vec3 h=texture(hist_tex,huv).rgb;
        // A little slack so thin detail survives the clamp; fast motion trusts history less.
        mediump vec3 slack=(hi-lo)*0.25+vec3(1.0/255.0);
        h=clamp(h,lo-slack,hi+slack);
        history*=clamp(1.0-length(mv)*0.12,0.35,1.0);
        out_color=vec4(mix(sharp,h,history),1.0);
    }else{
        out_color=vec4(sharp,1.0);
    }
}
