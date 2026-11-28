// SPDX-License-Identifier: GPL-2.0-or-later
#include "touch_menu_content.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define H(i, l) {.id = i, .kind = TM_HEADER, .label = l, .hint = ""}
#define SW(i, l, h) {.id = i, .kind = TM_SWITCH, .label = l, .hint = h, .vmax = 1, .vstep = 1}
#define ACT(i, l, h) {.id = i, .kind = TM_ACTION, .label = l, .hint = h}
#define SL(i, l, h, lo, hi, st, f) {.id = i, .kind = TM_SLIDER, .label = l, .hint = h, .vmin = lo, .vmax = hi, .vstep = st, .fmt = f}
#define LABELS(...) {__VA_ARGS__}
#define VALUES(...) {__VA_ARGS__}
#define CH(i, l, h, n, labs, vals) {.id = i, .kind = TM_CHOICE, .label = l, .hint = h, .choice_count = n, .choice_label = labs, .choice_value = vals}

static const tm_spec video[] = {
    H(TMI_H_DISPLAY, "DISPLAY"),
    CH(TMI_VID_RESOLUTION, "3D RESOLUTION", "Resolution of the 3D view. 100% is native; lower values render the 3D picture smaller and scale it up. The HUD stays sharp.",
       5, LABELS("100% NATIVE", "90%", "80%", "75%", "60%"), VALUES(100, 90, 80, 75, 60)),
    CH(TMI_VID_FSR, "UPSCALING", "For a lowered 3D resolution. FSR 1.0: spatial. LITE: cheap temporal. FULL: FSR 2.2, newer GPUs. LITE and FULL also smooth edges at 100%.",
       4, LABELS("OFF", "FSR 1.0", "LITE", "FULL"), VALUES(0, 3, 2, 1)),
    CH(TMI_VID_UISCALE, "UI SIZE", "Size of the HUD and the old menus. These settings keep their own touch-friendly size.",
       4, LABELS("80%", "100%", "120%", "140%"), VALUES(80, 100, 120, 140)),
    CH(TMI_VID_FPS, "FPS LIMIT", "Frame rate cap. Above 60 or unlimited switches VSYNC off so a 60 Hz panel does not hold it back.",
       6, LABELS("30 FPS", "45 FPS", "60 FPS", "90 FPS", "120 FPS", "UNLIMITED"), VALUES(30, 45, 60, 90, 120, 0)),
    SW(TMI_VID_VSYNC, "VSYNC", "Sync to the display refresh. Turn off for more than 60 FPS."),
    CH(TMI_VID_PERF, "PERFORMANCE MODE", "BALANCED 60, MAXIMUM 120 (VSYNC off) or BATTERY 30. Sets the FPS limit and VSYNC; no quality changes.",
       3, LABELS("BALANCED 60", "MAXIMUM 120", "BATTERY 30"), VALUES(0, 1, 2)),
    H(TMI_H_IMAGE, "IMAGE"),
    SL(TMI_VID_FOV, "FIELD OF VIEW", "Camera field of view in degrees.", 60, 160, 5, TM_FMT_INT),
    SL(TMI_VID_GAMMA, "GAMMA", "Black level. Raise it if dark areas are too dark.", 0, 1, 0.05f, TM_FMT_PERCENT),
    SW(TMI_VID_ULTRAWIDE, "ULTRAWIDE MODE", "Condense the HUD towards the centre of a very wide screen."),
    SW(TMI_VID_PARTICLES, "ORIGINAL PARTICLES", "Keep the original visual effects."),
    CH(TMI_VID_FILTER, "TEXTURE FILTERING", "LINEAR is smooth; NEAREST keeps the original blocky look.",
       2, LABELS("LINEAR", "NEAREST"), VALUES(0, 1)),
    SW(TMI_VID_DITHER, "DITHERING", "Reduces colour banding."),
};

