#version 450
precision highp float; precision highp int;
// Motion blur / composite: one full-screen triangle, no vertex buffers.
// Everything that does not depend on a texture result is computed here (Mali: lets the
// hardware prefetch the texels): pre-rotation, the texture coordinate and the homogeneous
// previous-frame position of the rotation-only reprojection (interpolates exactly).
layout(push_constant) uniform Push {
    vec4 rot;  // pre-rotation of the swapchain pass (identity for an offscreen target)
    vec4 uvr;  // uv of the first and the last pixel row/column: u0 v0 u1 v1 (FSR keeps a 1 px inset)
    vec4 h0; vec4 h1; vec4 h2; // columns of the 3x3 reprojection: Vulkan NDC (y down) -> previous NDC
    vec4 a;    // world size xy, display size zw (pixels)
    vec4 b;    // shutter scale, camera gain, movement gain, max length (display px)
    vec4 c;    // max samples, quality, 0, 0
} pc;
layout(location=0) out vec2 v_uv;
layout(location=1) out vec3 v_q;
layout(location=2) out vec2 v_n;
void main() {
    vec2 p=vec2(float((gl_VertexIndex<<1)&2),float(gl_VertexIndex&2)); // (0,0) (2,0) (0,2)
    vec2 n=p*2.0-1.0;
    gl_Position=vec4(pc.rot.x*n.x+pc.rot.y*n.y,pc.rot.z*n.x+pc.rot.w*n.y,0.0,1.0);
    v_uv=mix(pc.uvr.xy,pc.uvr.zw,p);
    v_n=n;
    v_q=mat3(pc.h0.xyz,pc.h1.xyz,pc.h2.xyz)*vec3(n,1.0);
}
