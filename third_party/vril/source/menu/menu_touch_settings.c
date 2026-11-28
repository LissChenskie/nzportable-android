/*
Copyright (C) 2025-2026 NZ:P Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
#include "../nzportable_def.h"
#include "menu_defs.h"

#ifdef PLATFORM_ANDROID
/* The touch settings screen of the Android build (replaces the mouse-style Configuration / Video / Audio / Controls /
 * Accessibility menus). The screen itself, its layout and its gestures live in android/native/touch_menu.c, the texts in
 * touch_menu_content.c; this file only says which cvar each row is and applies what the player did. */
#include "touch_menu_content.h"

extern cvar_t vid_render_scale, vid_ui_scale, r_vsync, cl_maxfps, vid_performance_mode, vk_fsr, scr_fov, v_gamma;
extern cvar_t vid_ultrawide_limiter, r_runqmbparticles, r_retro, r_dithering, vk_prerotate, scr_showfps, nzp_profile, net_lan_legacy;
extern cvar_t touch_overlay, touch_dynamic_stick, touch_sensitivity;
extern cvar_t mblur_mode, mblur_intensity, mblur_camera, mblur_movement, mblur_samples, mblur_quality, mblur_scale;
extern cvar_t volume, bgmvolume, in_aimassist, m_pitch, sensitivity, in_acceleration, in_rumble;
extern cvar_t cl_hitmarkers, cl_colorblind, scr_whiteflash, sv_spoofmonth, cl_textopacity, cl_crosshairdot, cl_viewbob, cl_meleedive;
extern void Host_WriteConfiguration(void);
extern float VID_PixelsPerMM(void);
extern int VID_UpscalerState(void);
extern int VID_UpscalerSupported(int mode);
extern int VID_StartRendererBenchmark(void);
extern int VID_MotionBlurState(void);
extern int VID_MotionBlurMovementSupported(void);
extern void VID_MotionBlurEffective(float *intensity, float *camera, float *movement, int *samples, int *quality, int *scale);
extern void VID_MotionBlurSetMode(int mode);
extern int VID_MotionBlurBeginEdit(void);
extern void VID_SetGraphicsAPIChoice(void);
extern char *VID_GraphicsAPIName(void);
extern void Android_OpenStickEditor(void);
void Menu_Bindings_Set(void);
void Menu_SetPreviousMenu(void);   /* menu_sys.c; not in menu_defs.h */
void Con_ToggleConsole_f(void);

static tm_state tms;
static int tms_ready;
static int pending_category = -1;

static void backend_fill(void *user, int x, int y, int w, int h, int r, int g, int b, int a) { (void)user; Draw_FillByColor(x, y, w, h, r, g, b, a); }
static void backend_text(void *user, int x, int y, const char *s, int scale, int r, int g, int b, int a) { (void)user; Draw_ColoredString(x, y, (char *)s, r, g, b, a, (float)scale); }
static int backend_width(void *user, const char *s, int scale) { (void)user; return getTextWidth((char *)s, (float)scale); }
static const tm_backend backend = {NULL, backend_fill, backend_text, backend_width};

/* ---------- what each row shows ---------- */

static int row_visible(void *user, int id) {
    (void)user;
#ifndef RENDERER_SUPPORTS_DITHERING
    if (id == TMI_VID_DITHER) return 0;
#endif
    (void)id;
    return 1;
}

static void fsr_text(tm_item *it) {
    const char *name = (int)vk_fsr.value == 2 ? "LITE" : (int)vk_fsr.value == 3 ? "FSR 1.0" : "FULL";
    if ((int)vk_fsr.value == 0) { snprintf(it->value_text, sizeof(it->value_text), "OFF"); return; }
    switch (VID_UpscalerState()) {
        case 1: snprintf(it->value_text, sizeof(it->value_text), "%s", name); break;
        case 2: snprintf(it->value_text, sizeof(it->value_text), "%s: NOT SUPPORTED", name); break;
        case 3: snprintf(it->value_text, sizeof(it->value_text), "%s: AUTO-OFF", name); break;
        default: snprintf(it->value_text, sizeof(it->value_text), "%s: NEEDS RES<100", name); break;
    }
}

