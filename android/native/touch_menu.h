// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
/* Touch-first settings screen: a category rail on the left, a scrollable list of large rows on the right, a sheet for picking
 * one of several values, and an info line at the bottom.
 *
 * This file is the whole screen minus the engine: layout (in millimetres, so a row is as tall under the thumb on every phone),
 * scrolling with momentum, the gesture rules (a tap fires on RELEASE, a drag scrolls and never fires, a slider follows the
 * finger), hit-testing and drawing through a small backend. The engine side (menu_touch_settings.c) fills the items from the
 * game's cvars and applies the events. No game or GL/Vulkan dependency, so tests/test_touch_menu.c runs it on the host and can
 * render every state to a picture. */

#define TM_MAX_ITEMS 40
#define TM_MAX_CATS 8
#define TM_MAX_CHOICES 16

typedef enum { TM_HEADER, TM_SWITCH, TM_CHOICE, TM_SLIDER, TM_ACTION, TM_INFO } tm_kind;

typedef struct {
    tm_kind kind;
    int id;                         /* the engine's identifier of the setting */
    char label[48];
    char hint[200];                 /* shown in the info line when the row was touched */
    char value_text[48];            /* right side of a choice / action / slider row */
    int enabled;                    /* 0: dimmed; a tap shows the hint instead of acting */
    float value, vmin, vmax, vstep; /* switch: 0/1; slider */
    int choice_count, choice_current;
    char choice_label[TM_MAX_CHOICES][32];
    unsigned char choice_enabled[TM_MAX_CHOICES];
} tm_item;

typedef enum { TM_EV_NONE, TM_EV_BACK, TM_EV_CATEGORY, TM_EV_SWITCH, TM_EV_SLIDER, TM_EV_PICK, TM_EV_ACTION, TM_EV_DISABLED } tm_event_kind;

typedef struct {
    tm_event_kind kind;
    int item;       /* item index (TM_EV_CATEGORY: category index) */
    int choice;     /* TM_EV_PICK: chosen entry */
    float value;    /* TM_EV_SLIDER: snapped value; TM_EV_SWITCH: new state 0/1 */
    int commit;     /* TM_EV_SLIDER: 1 when the finger lifted (save now) */
} tm_event;

typedef enum { TM_DOWN, TM_MOVE, TM_UP, TM_CANCEL } tm_phase;

typedef struct {
    void *user;
    void (*fill)(void *user, int x, int y, int w, int h, int r, int g, int b, int a);
    void (*text)(void *user, int x, int y, const char *s, int scale, int r, int g, int b, int a);
    int (*text_width)(void *user, const char *s, int scale);
} tm_backend;

typedef struct {
    int w, h;
    float ppmm;                     /* physical pixels per millimetre */
    /* metrics (pixels), see tm_set_screen */
    int pad, slop, rail_w, back_h, cat_h, title_h, info_h, row_h, slider_h, header_h, sheet_row_h, sheet_title_h;
    int text_main, text_small, text_title;
    int list_x, list_y, list_w, list_h;

    int cat_count, cat_current;
    char cat_name[TM_MAX_CATS][24];
    int item_count;
    tm_item items[TM_MAX_ITEMS];
    int item_y[TM_MAX_ITEMS], item_h[TM_MAX_ITEMS];
    int content_h;

    float scroll, scroll_max, velocity;
    double now;

    int mode;                       /* current gesture */
    int pressed;                    /* item / category / sheet entry under the finger, -1 none */
    float down_x, down_y, down_scroll, last_y, slider_last;
    double down_t, last_t;
    int ignore_tap;
    int focus;                      /* item whose hint is shown, -1 none */

    int sheet_item;                 /* item whose choices are open, -1 none */
    float sheet_scroll, sheet_scroll_max;

    char toast[96];
    double toast_until;
} tm_state;

void tm_init(tm_state *s);
void tm_set_screen(tm_state *s, int width, int height, float pixels_per_mm);
void tm_set_categories(tm_state *s, const char *const *names, int count, int current);

/* Rebuild the item list every frame (cheap); scroll, gesture and sheet state live in the state, not in the items. */
void tm_begin_items(tm_state *s);
tm_item *tm_add_item(tm_state *s, tm_kind kind, int id, const char *label, const char *hint);
void tm_end_items(tm_state *s);

/* Input: one finger at a time, times in seconds. Returns 1 and fills *ev when something happened. */
int tm_touch(tm_state *s, tm_phase phase, float x, float y, double t, tm_event *ev);
void tm_update(tm_state *s, double t);      /* momentum, toast */
int tm_back(tm_state *s);                   /* Back key: closes the sheet; returns 1 if it did something */
void tm_toast(tm_state *s, const char *text, double t);
void tm_select_category(tm_state *s, int index);
int tm_sheet_open(const tm_state *s);

void tm_draw(const tm_state *s, const tm_backend *b);

/* Geometry for the glue and the tests (screen pixels). */
void tm_item_rect(const tm_state *s, int index, int *x, int *y, int *w, int *h);
void tm_back_rect(const tm_state *s, int *x, int *y, int *w, int *h);
void tm_cat_rect(const tm_state *s, int index, int *x, int *y, int *w, int *h);
void tm_slider_track(const tm_state *s, int index, int *x0, int *x1, int *y);
/* Rectangle of an entry of the open sheet (screen pixels); entries scrolled out of the panel are outside its list area. */
/* The part of the open sheet that shows entries (screen pixels). */
void tm_sheet_list_rect(const tm_state *s, int *x, int *y, int *w, int *h);
void tm_sheet_entry_rect(const tm_state *s, int entry, int *x, int *y, int *w, int *h);
int tm_text_scale(float ppmm, float mm);
