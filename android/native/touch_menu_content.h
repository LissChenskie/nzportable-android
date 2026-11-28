// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
/* What the touch settings screen shows: categories, rows, labels, hints, ranges and the values a choice offers. Plain data (no
 * game code), so the host test renders the real texts at several screen sizes. The engine side (menu_touch_settings.c) says which
 * cvar each id is, whether a row is visible / enabled, and applies the events. */
#include "touch_menu.h"

typedef enum {
    TMI_NONE = 0,
    /* VIDEO */
    TMI_H_DISPLAY, TMI_VID_RESOLUTION, TMI_VID_FSR, TMI_VID_UISCALE, TMI_VID_FPS, TMI_VID_VSYNC, TMI_VID_PERF,
    TMI_H_IMAGE, TMI_VID_FOV, TMI_VID_GAMMA, TMI_VID_ULTRAWIDE, TMI_VID_PARTICLES, TMI_VID_FILTER, TMI_VID_DITHER,
    /* MOTION BLUR */
    TMI_MB_MODE, TMI_MB_INTENSITY, TMI_MB_CAMERA, TMI_MB_MOVEMENT, TMI_MB_SAMPLES, TMI_MB_QUALITY, TMI_MB_SCALE,
    /* AUDIO */
    TMI_AUD_MASTER, TMI_AUD_MUSIC,
    /* CONTROLS */
    TMI_H_TOUCH, TMI_CTL_OVERLAY, TMI_CTL_DYNAMIC, TMI_CTL_STICKZONE, TMI_CTL_TOUCHSENS,
    TMI_H_LOOK, TMI_CTL_INVERT, TMI_CTL_AIMASSIST,
    TMI_H_PAD, TMI_CTL_PADSENS, TMI_CTL_PADACCEL, TMI_CTL_RUMBLE, TMI_CTL_BINDINGS,
    /* ACCESSIBILITY */
    TMI_ACC_HITMARK, TMI_ACC_BACKDROP, TMI_ACC_COLORS, TMI_ACC_FLASH, TMI_ACC_MONTH, TMI_ACC_DOT, TMI_ACC_BOB, TMI_ACC_DIVE,
    /* ADVANCED */
    TMI_H_SYSTEM, TMI_ADV_API, TMI_ADV_PREROT, TMI_ADV_SHOWFPS, TMI_ADV_PROFILE, TMI_ADV_BENCH,
    TMI_H_NETWORK, TMI_ADV_LAN, TMI_ADV_CONSOLE
} tm_id;

typedef enum { TM_FMT_NONE, TM_FMT_PERCENT, TM_FMT_INT, TM_FMT_FLOAT1, TM_FMT_FLOAT2 } tm_fmt;

typedef struct {
    int id;
    tm_kind kind;
    const char *label, *hint;
    float vmin, vmax, vstep;
    tm_fmt fmt;
    int choice_count;
    const char *choice_label[TM_MAX_CHOICES];
    float choice_value[TM_MAX_CHOICES];
} tm_spec;

typedef struct { const char *name; const tm_spec *specs; int count; } tm_category_spec;

extern const tm_category_spec tm_categories[];
extern const int tm_category_count;

typedef struct {
    int (*visible)(void *user, int id);                 /* NULL: everything is shown */
    void (*fill)(void *user, int id, tm_item *item);    /* current value, enabled, value_text, choice_current / enabled */
    void *user;
} tm_content_hooks;

void tm_format_value(tm_fmt fmt, float v, char *out, int size);
const tm_spec *tm_find_spec(int id);
/* Fills the item list of the current category of s. */
void tm_content_build(tm_state *s, const tm_content_hooks *hooks);
/* The index of the choice whose value matches (nearest), -1 if none. */
int tm_choice_index_of(const tm_spec *spec, float value);