static void choice_from(tm_item *it, int id, float value) {
    const tm_spec *sp = tm_find_spec(id);
    it->choice_current = sp ? tm_choice_index_of(sp, value) : -1;
    if (it->choice_current < 0) snprintf(it->value_text, sizeof(it->value_text), "%d", (int)(value + 0.5f));   /* a value set from the console */
}

static void row_fill(void *user, int id, tm_item *it) {
    (void)user;
    int mb_i, mb_s, mb_q, mb_sc;
    float mb_in, mb_ca, mb_mo;
    VID_MotionBlurEffective(&mb_in, &mb_ca, &mb_mo, &mb_s, &mb_q, &mb_sc);
    mb_i = (int)(mblur_mode.value + 0.5f);
    const int mb_off = mb_i == 0;
    switch (id) {
        case TMI_VID_RESOLUTION: choice_from(it, id, vid_render_scale.value); break;
        case TMI_VID_FSR: {
            const tm_spec *sp = tm_find_spec(id);
            choice_from(it, id, vk_fsr.value);
            for (int c = 0; c < it->choice_count; c++) it->choice_enabled[c] = sp->choice_value[c] == 0 || VID_UpscalerSupported((int)sp->choice_value[c]);
            fsr_text(it);
            break;
        }
        case TMI_VID_UISCALE: choice_from(it, id, vid_ui_scale.value); break;
        case TMI_VID_FPS: choice_from(it, id, cl_maxfps.value); break;
        case TMI_VID_VSYNC: it->value = r_vsync.value != 0.f; break;
        case TMI_VID_PERF: choice_from(it, id, vid_performance_mode.value); break;
        case TMI_VID_FOV: it->value = scr_fov.value; break;
        case TMI_VID_GAMMA: it->value = v_gamma.value; break;
        case TMI_VID_ULTRAWIDE: it->value = vid_ultrawide_limiter.value != 0.f; break;
        case TMI_VID_PARTICLES: it->value = r_runqmbparticles.value != 0.f; break;
        case TMI_VID_FILTER: choice_from(it, id, r_retro.value != 0.f ? 1.f : 0.f); break;
        case TMI_VID_DITHER: it->value = r_dithering.value != 0.f; break;

        case TMI_MB_MODE:
            choice_from(it, id, mblur_mode.value);
            if (!mb_off && VID_MotionBlurState() == 2) snprintf(it->value_text, sizeof(it->value_text), "%s (NOT SUPPORTED)", it->choice_label[it->choice_current >= 0 ? it->choice_current : 0]);
            break;
        case TMI_MB_INTENSITY: it->value = mb_in; it->enabled = !mb_off; break;
        case TMI_MB_CAMERA: it->value = mb_ca; it->enabled = !mb_off; break;
        case TMI_MB_MOVEMENT: it->value = mb_mo; it->enabled = !mb_off && VID_MotionBlurMovementSupported(); break;
        case TMI_MB_SAMPLES: it->value = (float)mb_s; it->enabled = !mb_off; break;
        case TMI_MB_QUALITY: it->value = mb_q != 0; it->enabled = !mb_off; break;
        case TMI_MB_SCALE: choice_from(it, id, (float)mb_sc); it->enabled = !mb_off; break;

        case TMI_AUD_MASTER: it->value = volume.value; break;
        case TMI_AUD_MUSIC: it->value = bgmvolume.value; break;

        case TMI_CTL_OVERLAY: choice_from(it, id, touch_overlay.value); break;
        case TMI_CTL_DYNAMIC: it->value = touch_dynamic_stick.value != 0.f; break;
        case TMI_CTL_STICKZONE: it->enabled = touch_dynamic_stick.value != 0.f; break;
        case TMI_CTL_TOUCHSENS: it->value = touch_sensitivity.value; break;
        case TMI_CTL_INVERT: it->value = (int)m_pitch.value > 0; break;
        case TMI_CTL_AIMASSIST: it->value = (int)in_aimassist.value == 1; break;
        case TMI_CTL_PADSENS: it->value = sensitivity.value; break;
        case TMI_CTL_PADACCEL: it->value = in_acceleration.value; break;
        case TMI_CTL_RUMBLE: it->value = in_rumble.value != 0.f; break;

        case TMI_ACC_HITMARK: it->value = cl_hitmarkers.value != 0.f; break;
        case TMI_ACC_BACKDROP: it->value = cl_textopacity.value; break;
        case TMI_ACC_COLORS: it->value = cl_colorblind.value != 0.f; break;
        case TMI_ACC_FLASH: it->value = scr_whiteflash.value != 0.f; break;
        case TMI_ACC_MONTH: choice_from(it, id, sv_spoofmonth.value); break;
        case TMI_ACC_DOT: it->value = cl_crosshairdot.value != 0.f; break;
        case TMI_ACC_BOB: it->value = cl_viewbob.value; break;
        case TMI_ACC_DIVE: it->value = cl_meleedive.value != 0.f; break;

        case TMI_ADV_API: snprintf(it->value_text, sizeof(it->value_text), "%s", VID_GraphicsAPIName()); break;
        case TMI_ADV_PREROT: it->value = vk_prerotate.value != 0.f; it->enabled = VID_MotionBlurMovementSupported(); break;   /* Vulkan only */
        case TMI_ADV_SHOWFPS: it->value = scr_showfps.value != 0.f; break;
        case TMI_ADV_PROFILE: it->value = nzp_profile.value != 0.f; break;
        case TMI_ADV_LAN: it->value = net_lan_legacy.value != 0.f; break;
        default: break;
    }
}
static const tm_content_hooks hooks = {row_visible, row_fill, NULL};

