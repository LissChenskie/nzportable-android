// SPDX-License-Identifier: GPL-2.0-or-later
#include "touch_menu.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

enum { M_NONE, M_PRESS, M_SCROLL, M_SLIDER, M_RAIL, M_BACK, M_SHEET_PRESS, M_SHEET_SCROLL, M_DEAD };

/* palette */
#define BG 10, 10, 12
#define PANEL 22, 22, 27
#define ROW_PRESSED 40, 40, 48
#define LINE 38, 38, 46
#define ACCENT 214, 48, 48
#define TEXT 240, 240, 240
#define DIM 150, 150, 160
#define OFF 92, 92, 100

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int maxi(int a, int b) { return a > b ? a : b; }
static int mini(int a, int b) { return a < b ? a : b; }
static int mm(const tm_state *s, float millimetres) { return maxi(1, (int)(millimetres * s->ppmm + 0.5f)); }

int tm_text_scale(float ppmm, float millimetres) {
    int scale = (int)(millimetres * ppmm / 8.f + 0.5f);   /* the font is 8 pixels at scale 1 */
    return scale < 1 ? 1 : scale;
}

void tm_init(tm_state *s) {
    memset(s, 0, sizeof(*s));
    s->pressed = s->focus = s->sheet_item = -1;
    s->ppmm = 12.f;
    tm_set_screen(s, 1920, 1080, 12.f);
}

void tm_set_screen(tm_state *s, int width, int height, float ppmm) {
    if (width < 16) width = 16;
    if (height < 16) height = 16;
    if (!(ppmm > 1.f)) ppmm = 12.f;
    s->w = width;
    s->h = height;
    s->ppmm = ppmm;
    s->pad = mm(s, 2.5f);
    s->slop = mm(s, 3.0f);
    s->rail_w = (int)clampf(36.f * ppmm, 0.20f * width, 0.32f * width);
    s->back_h = mm(s, 11.f);
    s->title_h = 0;                     /* the rail already names the category: the list gets the height */
    s->sheet_title_h = mm(s, 8.f);
    s->info_h = mm(s, 12.f);
    s->row_h = mm(s, 10.f);
    s->slider_h = mm(s, 15.f);
    s->header_h = mm(s, 7.5f);
    s->sheet_row_h = mm(s, 9.f);
    s->text_main = tm_text_scale(ppmm, 3.2f);
    s->text_small = tm_text_scale(ppmm, 2.5f);
    s->text_title = tm_text_scale(ppmm, 4.0f);
    /* The rail keeps every category on screen: shrink their height rather than scroll the rail. */
    s->cat_h = mm(s, 10.5f);
    s->list_x = s->rail_w;
    s->list_w = width - s->rail_w;
    s->list_y = s->title_h;
    s->list_h = maxi(1, height - s->title_h - s->info_h);
    if (s->cat_count > 0) {
        const int fit = (height - s->back_h) / s->cat_count;
        if (fit < s->cat_h) s->cat_h = maxi(mm(s, 6.5f), fit);
    }
    s->scroll_max = maxi(0, s->content_h - s->list_h);
    s->scroll = clampf(s->scroll, 0.f, s->scroll_max);
}

void tm_set_categories(tm_state *s, const char *const *names, int count, int current) {
    if (count > TM_MAX_CATS) count = TM_MAX_CATS;
    s->cat_count = count;
    for (int i = 0; i < count; i++) snprintf(s->cat_name[i], sizeof(s->cat_name[i]), "%s", names[i]);
    s->cat_current = current < 0 ? 0 : current >= count ? count - 1 : current;
    tm_set_screen(s, s->w, s->h, s->ppmm);
}

void tm_select_category(tm_state *s, int index) {
    if (index < 0 || index >= s->cat_count) return;
    s->cat_current = index;
    s->scroll = s->velocity = 0.f;
    s->focus = -1;
    s->sheet_item = -1;
}

void tm_begin_items(tm_state *s) { s->item_count = 0; }