static const tm_spec blur[] = {
    CH(TMI_MB_MODE, "MOTION BLUR", "Blurs the 3D view while the camera moves; the HUD and the weapon stay sharp. OFF costs nothing. Changing a value below switches a preset to CUSTOM.",
       6, LABELS("OFF", "LOW", "MEDIUM", "HIGH", "ULTRA", "CUSTOM"), VALUES(0, 1, 2, 3, 4, 5)),
    SL(TMI_MB_INTENSITY, "BLUR INTENSITY", "How long the streaks are, up to 120%. Independent of the frame rate.", 0.1f, 1.2f, 0.1f, TM_FMT_PERCENT),
    SL(TMI_MB_CAMERA, "CAMERA BLUR", "Blur when you turn the camera. 0% removes it.", 0, 1, 0.25f, TM_FMT_PERCENT),
    SL(TMI_MB_MOVEMENT, "MOVEMENT BLUR", "Blur from walking and strafing (near things streak more). Vulkan only. Above 0% costs extra GPU time; 0% uses the faster rotation-only path.", 0, 1, 0.25f, TM_FMT_PERCENT),
    SL(TMI_MB_SAMPLES, "BLUR SAMPLES", "Most taps per pixel. Fast pixels use up to this many, slow ones fewer. Fewer is faster.", 4, 16, 2, TM_FMT_INT),
    SW(TMI_MB_QUALITY, "HIGH QUALITY BLUR", "ON also stops your weapon and nearer objects from smearing into the background (movement blur only). OFF is the cheapest."),
    CH(TMI_MB_SCALE, "EFFECT RESOLUTION", "Resolution of the blur itself: FULL is sharpest, HALF is the cheapest. The 3D image keeps its own resolution.",
       3, LABELS("FULL", "75%", "HALF"), VALUES(100, 75, 50)),
};

static const tm_spec audio[] = {
    SL(TMI_AUD_MASTER, "MASTER VOLUME", "Volume of everything.", 0, 1, 0.05f, TM_FMT_PERCENT),
    SL(TMI_AUD_MUSIC, "MUSIC VOLUME", "Volume of the background music.", 0, 1, 0.05f, TM_FMT_PERCENT),
};

static const tm_spec controls[] = {
    H(TMI_H_TOUCH, "TOUCH SCREEN"),
    CH(TMI_CTL_OVERLAY, "TOUCH BUTTONS", "ALWAYS ON: always show the on-screen controls. AUTO: hide them while a controller is connected. OFF: no on-screen controls.",
       3, LABELS("ALWAYS ON", "AUTO", "OFF"), VALUES(1, 0, 2)),
    SW(TMI_CTL_DYNAMIC, "DYNAMIC LEFT STICK", "ON: the movement stick appears under your thumb in the left part of the screen. OFF: it stays where it is drawn."),
    ACT(TMI_CTL_STICKZONE, "STICK ZONE AND SIZE", "Opens an editor: drag the handles to resize the area where the stick can start, and make the stick bigger or smaller. Needs the dynamic stick."),
    SL(TMI_CTL_TOUCHSENS, "CAMERA SENSITIVITY", "How far the camera turns for a swipe on the touch screen.", 0.2f, 3.0f, 0.1f, TM_FMT_FLOAT1),
    H(TMI_H_LOOK, "AIMING"),
    SW(TMI_CTL_INVERT, "INVERT LOOK", "Invert the vertical camera axis."),
    SW(TMI_CTL_AIMASSIST, "AIM ASSIST", "Helps to target enemies."),
    H(TMI_H_PAD, "GAMEPAD"),
    SL(TMI_CTL_PADSENS, "LOOK SENSITIVITY", "Camera speed with a controller.", 1, 10, 1, TM_FMT_INT),
    SL(TMI_CTL_PADACCEL, "LOOK ACCELERATION", "How much a hard push on the stick speeds the camera up.", 0, 1, 0.1f, TM_FMT_PERCENT),
    SW(TMI_CTL_RUMBLE, "VIBRATION", "Controller vibration."),
    ACT(TMI_CTL_BINDINGS, "KEY BINDINGS", "Change the controller and keyboard bindings."),
};

static const tm_spec access[] = {
    SW(TMI_ACC_HITMARK, "HITMARKERS", "Show a mark on the HUD when you hit an enemy."),
    SL(TMI_ACC_BACKDROP, "TEXT BACKDROP", "Opacity of the dark box behind HUD text.", 0, 1, 0.05f, TM_FMT_PERCENT),
    SW(TMI_ACC_COLORS, "ACCESSIBLE COLORS", "More distinct player colors for better visibility."),
    SW(TMI_ACC_FLASH, "FORBID WHITE FLASHES", "Replace the white screen flashes with another color."),
    CH(TMI_ACC_MONTH, "MONTH SPOOF", "Make the game believe it is another month (only the host matters).",
       13, LABELS("REAL TIME", "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"),
       VALUES(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)),
    SW(TMI_ACC_DOT, "CROSSHAIR DOT", "Add a dot to the middle of the crosshair."),
    SL(TMI_ACC_BOB, "CAMERA BOB", "Strength of the camera bob and roll. 0% turns it off.", 0, 1, 0.05f, TM_FMT_PERCENT),
    SW(TMI_ACC_DIVE, "SPRINT MELEE DIVE", "Dive when you use melee while sprinting."),
};

