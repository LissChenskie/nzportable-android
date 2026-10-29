// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <SDL.h>
void Android_Init(void);
int Android_Event(const SDL_Event *event);
int Android_Foreground(void);
void Android_Frame(void);
void Android_DrawTouch(void);
void Android_TouchMove(float *x,float *y);
void Android_TouchLook(void);
void Android_ResetTouch(void);
int Android_ThermalStatus(void);
int Android_FrameRate(void);
void Android_RecordFrame(double cpu_frame_ms);
