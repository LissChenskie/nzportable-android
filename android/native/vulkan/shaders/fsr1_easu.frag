#version 450
#extension GL_GOOGLE_include_directive : enable
// AMD FidelityFX Super Resolution 1.0, EASU (edge-adaptive spatial upsampling), fp32 path (works on any GPU).
// ffx_a.h / ffx_fsr1.h: Copyright (c) 2021 Advanced Micro Devices, Inc., MIT licence.
layout(set=0,binding=0) uniform sampler2D InputTexture;
layout(push_constant) uniform Push { uvec4 Const0; uvec4 Const1; uvec4 Const2; uvec4 Const3; } pc;
layout(location=0) out vec4 out_color;
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p){return textureGather(InputTexture,p,0);}
AF4 FsrEasuGF(AF2 p){return textureGather(InputTexture,p,1);}
AF4 FsrEasuBF(AF2 p){return textureGather(InputTexture,p,2);}
#include "ffx_fsr1.h"
void main(){
    AF3 c;
    FsrEasuF(c,AU2(gl_FragCoord.xy),pc.Const0,pc.Const1,pc.Const2,pc.Const3);
    out_color=vec4(c,1.0);
}
