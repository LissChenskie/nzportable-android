#version 450
#extension GL_GOOGLE_include_directive : enable
// AMD FidelityFX Super Resolution 1.0, RCAS (robust contrast adaptive sharpening), fp32 path.
// ffx_a.h / ffx_fsr1.h: Copyright (c) 2021 Advanced Micro Devices, Inc., MIT licence.
layout(set=0,binding=0) uniform sampler2D InputTexture;
layout(push_constant) uniform Push { uvec4 Const0; } pc;
layout(location=0) out vec4 out_color;
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p){return texelFetch(InputTexture,ASU2(p),0);}
void FsrRcasInputF(inout AF1 r,inout AF1 g,inout AF1 b){}
#include "ffx_fsr1.h"
void main(){
    AF3 c;
    FsrRcasF(c.r,c.g,c.b,AU2(gl_FragCoord.xy),pc.Const0);
    out_color=vec4(c,1.0);
}
