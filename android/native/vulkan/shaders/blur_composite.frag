#version 450
precision highp float; precision highp int;
// Final composite of the reduced-resolution blur: the sharp image stays at full detail where the
// blur weight is 0 (everything that does not move), the blur shows where its weight is high.
layout(set=0,binding=0) uniform mediump sampler2D src_tex;
layout(set=0,binding=1) uniform mediump sampler2D blur_tex;
layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 out_color;
void main() {
    mediump vec3 c=texture(src_tex,v_uv).rgb;
    mediump vec4 b=texture(blur_tex,v_uv);
    out_color=vec4(mix(c,b.rgb,b.a),1.0);
}