/* Why a dimmed row cannot be used (shown when it is tapped). */
static const char *disabled_reason(int id) {
    switch (id) {
        case TMI_MB_INTENSITY: case TMI_MB_CAMERA: case TMI_MB_SAMPLES: case TMI_MB_QUALITY: case TMI_MB_SCALE: return "Turn motion blur on first.";
        case TMI_MB_MOVEMENT: return mblur_mode.value == 0.f ? "Turn motion blur on first." : "Movement blur needs Vulkan.";
        case TMI_CTL_STICKZONE: return "Turn on the dynamic left stick first.";
        case TMI_ADV_PREROT: return "Pre-rotation is a Vulkan setting.";
        default: return "Not available right now.";
    }
}

/* ---------- what a change does ---------- */

/* The cvar's own name is used, so a wrong string cannot silently write to nothing (scr_fov is "fov", scr_showfps "show_fps"). */
static void set_cv(cvar_t *cv, float value) { Cvar_SetValue(cv->name, value); }
static void save(void) { Host_WriteConfiguration(); }

static void apply_switch(int id, int on) {
    const float v = on ? 1.f : 0.f;
    switch (id) {
        case TMI_VID_VSYNC: set_cv(&r_vsync, v); break;
        case TMI_VID_ULTRAWIDE: set_cv(&vid_ultrawide_limiter, v); break;
        case TMI_VID_PARTICLES: set_cv(&r_runqmbparticles, v); break;
        case TMI_VID_DITHER: set_cv(&r_dithering, v); break;
        case TMI_MB_QUALITY: if (VID_MotionBlurBeginEdit()) set_cv(&mblur_quality, v); break;
        case TMI_CTL_DYNAMIC: set_cv(&touch_dynamic_stick, v); break;
        case TMI_CTL_INVERT: set_cv(&m_pitch, v); break;
        case TMI_CTL_AIMASSIST: set_cv(&in_aimassist, v); break;
        case TMI_CTL_RUMBLE: set_cv(&in_rumble, v); break;
        case TMI_ACC_HITMARK: set_cv(&cl_hitmarkers, v); break;
        case TMI_ACC_COLORS: set_cv(&cl_colorblind, v); break;
        case TMI_ACC_FLASH: set_cv(&scr_whiteflash, v); break;
        case TMI_ACC_DOT: set_cv(&cl_crosshairdot, v); break;
        case TMI_ACC_DIVE: set_cv(&cl_meleedive, v); break;
        case TMI_ADV_PREROT: set_cv(&vk_prerotate, v); tm_toast(&tms, "Restart the app to apply pre-rotation.", realtime); break;
        case TMI_ADV_SHOWFPS: set_cv(&scr_showfps, v); break;
        case TMI_ADV_PROFILE: set_cv(&nzp_profile, v); break;
        case TMI_ADV_LAN: set_cv(&net_lan_legacy, v); break;
        default: return;
    }
    save();
}