tm_item *tm_add_item(tm_state *s, tm_kind kind, int id, const char *label, const char *hint) {
    if (s->item_count >= TM_MAX_ITEMS) return NULL;
    tm_item *it = &s->items[s->item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = kind;
    it->id = id;
    it->enabled = 1;
    it->choice_current = -1;
    snprintf(it->label, sizeof(it->label), "%s", label ? label : "");
    snprintf(it->hint, sizeof(it->hint), "%s", hint ? hint : "");
    return it;
}

void tm_end_items(tm_state *s) {
    int y = s->pad / 2;
    for (int i = 0; i < s->item_count; i++) {
        const tm_kind k = s->items[i].kind;
        s->item_y[i] = y;
        s->item_h[i] = k == TM_HEADER ? s->header_h : k == TM_SLIDER ? s->slider_h : s->row_h;
        y += s->item_h[i];
    }
    s->content_h = y + s->pad;
    s->scroll_max = maxi(0, s->content_h - s->list_h);
    s->scroll = clampf(s->scroll, 0.f, s->scroll_max);
    if (s->sheet_item >= s->item_count) s->sheet_item = -1;
    if (s->pressed >= s->item_count && (s->mode == M_PRESS || s->mode == M_SLIDER)) { s->pressed = -1; s->mode = M_DEAD; }
}

/* ---------- geometry ---------- */

void tm_item_rect(const tm_state *s, int i, int *x, int *y, int *w, int *h) {
    *x = s->list_x;
    *y = s->list_y + s->item_y[i] - (int)s->scroll;
    *w = s->list_w;
    *h = s->item_h[i];
}
void tm_back_rect(const tm_state *s, int *x, int *y, int *w, int *h) { *x = 0; *y = 0; *w = s->rail_w; *h = s->back_h; }
void tm_cat_rect(const tm_state *s, int i, int *x, int *y, int *w, int *h) { *x = 0; *y = s->back_h + i * s->cat_h; *w = s->rail_w; *h = s->cat_h; }

static int slider_margin(const tm_state *s) { return mm(s, 5.f); }
void tm_slider_track(const tm_state *s, int i, int *x0, int *x1, int *y) {
    int rx, ry, rw, rh;
    tm_item_rect(s, i, &rx, &ry, &rw, &rh);
    *x0 = rx + slider_margin(s);
    *x1 = rx + rw - slider_margin(s);
    *y = ry + (int)(rh * 0.68f);
}

static void str_copy(char *dst, int size, const char *src) {
    int n = (int)strlen(src);
    if (n >= size) n = size - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = 0;
}

static int in_rect(float x, float y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

/* The sheet: a centred panel with a title and a list that scrolls when it does not fit. */
typedef struct { int x, y, w, h, list_y, list_h, content_h; } sheet_geo;
static sheet_geo sheet_geometry(const tm_state *s) {
    sheet_geo g;
    const tm_item *it = &s->items[s->sheet_item < 0 ? 0 : s->sheet_item];
    const int n = s->sheet_item < 0 ? 0 : it->choice_count;
    g.w = (int)clampf(0.5f * s->w, mm(s, 60.f), mm(s, 110.f));
    if (g.w > s->w - 2 * s->pad) g.w = s->w - 2 * s->pad;
    g.content_h = n * s->sheet_row_h;
    const int max_h = (int)(0.95f * s->h);
    g.h = mini(max_h, s->sheet_title_h + g.content_h + s->pad);
    g.x = s->list_x + (s->list_w - g.w) / 2;
    if (g.x < s->pad) g.x = s->pad;
    g.y = (s->h - g.h) / 2;
    g.list_y = g.y + s->sheet_title_h;
    g.list_h = g.h - s->sheet_title_h - s->pad / 2;
    return g;
}
static void sheet_limits(tm_state *s) {
    if (s->sheet_item < 0) { s->sheet_scroll_max = 0; return; }
    const sheet_geo g = sheet_geometry(s);
    s->sheet_scroll_max = (float)maxi(0, g.content_h - g.list_h);
    s->sheet_scroll = clampf(s->sheet_scroll, 0.f, s->sheet_scroll_max);
}

int tm_sheet_open(const tm_state *s) { return s->sheet_item >= 0; }

static void open_sheet(tm_state *s, int item) {
    const tm_item *it = &s->items[item];
    s->sheet_item = item;
    s->sheet_scroll = 0.f;
    sheet_limits(s);
    /* Start with the current value in view. */
    if (it->choice_current > 0) {
        const sheet_geo g = sheet_geometry(s);
        s->sheet_scroll = clampf((float)(it->choice_current * s->sheet_row_h) - g.list_h * 0.4f, 0.f, s->sheet_scroll_max);
    }
}

void tm_sheet_list_rect(const tm_state *s, int *x, int *y, int *w, int *h) {
    if (s->sheet_item < 0) { *x = *y = *w = *h = 0; return; }
    const sheet_geo g = sheet_geometry(s);
    *x = g.x; *y = g.list_y; *w = g.w; *h = g.list_h;
}

void tm_sheet_entry_rect(const tm_state *s, int entry, int *x, int *y, int *w, int *h) {
    if (s->sheet_item < 0) { *x = *y = *w = *h = 0; return; }
    const sheet_geo g = sheet_geometry(s);
    *x = g.x;
    *w = g.w;
    *h = s->sheet_row_h;
    *y = g.list_y + entry * s->sheet_row_h - (int)s->sheet_scroll;
}

static int sheet_entry_at(const tm_state *s, float x, float y) {
    const sheet_geo g = sheet_geometry(s);
    if (!in_rect(x, y, g.x, g.list_y, g.w, g.list_h)) return -1;
    const int index = (int)((y - g.list_y + s->sheet_scroll) / s->sheet_row_h);
    return index >= 0 && index < s->items[s->sheet_item].choice_count ? index : -1;
}

static int item_at(const tm_state *s, float x, float y) {
    if (!in_rect(x, y, s->list_x, s->list_y, s->list_w, s->list_h)) return -1;
    for (int i = 0; i < s->item_count; i++) {
        const int top = s->list_y + s->item_y[i] - (int)s->scroll;
        if (y >= top && y < top + s->item_h[i]) return i;
    }
    return -1;
}

static int is_interactive(const tm_item *it) { return it->kind != TM_HEADER && it->kind != TM_INFO; }

static float slider_value(const tm_state *s, int i, float x) {
    const tm_item *it = &s->items[i];
    int x0, x1, y;
    tm_slider_track(s, i, &x0, &x1, &y);
    const float f = clampf((x - x0) / (float)maxi(1, x1 - x0), 0.f, 1.f);
    float v = it->vmin + f * (it->vmax - it->vmin);
    if (it->vstep > 0.f) v = it->vmin + floorf((v - it->vmin) / it->vstep + 0.5f) * it->vstep;
    return clampf(v, it->vmin, it->vmax);
}

void tm_toast(tm_state *s, const char *text, double t) {
    snprintf(s->toast, sizeof(s->toast), "%s", text);
    s->toast_until = t + 2.6;
}

int tm_back(tm_state *s) {
    if (s->sheet_item >= 0) {
        s->sheet_item = -1;
        s->mode = M_DEAD;
        return 1;
    }
    return 0;
}

/* ---------- input ---------- */

static void begin_scroll_estimate(tm_state *s, float x, float y, double t) {
    s->down_x = x;
    s->down_y = y;
    s->last_y = y;
    s->down_t = s->last_t = t;
    s->down_scroll = s->sheet_item >= 0 ? s->sheet_scroll : s->scroll;
}

int tm_touch(tm_state *s, tm_phase phase, float x, float y, double t, tm_event *ev) {
    memset(ev, 0, sizeof(*ev));
    ev->kind = TM_EV_NONE;
    const int sheet = s->sheet_item >= 0;

    if (phase == TM_DOWN) {
        s->pressed = -1;
        s->mode = M_NONE;
        begin_scroll_estimate(s, x, y, t);
        s->ignore_tap = fabsf(s->velocity) > 40.f;      /* a touch that catches a fling is not a tap */
        s->velocity = 0.f;
        if (sheet) {
            const sheet_geo g = sheet_geometry(s);
            if (in_rect(x, y, g.x, g.list_y, g.w, g.list_h)) { s->mode = M_SHEET_PRESS; s->pressed = sheet_entry_at(s, x, y); }
            else s->mode = M_SHEET_PRESS;                // outside: closes on release
            if (!in_rect(x, y, g.x, g.y, g.w, g.h)) s->pressed = -2;
            return 0;
        }
        if (x < s->rail_w) {
            if (y < s->back_h) { s->mode = M_BACK; s->pressed = 0; return 0; }
            const int c = (int)((y - s->back_h) / s->cat_h);
            if (c >= 0 && c < s->cat_count) { s->mode = M_RAIL; s->pressed = c; }
            return 0;
        }
        const int i = item_at(s, x, y);
        if (i >= 0) {
            const tm_item *it = &s->items[i];
            const int top = s->list_y + s->item_y[i] - (int)s->scroll;
            if (it->kind == TM_SLIDER && it->enabled && y >= top + 0.38f * s->item_h[i]) {
                s->mode = M_SLIDER;
                s->pressed = i;
                s->focus = i;
                s->slider_last = slider_value(s, i, x);
                ev->kind = TM_EV_SLIDER;
                ev->item = i;
                ev->value = s->slider_last;
                return 1;
            }
            s->pressed = is_interactive(it) ? i : -1;
        }
        s->mode = M_PRESS;
        return 0;
    }

    if (phase == TM_MOVE) {
        if (s->mode == M_SLIDER) {
            const float v = slider_value(s, s->pressed, x);
            if (v != s->slider_last) {
                s->slider_last = v;
                ev->kind = TM_EV_SLIDER;
                ev->item = s->pressed;
                ev->value = v;
                return 1;
            }
            return 0;
        }
        if (s->mode == M_PRESS || s->mode == M_SHEET_PRESS || s->mode == M_RAIL || s->mode == M_BACK) {
            const float dx = x - s->down_x, dy = y - s->down_y;
            if (dx * dx + dy * dy > (float)s->slop * s->slop) {
                const int vertical = fabsf(dy) >= fabsf(dx);
                if ((s->mode == M_PRESS || s->mode == M_SHEET_PRESS) && vertical) s->mode = s->mode == M_PRESS ? M_SCROLL : M_SHEET_SCROLL;
                else s->mode = M_DEAD;                 // sideways drag on a row / any drag on the rail: just cancel the tap
                s->pressed = -1;
            }
        }
        if (s->mode == M_SCROLL || s->mode == M_SHEET_SCROLL) {
            const float target = s->down_scroll + (s->down_y - y);
            const double dt = t - s->last_t;
            if (dt > 0.0005) {
                const float inst = (float)((s->last_y - y) / dt);
                s->velocity = s->velocity * 0.55f + inst * 0.45f;
                s->last_t = t;
                s->last_y = y;
            }
            if (s->mode == M_SCROLL) s->scroll = clampf(target, 0.f, s->scroll_max);
            else { s->sheet_scroll = clampf(target, 0.f, s->sheet_scroll_max); }
        }
        return 0;
    }

    /* UP / CANCEL */
    const int mode = s->mode, pressed = s->pressed, ignore = s->ignore_tap;
    s->mode = M_NONE;
    s->pressed = -1;
    if (mode == M_SCROLL || mode == M_SHEET_SCROLL) {
        if (phase == TM_CANCEL || t - s->last_t > 0.08 || fabsf(s->velocity) < 120.f) s->velocity = 0.f;
        s->velocity = clampf(s->velocity, -20.f * s->h, 20.f * s->h);
        if (mode == M_SHEET_SCROLL) s->velocity = 0.f;      // the sheet is short: no momentum there
        return 0;
    }
    if (phase == TM_CANCEL) return 0;
    if (mode == M_SLIDER) {
        ev->kind = TM_EV_SLIDER;
        ev->item = pressed;
        ev->value = slider_value(s, pressed, x);
        ev->commit = 1;
        return 1;
    }
    if (mode == M_BACK) {
        if (x < s->rail_w && y < s->back_h) { ev->kind = TM_EV_BACK; return 1; }
        return 0;
    }
    if (mode == M_RAIL) {
        const int c = x < s->rail_w ? (int)((y - s->back_h) / s->cat_h) : -1;
        if (c == pressed && y >= s->back_h) {
            if (c != s->cat_current) {
                tm_select_category(s, c);
                ev->kind = TM_EV_CATEGORY;
                ev->item = c;
                return 1;
            }
        }
        return 0;
    }
    if (mode == M_SHEET_PRESS) {
        const int up_entry = sheet_entry_at(s, x, y);
        const sheet_geo g = sheet_geometry(s);
        (void)g;
        if (pressed == -2) {          /* a tap that began outside the panel closes it; dragging out of it only cancels */
            if (!ignore) s->sheet_item = -1;
            return 0;
        }
        if (pressed >= 0 && up_entry == pressed && !ignore) {
            const tm_item *it = &s->items[s->sheet_item];
            if (!it->choice_enabled[pressed]) return 0;
            ev->kind = TM_EV_PICK;
            ev->item = s->sheet_item;
            ev->choice = pressed;
            s->sheet_item = -1;
            return 1;
        }
        return 0;
    }
    if (mode == M_PRESS && pressed >= 0 && !ignore) {
        if (item_at(s, x, y) != pressed) return 0;
        tm_item *it = &s->items[pressed];
        s->focus = pressed;
        if (!it->enabled) { ev->kind = TM_EV_DISABLED; ev->item = pressed; return 1; }
        ev->item = pressed;
        switch (it->kind) {
            case TM_SWITCH: ev->kind = TM_EV_SWITCH; ev->value = it->value > 0.5f ? 0.f : 1.f; return 1;
            case TM_ACTION: ev->kind = TM_EV_ACTION; return 1;
            case TM_CHOICE:
                if (it->choice_count > 0) open_sheet(s, pressed);
                return 0;
            default: return 0;
        }
    }
    return 0;
}

void tm_update(tm_state *s, double t) {
    double dt = t - s->now;
    if (s->now == 0.0 || dt < 0.0) dt = 0.0;
    if (dt > 0.1) dt = 0.1;
    s->now = t;
    sheet_limits(s);
    if (s->sheet_item < 0 && s->mode != M_SCROLL && s->velocity != 0.f) {   /* momentum of the list only */
        s->scroll += s->velocity * (float)dt;
        s->velocity *= expf(-4.2f * (float)dt);
        if (s->scroll <= 0.f) { s->scroll = 0.f; s->velocity = 0.f; }
        if (s->scroll >= s->scroll_max) { s->scroll = s->scroll_max; s->velocity = 0.f; }
        if (fabsf(s->velocity) < 25.f) s->velocity = 0.f;
    }
    if (s->toast[0] && t > s->toast_until) s->toast[0] = 0;
}

/* ---------- drawing ---------- */

typedef struct { int x0, y0, x1, y1; } clip_t;

static void fill_c(const tm_backend *b, const clip_t *c, int x, int y, int w, int h, int r, int g, int bl, int a) {
    int x1 = x + w, y1 = y + h;
    if (x < c->x0) x = c->x0;
    if (y < c->y0) y = c->y0;
    if (x1 > c->x1) x1 = c->x1;
    if (y1 > c->y1) y1 = c->y1;
    if (x1 > x && y1 > y) b->fill(b->user, x, y, x1 - x, y1 - y, r, g, bl, a);
}

/* A glyph row cannot be cut: text is drawn only when it lies entirely inside the clip. */
static void text_c(const tm_backend *b, const clip_t *c, int x, int y, const char *str, int scale, int r, int g, int bl, int a) {
    if (y < c->y0 || y + 8 * scale > c->y1 || x >= c->x1) return;
    b->text(b->user, x, y, str, scale, r, g, bl, a);
}

static void fit_text(const tm_backend *b, const char *src, int scale, int max_w, char *out, int out_size) {
    snprintf(out, out_size, "%s", src);
    if (max_w <= 0 || b->text_width(b->user, out, scale) <= max_w) return;
    int n = (int)strlen(out);
    while (n > 1) {
        out[--n] = 0;
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s..", out);
        if (b->text_width(b->user, tmp, scale) <= max_w) { snprintf(out, out_size, "%s", tmp); return; }
    }
}

/* Greedy word wrap into at most max_lines lines; the last line ends in ".." if text was left. */
static int wrap_text(const tm_backend *b, const char *src, int scale, int max_w, int max_lines, char lines[][208]) {
    int count = 0;
    const char *p = src;
    while (*p && count < max_lines) {
        char line[208] = "";
        while (*p) {
            char word[64];
            int wl = 0;
            while (p[wl] && p[wl] != ' ' && wl < 63) { word[wl] = p[wl]; wl++; }
            word[wl] = 0;
            char trial[280];
            const int line_len = (int)strlen(line);
            if (line_len + 1 + wl >= (int)sizeof(trial)) break;
            memcpy(trial, line, (size_t)line_len);
            int pos = line_len;
            if (line_len) trial[pos++] = ' ';
            memcpy(trial + pos, word, (size_t)wl);
            trial[pos + wl] = 0;
            if (line[0] && b->text_width(b->user, trial, scale) > max_w) break;
            str_copy(line, sizeof(line), trial);
            p += wl;
            while (*p == ' ') p++;
            if (!*p) break;
        }
        if (!line[0]) break;
        str_copy(lines[count++], 208, line);
    }
    if (*p && count > 0) {
        char last[208];
        fit_text(b, lines[count - 1], scale, max_w - b->text_width(b->user, "..", scale), last, sizeof(last));
        str_copy(lines[count - 1], 206, last);          /* room for the two dots */
        strcat(lines[count - 1], "..");
    }
    return count;
}

static void draw_row(const tm_state *s, const tm_backend *b, const clip_t *clip, int i) {
    const tm_item *it = &s->items[i];
    int x, y, w, h;
    tm_item_rect(s, i, &x, &y, &w, &h);
    if (y + h <= clip->y0 || y >= clip->y1) return;
    const int pressed = i == s->pressed && (s->mode == M_PRESS);
    const int pad = s->pad * 2;
    const int text_h = 8 * s->text_main;
    const int dim = !it->enabled;

    if (it->kind == TM_HEADER) {
        char up[56];
        fit_text(b, it->label, s->text_small, w - 2 * pad, up, sizeof(up));
        text_c(b, clip, x + pad, y + (h - 8 * s->text_small) / 2 + s->pad / 2, up, s->text_small, ACCENT, 255);
        return;
    }
    if (pressed) fill_c(b, clip, x, y, w, h, ROW_PRESSED, 255);
    fill_c(b, clip, x + pad, y + h - 1, w - 2 * pad, 1, LINE, 255);   /* separator */

    int right_w = 0;
    char value[64] = "";
    if (it->kind == TM_SWITCH) {
        const int sw_w = mm(s, 12.f), sw_h = mm(s, 6.2f);
        const int sx = x + w - pad - sw_w, sy = y + (h - sw_h) / 2, on = it->value > 0.5f;
        const int knob = sw_h - 2 * mm(s, 0.8f), gap = mm(s, 0.8f);
        fill_c(b, clip, sx, sy, sw_w, sw_h, on ? (dim ? 120 : 214) : 58, on ? (dim ? 60 : 48) : 58, on ? (dim ? 60 : 48) : 66, 255);
        fill_c(b, clip, on ? sx + sw_w - gap - knob : sx + gap, sy + gap, knob, knob, dim ? 150 : 245, dim ? 150 : 245, dim ? 150 : 245, 255);
        right_w = sw_w + pad;
    } else if (it->kind == TM_SLIDER) {
        fit_text(b, it->value_text, s->text_main, w / 2, value, sizeof(value));
        const int vw = b->text_width(b->user, value, s->text_main);
        text_c(b, clip, x + w - pad - vw, y + mm(s, 1.6f), value, s->text_main, dim ? 90 : 214, dim ? 90 : 48, dim ? 90 : 48, 255);
        right_w = vw + pad;
    } else if (it->kind == TM_CHOICE || it->kind == TM_ACTION) {
        const int arrow_w = b->text_width(b->user, ">", s->text_main) + mm(s, 1.f);
        if (it->kind == TM_CHOICE || it->value_text[0]) {
            fit_text(b, it->value_text, s->text_main, w * 45 / 100, value, sizeof(value));
            const int vw = b->text_width(b->user, value, s->text_main);
            text_c(b, clip, x + w - pad - arrow_w - vw - mm(s, 1.f), y + (h - text_h) / 2, value, s->text_main, dim ? 90 : 214, dim ? 90 : 48, dim ? 90 : 48, 255);
            right_w = vw + arrow_w + pad + mm(s, 1.f);
        } else right_w = arrow_w + pad;
        text_c(b, clip, x + w - pad - arrow_w, y + (h - text_h) / 2, ">", s->text_main, dim ? 90 : 150, dim ? 90 : 150, dim ? 96 : 160, 255);
    }
    char label[64];
    fit_text(b, it->label, s->text_main, w - 2 * pad - right_w - mm(s, 2.f), label, sizeof(label));
    const int ly = it->kind == TM_SLIDER ? y + mm(s, 1.6f) : y + (h - text_h) / 2;
    text_c(b, clip, x + pad, ly, label, s->text_main, dim ? 90 : 240, dim ? 90 : 240, dim ? 96 : 240, 255);

    if (it->kind == TM_SLIDER) {
        int x0, x1, ty;
        tm_slider_track(s, i, &x0, &x1, &ty);
        const int th = maxi(2, mm(s, 1.4f)), knob = mm(s, 5.5f);
        const float f = it->vmax > it->vmin ? clampf((it->value - it->vmin) / (it->vmax - it->vmin), 0.f, 1.f) : 0.f;
        const int kx = x0 + (int)(f * (x1 - x0));
        fill_c(b, clip, x0, ty - th / 2, x1 - x0, th, 58, 58, 66, 255);
        fill_c(b, clip, x0, ty - th / 2, kx - x0, th, dim ? 120 : 214, dim ? 60 : 48, dim ? 60 : 48, 255);
        const int active = s->mode == M_SLIDER && s->pressed == i;
        fill_c(b, clip, kx - knob / 2, ty - knob / 2, knob, knob, dim ? 150 : 245, dim ? 150 : 245, dim ? 150 : 245, 255);
        if (active) fill_c(b, clip, kx - knob / 2 - 2, ty - knob / 2 - 2, knob + 4, 3, ACCENT, 255);
    }
}

static void draw_sheet(const tm_state *s, const tm_backend *b) {
    const clip_t all = {0, 0, s->w, s->h};
    const sheet_geo g = sheet_geometry(s);
    const tm_item *it = &s->items[s->sheet_item];
    fill_c(b, &all, 0, 0, s->w, s->h, 0, 0, 0, 175);
    fill_c(b, &all, g.x - 2, g.y - 2, g.w + 4, g.h + 4, ACCENT, 255);
    fill_c(b, &all, g.x, g.y, g.w, g.h, PANEL, 255);
    char title[64];
    fit_text(b, it->label, s->text_main, g.w - 2 * s->pad * 2, title, sizeof(title));
    text_c(b, &all, g.x + s->pad * 2, g.y + (s->sheet_title_h - 8 * s->text_main) / 2, title, s->text_main, TEXT, 255);
    fill_c(b, &all, g.x, g.y + s->sheet_title_h - 1, g.w, 1, LINE, 255);
    const clip_t clip = {g.x, g.list_y, g.x + g.w, g.list_y + g.list_h};
    for (int i = 0; i < it->choice_count; i++) {
        const int ry = g.list_y + i * s->sheet_row_h - (int)s->sheet_scroll;
        if (ry + s->sheet_row_h <= clip.y0 || ry >= clip.y1) continue;
        const int on = it->choice_enabled[i], cur = i == it->choice_current;
        if (i == s->pressed && s->mode == M_SHEET_PRESS && on) fill_c(b, &clip, g.x, ry, g.w, s->sheet_row_h, ROW_PRESSED, 255);
        const int mark = mm(s, 3.f);
        if (cur) fill_c(b, &clip, g.x + s->pad * 2, ry + (s->sheet_row_h - mark) / 2, mark, mark, ACCENT, 255);
        char label[48];
        fit_text(b, it->choice_label[i], s->text_main, g.w - 4 * s->pad - mark - mm(s, 2.f), label, sizeof(label));
        text_c(b, &clip, g.x + s->pad * 2 + mark + mm(s, 2.5f), ry + (s->sheet_row_h - 8 * s->text_main) / 2, label, s->text_main,
               !on ? 90 : cur ? 214 : 240, !on ? 90 : cur ? 48 : 240, !on ? 96 : cur ? 48 : 240, 255);
        fill_c(b, &clip, g.x + s->pad * 2, ry + s->sheet_row_h - 1, g.w - 4 * s->pad, 1, LINE, 255);
    }
    if (s->sheet_scroll_max > 0.f) {   /* scroll indicator */
        const float vis = (float)g.list_h / (float)g.content_h;
        const int th = maxi(mm(s, 4.f), (int)(g.list_h * vis));
        const int ty = g.list_y + (int)((g.list_h - th) * (s->sheet_scroll / s->sheet_scroll_max));
        fill_c(b, &clip, g.x + g.w - mm(s, 1.2f), ty, mm(s, 0.8f), th, 120, 120, 130, 255);
    }
}

void tm_draw(const tm_state *s, const tm_backend *b) {
    const clip_t all = {0, 0, s->w, s->h};
    fill_c(b, &all, 0, 0, s->w, s->h, BG, 255);

    /* list */
    const clip_t list = {s->list_x, s->list_y, s->list_x + s->list_w, s->list_y + s->list_h};
    for (int i = 0; i < s->item_count; i++) draw_row(s, b, &list, i);
    if (s->scroll_max > 0.f) {
        const float vis = (float)s->list_h / (float)s->content_h;
        const int th = maxi(mm(s, 6.f), (int)(s->list_h * vis));
        const int ty = s->list_y + (int)((s->list_h - th) * (s->scroll / s->scroll_max));
        fill_c(b, &list, s->w - mm(s, 1.6f), ty, mm(s, 0.9f), th, 110, 110, 120, 255);
    }

    /* title bar (none by default: the rail names the category) */
    if (s->title_h > 0) {
        fill_c(b, &all, s->list_x, 0, s->list_w, s->title_h, PANEL, 255);
        fill_c(b, &all, s->list_x, s->title_h - 2, s->list_w, 2, ACCENT, 255);
        if (s->cat_count > 0)
            text_c(b, &all, s->list_x + s->pad * 2, (s->title_h - 8 * s->text_title) / 2, s->cat_name[s->cat_current], s->text_title, TEXT, 255);
    }

    /* info line: the hint of the row that was touched last */
    const int iy = s->h - s->info_h;
    fill_c(b, &all, s->list_x, iy, s->list_w, s->info_h, PANEL, 255);
    fill_c(b, &all, s->list_x, iy, s->list_w, 1, LINE, 255);
    if (s->focus >= 0 && s->focus < s->item_count && s->items[s->focus].hint[0]) {
        char lines[3][208];
        const int line_h = 8 * s->text_small + mm(s, 0.8f);
        const int max_lines = maxi(1, mini(3, (s->info_h - s->pad) / line_h));
        const int n = wrap_text(b, s->items[s->focus].hint, s->text_small, s->list_w - 4 * s->pad, max_lines, lines);
        for (int k = 0; k < n; k++) text_c(b, &all, s->list_x + 2 * s->pad, iy + s->pad / 2 + k * line_h, lines[k], s->text_small, DIM, 255);
    }

    /* rail */
    fill_c(b, &all, 0, 0, s->rail_w, s->h, PANEL, 255);
    fill_c(b, &all, s->rail_w - 1, 0, 1, s->h, LINE, 255);
    const int back_on = s->mode == M_BACK;
    if (back_on) fill_c(b, &all, 0, 0, s->rail_w, s->back_h, ROW_PRESSED, 255);
    {
        char back[16] = "< BACK";
        text_c(b, &all, s->pad * 2, (s->back_h - 8 * s->text_main) / 2, back, s->text_main, TEXT, 255);
    }
    fill_c(b, &all, 0, s->back_h - 1, s->rail_w, 1, LINE, 255);
    for (int i = 0; i < s->cat_count; i++) {
        int x, y, w, h;
        tm_cat_rect(s, i, &x, &y, &w, &h);
        const int cur = i == s->cat_current, press = s->mode == M_RAIL && s->pressed == i;
        if (cur) { fill_c(b, &all, x, y, w, h, 34, 24, 28, 255); fill_c(b, &all, x, y, mm(s, 1.4f), h, ACCENT, 255); }
        else if (press) fill_c(b, &all, x, y, w, h, ROW_PRESSED, 255);
        char name[28];
        fit_text(b, s->cat_name[i], s->text_main, w - s->pad * 3, name, sizeof(name));
        text_c(b, &all, x + s->pad * 2, y + (h - 8 * s->text_main) / 2, name, s->text_main, cur ? 255 : 200, cur ? 255 : 200, cur ? 255 : 205, 255);
    }

    /* toast */
    if (s->toast[0]) {
        char msg[96];
        fit_text(b, s->toast, s->text_small, s->list_w - 8 * s->pad, msg, sizeof(msg));
        const int tw = b->text_width(b->user, msg, s->text_small), bw = tw + 4 * s->pad, bh = 8 * s->text_small + 2 * s->pad;
        const int bx = s->list_x + (s->list_w - bw) / 2, by = iy - bh - s->pad;
        fill_c(b, &all, bx, by, bw, bh, 50, 50, 58, 255);
        fill_c(b, &all, bx, by, bw, 2, ACCENT, 255);
        text_c(b, &all, bx + 2 * s->pad, by + s->pad, msg, s->text_small, TEXT, 255);
    }

    if (s->sheet_item >= 0) draw_sheet(s, b);
}