static const tm_spec advanced[] = {
    H(TMI_H_SYSTEM, "SYSTEM"),
    ACT(TMI_ADV_API, "GRAPHICS API", "Vulkan (default) or GLES2. The game never switches by itself. Restart the app to apply."),
    SW(TMI_ADV_PREROT, "PRE-ROTATION", "Vulkan: present with the screen rotation (good for Mali). Restart the app after changing."),
    SW(TMI_ADV_SHOWFPS, "SHOW FPS", "Show the real frame rate on screen."),
    SW(TMI_ADV_PROFILE, "PERFORMANCE LOG", "Write a performance log while playing."),
    ACT(TMI_ADV_BENCH, "OFFSCREEN BENCHMARK", "Renders 600 frames without presenting them. See renderer-benchmark.csv; use a demo for a repeatable load."),
    H(TMI_H_NETWORK, "NETWORK AND DEVELOPER"),
    SW(TMI_ADV_LAN, "LAN COMPATIBILITY", "ON = original networking. Try it if joining a co-op game fails."),
    ACT(TMI_ADV_CONSOLE, "OPEN CONSOLE", "Open the developer console."),
};

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))
const tm_category_spec tm_categories[] = {
    {"VIDEO", video, N(video)},
    {"BLUR", blur, N(blur)},
    {"AUDIO", audio, N(audio)},
    {"CONTROLS", controls, N(controls)},
    {"ACCESS", access, N(access)},
    {"ADVANCED", advanced, N(advanced)},
};
const int tm_category_count = (int)(sizeof(tm_categories) / sizeof(tm_categories[0]));

void tm_format_value(tm_fmt fmt, float v, char *out, int size) {
    switch (fmt) {
        case TM_FMT_PERCENT: snprintf(out, (size_t)size, "%d%%", (int)floorf(v * 100.f + 0.5f)); break;
        case TM_FMT_INT: snprintf(out, (size_t)size, "%d", (int)floorf(v + 0.5f)); break;
        case TM_FMT_FLOAT1: snprintf(out, (size_t)size, "%.1f", v); break;
        case TM_FMT_FLOAT2: snprintf(out, (size_t)size, "%.2f", v); break;
        default: out[0] = 0; break;
    }
}

const tm_spec *tm_find_spec(int id) {
    for (int c = 0; c < tm_category_count; c++)
        for (int i = 0; i < tm_categories[c].count; i++)
            if (tm_categories[c].specs[i].id == id) return &tm_categories[c].specs[i];
    return NULL;
}

int tm_choice_index_of(const tm_spec *spec, float value) {
    int best = -1;
    float best_d = 1e30f;
    for (int i = 0; i < spec->choice_count; i++) {
        const float d = fabsf(spec->choice_value[i] - value);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best_d < 0.01f ? best : -1;
}

void tm_content_build(tm_state *s, const tm_content_hooks *hooks) {
    tm_begin_items(s);
    if (s->cat_current >= 0 && s->cat_current < tm_category_count) {
        const tm_category_spec *cat = &tm_categories[s->cat_current];
        const tm_spec *pending_header = NULL;
        for (int i = 0; i < cat->count; i++) {
            const tm_spec *sp = &cat->specs[i];
            if (sp->kind == TM_HEADER) { pending_header = sp; continue; }   /* only if a row under it is shown */
            if (hooks && hooks->visible && !hooks->visible(hooks->user, sp->id)) continue;
            if (pending_header) { tm_add_item(s, TM_HEADER, pending_header->id, pending_header->label, ""); pending_header = NULL; }
            tm_item *it = tm_add_item(s, sp->kind, sp->id, sp->label, sp->hint);
            if (!it) break;
            it->vmin = sp->vmin;
            it->vmax = sp->vmax;
            it->vstep = sp->vstep;
            it->choice_count = sp->choice_count;
            for (int c = 0; c < sp->choice_count; c++) {
                snprintf(it->choice_label[c], sizeof(it->choice_label[c]), "%s", sp->choice_label[c]);
                it->choice_enabled[c] = 1;
            }
            if (hooks && hooks->fill) hooks->fill(hooks->user, sp->id, it);
            if (sp->kind == TM_SLIDER && !it->value_text[0]) tm_format_value(sp->fmt, it->value, it->value_text, sizeof(it->value_text));
            if (sp->kind == TM_CHOICE && !it->value_text[0] && it->choice_current >= 0 && it->choice_current < it->choice_count)
                snprintf(it->value_text, sizeof(it->value_text), "%s", it->choice_label[it->choice_current]);
        }
    }
    tm_end_items(s);
}