static void apply_pick(int id, float value) {
    switch (id) {
        case TMI_VID_RESOLUTION: set_cv(&vid_render_scale, value); break;
        case TMI_VID_FSR: set_cv(&vk_fsr, value); break;
        case TMI_VID_UISCALE: set_cv(&vid_ui_scale, value); break;
        case TMI_VID_FPS:
            set_cv(&cl_maxfps, value);
            if (value == 0.f || value > 60.f) set_cv(&r_vsync, 0);   /* a 60 Hz sync would hold it back */
            break;
        case TMI_VID_PERF: {
            const int mode = (int)value;
            set_cv(&vid_performance_mode, mode);
            set_cv(&cl_maxfps, mode == 1 ? 120 : mode == 2 ? 30 : 60);
            set_cv(&r_vsync, mode == 1 ? 0 : 1);
            break;
        }
        case TMI_VID_FILTER: set_cv(&r_retro, value); break;
        case TMI_MB_MODE: VID_MotionBlurSetMode((int)value); break;
        case TMI_MB_SCALE: if (VID_MotionBlurBeginEdit()) set_cv(&mblur_scale, value); break;
        case TMI_CTL_OVERLAY: set_cv(&touch_overlay, value); break;
        case TMI_ACC_MONTH: set_cv(&sv_spoofmonth, value); break;
        default: return;
    }
    save();
}

static void apply_slider(int id, float v, int commit) {
    switch (id) {
        case TMI_VID_FOV: set_cv(&scr_fov, v); break;
        case TMI_VID_GAMMA: set_cv(&v_gamma, v); break;
        case TMI_MB_INTENSITY: if (VID_MotionBlurBeginEdit()) set_cv(&mblur_intensity, v); break;
        case TMI_MB_CAMERA: if (VID_MotionBlurBeginEdit()) set_cv(&mblur_camera, v); break;
        case TMI_MB_MOVEMENT: if (VID_MotionBlurBeginEdit()) set_cv(&mblur_movement, v); break;
        case TMI_MB_SAMPLES: if (VID_MotionBlurBeginEdit()) set_cv(&mblur_samples, v); break;
        case TMI_AUD_MASTER: set_cv(&volume, v); break;
        case TMI_AUD_MUSIC: set_cv(&bgmvolume, v); break;
        case TMI_CTL_TOUCHSENS: set_cv(&touch_sensitivity, v); break;
        case TMI_CTL_PADSENS: set_cv(&sensitivity, v); break;
        case TMI_CTL_PADACCEL: set_cv(&in_acceleration, v); break;
        case TMI_ACC_BACKDROP: set_cv(&cl_textopacity, v); break;
        case TMI_ACC_BOB: set_cv(&cl_viewbob, v); break;
        default: return;
    }
    if (commit) save();   /* while the finger is down only the cvar moves, the file is written once on release */
}

