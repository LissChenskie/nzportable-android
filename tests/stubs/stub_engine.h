// SPDX-License-Identifier: GPL-2.0-or-later
/* Just enough of the engine for menu_touch_settings.c to compile and run on the host (tests/touch_settings_glue_test.c).
 * The runner copies the real glue file with its two engine includes pointed here. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef int qboolean;
#define true 1
#define false 0
typedef struct { const char *name; const char *string; qboolean archive; float value; } cvar_t;

/* name in the engine, variable, default */
#define STUB_CVARS(X) \
    X(vid_render_scale, "vid_render_scale", 100) X(vid_ui_scale, "vid_ui_scale", 100) X(r_vsync, "r_vsync", 1) X(cl_maxfps, "cl_maxfps", 60) \
    X(vid_performance_mode, "vid_performance_mode", 0) X(vk_fsr, "vk_fsr", 0) X(scr_fov, "fov", 90) X(v_gamma, "gamma", 1) \
    X(vid_ultrawide_limiter, "vid_ultrawide_limiter", 0) X(r_runqmbparticles, "r_runqmbparticles", 0) X(r_retro, "r_retro", 0) X(r_dithering, "r_dithering", 0) \
    X(vk_prerotate, "vk_prerotate", 0) X(scr_showfps, "show_fps", 0) X(nzp_profile, "nzp_profile", 0) X(net_lan_legacy, "net_lan_legacy", 0) \
    X(touch_overlay, "touch_overlay", 1) X(touch_dynamic_stick, "touch_dynamic_stick", 1) X(touch_sensitivity, "touch_sensitivity", 1) \
    X(mblur_mode, "mblur_mode", 0) X(mblur_intensity, "mblur_intensity", 0.5) X(mblur_camera, "mblur_camera", 1) X(mblur_movement, "mblur_movement", 0) \
    X(mblur_samples, "mblur_samples", 8) X(mblur_quality, "mblur_quality", 1) X(mblur_scale, "mblur_scale", 75) \
    X(volume, "volume", 0.7) X(bgmvolume, "bgmvolume", 0.5) X(in_aimassist, "in_aimassist", 0) X(m_pitch, "m_pitch", 0) X(sensitivity, "sensitivity", 3) \
    X(in_acceleration, "in_acceleration", 0.5) X(in_rumble, "in_rumble", 1) X(cl_hitmarkers, "cl_hitmarkers", 0) X(cl_colorblind, "cl_colorblind", 0) \
    X(scr_whiteflash, "scr_whiteflash", 0) X(sv_spoofmonth, "sv_spoofmonth", 0) X(cl_textopacity, "cl_textopacity", 0.5) X(cl_crosshairdot, "cl_crosshairdot", 0) \
    X(cl_viewbob, "cl_viewbob", 1) X(cl_meleedive, "cl_meleedive", 0)
#define X(var, name, def) extern cvar_t var;
STUB_CVARS(X)
#undef X
void Cvar_SetValue(const char *name, float value);

/* menu / engine state used by the glue */
enum { m_none, m_main, m_pause, m_configuration, m_bindings };
enum { key_game, key_menu, key_menu_pause };
#define MENU_SND_NAVIGATE 1
#define MENU_SND_ENTER 2
#define MENU_SND_BEEP 3
#define K_ESCAPE 27
extern int m_state, key_dest, MENU_KEY_BACK;
extern double realtime;
typedef struct { int width, height; } viddef_t;
extern viddef_t vid;
void Menu_SetSound(int type);
void Menu_Configuration_Set(void);
void Menu_TouchSettings_Opened(void);

void Draw_FillByColor(int x, int y, int w, int h, int r, int g, int b, int a);
void Draw_ColoredString(int x, int y, char *s, float r, float g, float b, float a, float scale);
int getTextWidth(char *s, float scale);
