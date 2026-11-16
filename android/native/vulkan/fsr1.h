// SPDX-License-Identifier: GPL-2.0-or-later
// CPU-side constants for AMD FidelityFX Super Resolution 1.0 (EASU + RCAS), MIT, vendor/fsr1/.
#pragma once
/* EASU: 16 uints (4 x uvec4) for an upscale from in_w x in_h (viewport = full input) to out_w x out_h. */
void nzp_fsr1_easu_constants(unsigned out[16],float in_w,float in_h,float out_w,float out_h);
/* RCAS: 4 uints. sharpness in stops: 0 = sharpest, 2 = least. */
void nzp_fsr1_rcas_constants(unsigned out[4],float sharpness_stops);
