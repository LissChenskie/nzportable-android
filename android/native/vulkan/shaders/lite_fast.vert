#version 450
precision highp float; precision highp int;
// FSR Lite "fast" (Mali Bifrost/G72 oriented): everything that does not depend on a texture result is
// computed per vertex, so the fragment shader's texture coordinates are plain varyings. On Bifrost that
// lets the hardware issue the fetches before the fragment program runs (texture prefetch).
layout(push_constant) uniform Push {
    vec4 h0; vec4 h1; vec4 h2; // columns of the 3x3 reprojection (Vulkan NDC -> previous-frame homogeneous NDC)
    vec4 a;                    // jitter px (y down) xy, render size zw
    vec4 b;                    // display size xy, history blend z
} pc;
layout(location=0) out vec2 v_cuv; // where the current frame holds this pixel's content
layout(location=1) out vec3 v_q;   // homogeneous previous position; divided per pixel
void main() {
    vec2 p=vec2(float((gl_VertexIndex<<1)&2),float(gl_VertexIndex&2)); // (0,0) (2,0) (0,2)
    vec2 n=p*2.0-1.0;
    gl_Position=vec4(n,0.0,1.0);
    v_cuv=p+pc.a.xy/pc.a.zw; // linear in the vertex => exact after interpolation
    v_q=mat3(pc.h0.xyz,pc.h1.xyz,pc.h2.xyz)*vec3(n,1.0);
}
