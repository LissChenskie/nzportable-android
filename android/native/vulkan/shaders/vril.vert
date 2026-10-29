#version 450
layout(constant_id=0) const bool LIGHTMAPPED=false;
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=3) in vec2 lightmap_uv;
layout(set=0,binding=0,std140) uniform State {
    mat4 mvp; mat4 mv; mat4 tm; vec4 fog; vec4 fogcolor; vec4 params;
} u;
layout(location=0) out vec2 v_uv;
layout(location=1) out vec4 v_color;
layout(location=2) out float v_depth;
layout(location=3) out vec2 v_lightmap_uv;
void main() {
    vec4 p=u.mvp*vec4(position,1);
    // GL clip depth [-w,w] -> Vulkan [0,w]. Flip Y once; viewport is positive.
    gl_Position=vec4(p.x,-p.y,(p.z+p.w)*0.5,p.w);
    gl_PointSize=1;
    v_uv=(u.tm*vec4(uv,0,1)).xy;
    v_color=color;v_lightmap_uv=LIGHTMAPPED?lightmap_uv:vec2(0);
    v_depth=abs((u.mv*vec4(position,1)).z);
}