static void apply_action(int id) {
    switch (id) {
        case TMI_CTL_STICKZONE: Android_OpenStickEditor(); break;
        case TMI_CTL_BINDINGS: Menu_Bindings_Set(); break;
        case TMI_ADV_API:
            VID_SetGraphicsAPIChoice();
            tm_toast(&tms, "Restart the app to apply the graphics API.", realtime);
            save();
            break;
        case TMI_ADV_BENCH:
            if (VID_StartRendererBenchmark()) { key_dest = key_game; m_state = m_none; }
            break;
        case TMI_ADV_CONSOLE: Con_ToggleConsole_f(); break;
        default: break;
    }
}

/* ---------- entry points used by the menu system and the touch input ---------- */

static void ensure(void) {
    if (tms_ready) return;
    const char *names[TM_MAX_CATS];
    for (int i = 0; i < tm_category_count; i++) names[i] = tm_categories[i].name;
    tm_init(&tms);
    tm_set_categories(&tms, names, tm_category_count, 0);
    tms_ready = 1;
}

/* Called whenever the screen is entered: nothing from an earlier visit (an open sheet, a half gesture) survives. */
void Menu_TouchSettings_Opened(void) {
    ensure();
    tms.sheet_item = -1;
    tms.mode = 0;
    tms.pressed = -1;
    tms.velocity = 0.f;
    tms.focus = -1;
    tms.toast[0] = 0;
    if (pending_category >= 0) { tm_select_category(&tms, pending_category); pending_category = -1; }
}

/* The old per-topic menus lead here, to the category that matches: VIDEO 0, BLUR 1, AUDIO 2, CONTROLS 3, ACCESS 4, ADVANCED 5. */
void Menu_TouchSettings_Open(int category) {
    pending_category = category;
    Menu_Configuration_Set();
}

void Menu_TouchSettings_Draw(void) {
    ensure();
    const float ppmm = VID_PixelsPerMM();
    if (tms.w != vid.width || tms.h != vid.height || tms.ppmm != ppmm) tm_set_screen(&tms, vid.width, vid.height, ppmm);
    tm_update(&tms, realtime);
    tm_content_build(&tms, &hooks);
    tm_draw(&tms, &backend);
}

/* A touch event with the position as a fraction of the screen. Returns 1 if the screen took it. */
int Menu_TouchSettings_Finger(int phase, float nx, float ny) {
    if (m_state != m_configuration) return 0;
    ensure();
    tm_event ev;
    tm_touch(&tms, (tm_phase)phase, nx * vid.width, ny * vid.height, realtime, &ev);
    switch (ev.kind) {
        case TM_EV_BACK: Menu_SetSound(MENU_SND_NAVIGATE); Menu_SetPreviousMenu(); break;
        case TM_EV_CATEGORY: Menu_SetSound(MENU_SND_NAVIGATE); break;
        case TM_EV_SWITCH: Menu_SetSound(MENU_SND_ENTER); apply_switch(tms.items[ev.item].id, ev.value > 0.5f); break;
        case TM_EV_PICK: {
            const tm_spec *sp = tm_find_spec(tms.items[ev.item].id);
            Menu_SetSound(MENU_SND_ENTER);
            if (sp && ev.choice >= 0 && ev.choice < sp->choice_count) apply_pick(sp->id, sp->choice_value[ev.choice]);
            break;
        }
        case TM_EV_SLIDER: apply_slider(tms.items[ev.item].id, ev.value, ev.commit); break;
        case TM_EV_ACTION: Menu_SetSound(MENU_SND_ENTER); apply_action(tms.items[ev.item].id); break;
        case TM_EV_DISABLED: Menu_SetSound(MENU_SND_BEEP); tm_toast(&tms, disabled_reason(tms.items[ev.item].id), realtime); break;
        default: break;
    }
    return 1;
}

/* The Back key: the sheet closes first. Returns 1 if it did something. */
int Menu_TouchSettings_Key(int key) {
    if (m_state != m_configuration || !tms_ready) return 0;
    if (key == MENU_KEY_BACK || key == K_ESCAPE) return tm_back(&tms);
    return 0;
}
#endif
