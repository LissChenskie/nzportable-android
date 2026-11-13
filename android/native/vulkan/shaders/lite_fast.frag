#version 450
precision highp float; precision highp int;
// Two texture fetches per display pixel: the jittered current frame and the reprojected history.
// No motion-vector texture (rotation-only reprojection from one 3x3 matrix), no neighbourhood taps:
// history that differs from the current sample is rejected by colour distance instead of a clamp box.
layout(set=0,binding=0) uniform mediump sampler2D cur_tex;
layout(set=0,binding=2) uniform mediump sampler2D hist_tex;
layout(push_constant) uniform Push { vec4 h0; vec4 h1; vec4 h2; vec4 a; vec4 b; } pc;
layout(location=0) in vec2 v_cuv;
layout(location=1) in vec3 v_q;
layout(location=0) out vec4 out_color;
void main() {
    mediump vec3 c=texture(cur_tex,v_cuv).rgb;
    mediump float w=pc.b.z;
    if(w>0.0&&v_q.z>1e-4){
        vec2 huv=(v_q.xy/v_q.z)*0.5+0.5;
        if(huv.x>=0.0&&huv.y>=0.0&&huv.x<=1.0&&huv.y<=1.0){
            mediump vec3 h=texture(hist_tex,huv).rgb;
            mediump vec3 d=abs(h-c);
            w*=1.0-smoothstep(0.05,0.25,max(d.r,max(d.g,d.b)));
            out_color=vec4(mix(c,h,w),1.0);
            return;
        }
    }
    out_color=vec4(c,1.0);
}
