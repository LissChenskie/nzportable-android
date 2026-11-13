#version 450
precision highp float; precision highp int;
layout(constant_id=0) const bool LIGHTMAPPED=false;
// constant_id 1 is ALPHA_TEST (fragment only). MOTION adds jitter and previous-frame
// positions for the temporal upscaler; it is compiled out of every other pipeline.
layout(constant_id=2) const bool MOTION=false;
// constant_id 3: jitter only (FSR Lite fast: no motion vectors, no previous positions).
layout(constant_id=3) const bool JITTER=false;
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=3) in vec2 lightmap_uv;
layout(set=0,binding=0,std140) uniform State {
    mat4 mvp; mat4 mv; mat4 tm; vec4 fog; vec4 fogcolor; vec4 params;
    mat4 prev_mvp; vec4 motion;
} u;
// rot: pre-rotation (see prerotate.h): x'=rot.x*x+rot.y*y, y'=rot.z*x+rot.w*y.
// jitter_size: xy = sub-pixel jitter in Vulkan NDC, zw = render size in pixels.
layout(push_constant) uniform Push { vec4 rot; vec4 jitter_size; } pc;
layout(location=0) out vec2 v_uv;
layout(location=1) out mediump vec4 v_color;
layout(location=2) out mediump float v_depth;
layout(location=3) out vec2 v_lightmap_uv;
layout(location=4) out vec4 v_cur;
layout(location=5) out vec4 v_prev;
void main() {
    vec4 p=u.mvp*vec4(position,1);
    // GL clip depth [-w,w] -> Vulkan [0,w]. Flip Y once; viewport is positive.
    vec2 n=vec2(p.x,-p.y);
    if(MOTION){
        // Unjittered current/previous clip positions (Vulkan orientation) for the motion vectors.
        v_cur=vec4(n,p.z,p.w);
        vec4 q=u.prev_mvp*vec4(position,1);
        v_prev=vec4(q.x,-q.y,q.z,q.w);
        n+=pc.jitter_size.xy*p.w;
    }else{
        v_cur=vec4(0,0,0,1);v_prev=vec4(0,0,0,1);
        if(JITTER)n+=pc.jitter_size.xy*p.w;
    }
    gl_Position=vec4(pc.rot.x*n.x+pc.rot.y*n.y,pc.rot.z*n.x+pc.rot.w*n.y,(p.z+p.w)*0.5,p.w);
    gl_PointSize=1;
    v_uv=(u.tm*vec4(uv,0,1)).xy;
    v_color=color;v_lightmap_uv=LIGHTMAPPED?lightmap_uv:vec2(0);
    v_depth=abs((u.mv*vec4(position,1)).z);
}
