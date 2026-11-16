// SPDX-License-Identifier: GPL-2.0-or-later
// AMD FidelityFX FSR 1.0 headers (MIT) are used unmodified; see vendor/fsr1/LICENSE-FidelityFX-FSR.txt.
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#define A_CPU 1
#include "vendor/fsr1/ffx_a.h"
#include "vendor/fsr1/ffx_fsr1.h"
#include "fsr1.h"
void nzp_fsr1_easu_constants(unsigned out[16],float in_w,float in_h,float out_w,float out_h){
    FsrEasuCon(out,out+4,out+8,out+12,in_w,in_h,in_w,in_h,out_w,out_h);
}
void nzp_fsr1_rcas_constants(unsigned out[4],float sharpness_stops){FsrRcasCon(out,sharpness_stops);}
