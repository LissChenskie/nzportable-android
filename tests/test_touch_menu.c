// SPDX-License-Identifier: GPL-2.0-or-later
// Touch settings screen: layout in millimetres, gestures, the sheet, the content tables and the drawing of the real texts at
// several screen sizes. Set TM_DUMP_DIR to also write every drawn state as a command list (tools: tests/render_touch_menu.py).
#include "touch_menu_content.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); exit(1); } } while (0)

/* ---------- recording backend ---------- */
typedef struct { int kind, x, y, w, h, scale; char text[200]; int r, g, b, a; } cmd_t;
static cmd_t cmds[20000];
static int ncmd, screen_w, screen_h;
static int fake_width(void *u, const char *s, int scale) { (void)u; return (int)strlen(s) * 7 * scale; }   /* a little wider than the real font */
static void fake_fill(void *u, int x, int y, int w, int h, int r, int g, int b, int a) {
    (void)u;
    CHECK(ncmd < 20000, "too many commands");
    cmd_t *c = &cmds[ncmd++];
    c->kind = 0; c->x = x; c->y = y; c->w = w; c->h = h; c->r = r; c->g = g; c->b = b; c->a = a;
}
static void fake_text(void *u, int x, int y, const char *s, int scale, int r, int g, int b, int a) {
    (void)u;
    CHECK(ncmd < 20000, "too many commands");
    cmd_t *c = &cmds[ncmd++];
    c->kind = 1; c->x = x; c->y = y; c->scale = scale; c->w = fake_width(NULL, s, scale); c->h = 8 * scale; c->r = r; c->g = g; c->b = b; c->a = a;
    snprintf(c->text, sizeof(c->text), "%s", s);
}
static const tm_backend backend = {NULL, fake_fill, fake_text, fake_width};

static void draw(const tm_state *s) {
    ncmd = 0;
    screen_w = s->w;
    screen_h = s->h;
    tm_draw(s, &backend);
}

static void dump(const tm_state *s, const char *name) {
    const char *dir = getenv("TM_DUMP_DIR");
    if (!dir) return;
    draw(s);
    char path[300];
    snprintf(path, sizeof(path), "%s/%s.cmd", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "size %d %d\n", s->w, s->h);
    for (int i = 0; i < ncmd; i++) {
        const cmd_t *c = &cmds[i];
        if (c->kind == 0) fprintf(f, "fill %d %d %d %d %d %d %d %d\n", c->x, c->y, c->w, c->h, c->r, c->g, c->b, c->a);
        else fprintf(f, "text %d %d %d %d %d %d %d|%s\n", c->x, c->y, c->scale, c->w, c->r, c->g, c->b, c->text);
    }
    fclose(f);
}

/* Every drawn thing lies inside the screen, and no two texts overlap (a label running into its value, a hint into the toast...). */
/* The toast is a snackbar: it covers the last row on purpose (opaque box), so it is the one text allowed to overlap others. */
static const char *toast_text = "Restart the app to apply this setting";
static void validate_drawing(const char *what) {
    for (int i = 0; i < ncmd; i++) {
        const cmd_t *c = &cmds[i];
        CHECK(c->x >= 0 && c->y >= 0 && c->x + c->w <= screen_w && c->y + c->h <= screen_h,
              "%s: command %d (%s) outside the %dx%d screen: %d,%d %dx%d '%s'", what, i, c->kind ? "text" : "fill", screen_w, screen_h, c->x, c->y, c->w, c->h, c->kind ? c->text : "");
    }
    /* A modal sheet covers the list with an opaque panel: only what is drawn after its dimming layer has to be free of overlaps. */
    int first = 0;
    for (int i = 0; i < ncmd; i++) if (cmds[i].kind == 0 && cmds[i].a == 175 && cmds[i].w == screen_w) first = i;
    for (int i = first; i < ncmd; i++) {
        if (cmds[i].kind != 1) continue;
        for (int j = i + 1; j < ncmd; j++) {
            if (cmds[j].kind != 1) continue;
            const cmd_t *a = &cmds[i], *b = &cmds[j];
            if (!strncmp(a->text, toast_text, 7) || !strncmp(b->text, toast_text, 7)) continue;   /* the text may be shortened with .. */
            const int overlap = a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
            CHECK(!overlap, "%s: texts overlap: '%s' (%d,%d %dx%d) and '%s' (%d,%d %dx%d)", what, a->text, a->x, a->y, a->w, a->h, b->text, b->x, b->y, b->w, b->h);
        }
    }
}

/* ---------- a model standing in for the engine's cvars ---------- */
static float model_switch[TM_MAX_ITEMS];
static int model_choice[TM_MAX_ITEMS];

static void test_fill(void *user, int id, tm_item *it) {
    (void)user;
    const tm_spec *sp = tm_find_spec(id);
    const int k = id % TM_MAX_ITEMS;
    it->enabled = (id % 7) != 3;     /* a few disabled rows */
    if (it->kind == TM_SWITCH) it->value = model_switch[k];
    else if (it->kind == TM_SLIDER) it->value = sp->vmin + (sp->vmax - sp->vmin) * (0.25f + 0.1f * (k % 5));
    else if (it->kind == TM_CHOICE) {
        it->choice_current = model_choice[k] % sp->choice_count;
        if (sp->choice_count > 2) it->choice_enabled[sp->choice_count - 1] = 0;
    }
    if (it->kind == TM_ACTION) snprintf(it->value_text, sizeof(it->value_text), "%s", id % 2 ? "VULKAN" : "");
}
static const tm_content_hooks hooks = {NULL, test_fill, NULL};

static void names(const char **out) { for (int i = 0; i < tm_category_count; i++) out[i] = tm_categories[i].name; }

static void setup(tm_state *s, int w, int h, float ppmm, int category) {
    const char *n[TM_MAX_CATS];
    names(n);
    tm_init(s);
    tm_set_categories(s, n, tm_category_count, category);
    tm_set_screen(s, w, h, ppmm);
    tm_content_build(s, &hooks);
}

/* ---------- gesture helpers: the items are rebuilt before every event, like the engine does every frame ---------- */
static double clock_s;
static tm_event send(tm_state *s, tm_phase p, float x, float y, double dt) {
    tm_event ev;
    clock_s += dt;
    tm_content_build(s, &hooks);
    tm_touch(s, p, x, y, clock_s, &ev);
    return ev;
}
/* Scrolls the row into the middle of the list if it is not fully visible (a thumb cannot tap what is off screen), then aims at it. */
static void centre(tm_state *s, int i, float *x, float *y) {
    int rx, ry, rw, rh;
    tm_item_rect(s, i, &rx, &ry, &rw, &rh);
    if (ry < s->list_y || ry + rh > s->list_y + s->list_h) {
        float target = (float)(s->item_y[i] + rh / 2 - s->list_h / 2);
        s->scroll = target < 0.f ? 0.f : target > s->scroll_max ? s->scroll_max : target;
        tm_item_rect(s, i, &rx, &ry, &rw, &rh);
    }
    *x = rx + rw / 2.f;
    *y = ry + rh / 2.f;
}
static int find_item(const tm_state *s, int id) {
    for (int i = 0; i < s->item_count; i++) if (s->items[i].id == id) return i;
    return -1;
}

typedef struct { const char *name; int w, h; float ppmm; } screen_t;
static const screen_t screens[] = {
    {"a51_2400x1080", 2400, 1080, 15.9f}, {"fhd_1920x1080", 1920, 1080, 14.5f}, {"tablet_2560x1600", 2560, 1600, 11.8f},
    {"hd_1280x720", 1280, 720, 9.6f}, {"wide_1600x720", 1600, 720, 10.6f}, {"small_960x540", 960, 540, 7.3f},
    {"portrait_1080x2400", 1080, 2400, 15.9f},
};
#define NSCREENS ((int)(sizeof(screens) / sizeof(screens[0])))

int main(void) {
    /* ===== 1. the content tables ===== */
    {
        int ids[200], n = 0;
        CHECK(tm_category_count > 0 && tm_category_count <= TM_MAX_CATS, "category count");
        for (int c = 0; c < tm_category_count; c++) {
            CHECK(strlen(tm_categories[c].name) > 0 && strlen(tm_categories[c].name) < 24, "category name length");
            CHECK(tm_categories[c].count > 0 && tm_categories[c].count <= TM_MAX_ITEMS, "items per category");
            for (int i = 0; i < tm_categories[c].count; i++) {
                const tm_spec *sp = &tm_categories[c].specs[i];
                CHECK(sp->id > 0, "id");
                for (int k = 0; k < n; k++) CHECK(ids[k] != sp->id, "duplicate id %d", sp->id);
                ids[n++] = sp->id;
                CHECK(strlen(sp->label) > 0 && strlen(sp->label) < 48, "label '%s'", sp->label);
                CHECK(strlen(sp->hint) < 200, "hint too long for %s", sp->label);
                if (sp->kind != TM_HEADER) CHECK(sp->kind == TM_ACTION || strlen(sp->hint) > 0 || sp->kind == TM_INFO, "%s needs a hint", sp->label);
                if (sp->kind == TM_SLIDER) {
                    CHECK(sp->vmax > sp->vmin && sp->vstep > 0.f, "slider range %s", sp->label);
                    const float steps = (sp->vmax - sp->vmin) / sp->vstep;
                    CHECK(fabsf(steps - floorf(steps + 0.5f)) < 0.01f, "slider %s: range is not a whole number of steps (%f)", sp->label, steps);
                    CHECK(steps >= 4.f && steps <= 100.f, "slider %s steps %f", sp->label, steps);
                }
                if (sp->kind == TM_CHOICE) {
                    CHECK(sp->choice_count >= 2 && sp->choice_count <= TM_MAX_CHOICES, "choices of %s", sp->label);
                    for (int a = 0; a < sp->choice_count; a++) {
                        CHECK(sp->choice_label[a] && strlen(sp->choice_label[a]) > 0 && strlen(sp->choice_label[a]) < 32, "choice label");
                        for (int b = a + 1; b < sp->choice_count; b++) CHECK(sp->choice_value[a] != sp->choice_value[b], "duplicate choice value in %s", sp->label);
                    }
                    CHECK(tm_choice_index_of(sp, sp->choice_value[sp->choice_count - 1]) == sp->choice_count - 1, "index lookup");
                    CHECK(tm_choice_index_of(sp, 12345.f) == -1, "no match");
                }
            }
        }
        char buf[16];
        tm_format_value(TM_FMT_PERCENT, 1.2f, buf, sizeof buf); CHECK(!strcmp(buf, "120%"), "percent '%s'", buf);
        tm_format_value(TM_FMT_PERCENT, 0.05f, buf, sizeof buf); CHECK(!strcmp(buf, "5%"), "percent small");
        tm_format_value(TM_FMT_FLOAT1, 1.5f, buf, sizeof buf); CHECK(!strcmp(buf, "1.5"), "float1");
        tm_format_value(TM_FMT_INT, 89.6f, buf, sizeof buf); CHECK(!strcmp(buf, "90"), "int");
        CHECK(tm_find_spec(TMI_VID_FOV) && !tm_find_spec(99999), "find spec");
        printf("content: %d settings in %d categories, all ids unique, all sliders on whole steps\n", n, tm_category_count);
    }

    /* ===== 2. metrics and drawing on every screen: sizes in millimetres, nothing outside the screen, no text on text ===== */
    for (int si = 0; si < NSCREENS; si++) {
        const screen_t *sc = &screens[si];
        const int landscape = sc->w >= sc->h;
        for (int cat = 0; cat < tm_category_count; cat++) {
            tm_state s;
            setup(&s, sc->w, sc->h, sc->ppmm, cat);
            const float row_mm = s.row_h / sc->ppmm, back_mm = s.back_h / sc->ppmm, cat_mm = s.cat_h / sc->ppmm;
            CHECK(row_mm > 9.0f && row_mm < 11.5f, "%s: row %.1f mm", sc->name, row_mm);
            CHECK(back_mm > 10.0f, "%s: back button %.1f mm", sc->name, back_mm);
            CHECK(cat_mm >= 6.0f, "%s: category row %.1f mm", sc->name, cat_mm);
            CHECK(s.back_h + s.cat_h * s.cat_count <= s.h, "%s: the rail does not fit", sc->name);
            CHECK(8 * s.text_main <= s.row_h * 0.85f, "%s: text taller than its row", sc->name);
            CHECK(s.list_h > 0 && s.list_w > 0, "%s: no list", sc->name);
            if (landscape) CHECK(s.list_h >= 0.45f * s.h, "%s: list only %d of %d px", sc->name, s.list_h, s.h);
            CHECK(s.rail_w <= 0.32f * s.w + 1, "%s: rail too wide", sc->name);
            draw(&s);
            for (int i = 0; landscape && i < ncmd; i++)       /* every category name is shown whole (the app is landscape-only) */
                if (cmds[i].kind == 1 && cmds[i].x < s.rail_w && strstr(cmds[i].text, "..") && strcmp(cmds[i].text, "< BACK"))
                    CHECK(0, "%s: category name cut off: '%s'", sc->name, cmds[i].text);
            char what[160];
            snprintf(what, sizeof what, "%s / %s", sc->name, tm_categories[cat].name);
            validate_drawing(what);
            /* scrolled to the end, with a hint and a toast: the lower part of the longest lists */
            s.scroll = s.scroll_max;
            if (s.item_count > 1) s.focus = s.item_count - 1;
            tm_toast(&s, toast_text, 0.0);
            draw(&s);
            snprintf(what, sizeof what, "%s / %s scrolled", sc->name, tm_categories[cat].name);
            validate_drawing(what);
            for (int i = 0; i < ncmd; i++) {       /* the toast sits above the info line, never on it */
                if (cmds[i].kind != 1 || strncmp(cmds[i].text, toast_text, 7)) continue;
                CHECK(cmds[i].y + cmds[i].h <= s.h - s.info_h, "%s: the toast runs into the info line", what);
            }
        }
    }
    printf("layout: %d screen sizes x %d categories, rows ~10 mm, nothing outside the screen, no overlapping text\n", NSCREENS, tm_category_count);

    /* ===== 3. gestures on the phone the project is tuned for ===== */
    const float PPMM = 15.9f;
    tm_state s;
    setup(&s, 2400, 1080, PPMM, 0);   /* VIDEO */
    CHECK(s.scroll_max > 0.f, "the video list is longer than the screen");
    float x, y;
    tm_event ev;
    clock_s = 10.0;

    /* a tap fires on RELEASE, not on touch */
    int sw = find_item(&s, TMI_VID_VSYNC);
    CHECK(sw >= 0, "vsync row");
    centre(&s, sw, &x, &y);
    ev = send(&s, TM_DOWN, x, y, 0.5);
    CHECK(ev.kind == TM_EV_NONE, "touching must not fire");
    ev = send(&s, TM_UP, x, y, 0.08);
    CHECK(ev.kind == TM_EV_SWITCH && s.items[ev.item].id == TMI_VID_VSYNC && ev.value == 1.f, "tap fires on release and turns it on (got kind %d item %d id %d value %f; sw=%d enabled=%d)", ev.kind, ev.item, ev.item >= 0 ? s.items[ev.item].id : -1, ev.value, sw, s.items[sw].enabled);
    model_switch[TMI_VID_VSYNC % TM_MAX_ITEMS] = 1.f;
    ev = send(&s, TM_DOWN, x, y, 0.5);
    ev = send(&s, TM_UP, x, y, 0.08);
    CHECK(ev.kind == TM_EV_SWITCH && ev.value == 0.f, "tapping an ON switch turns it off");

    /* finger jitter inside the slop is still a tap; a bigger move is not */
    ev = send(&s, TM_DOWN, x, y, 0.5);
    ev = send(&s, TM_MOVE, x + 0.4f * s.slop, y - 0.4f * s.slop, 0.02);
    ev = send(&s, TM_UP, x + 0.4f * s.slop, y - 0.4f * s.slop, 0.02);
    CHECK(ev.kind == TM_EV_SWITCH, "a little jitter is still a tap");
    const float scroll_at_start = s.scroll;
    ev = send(&s, TM_DOWN, x, y, 0.5);
    send(&s, TM_MOVE, x + 3 * s.slop, y, 0.03);
    ev = send(&s, TM_UP, x + 3 * s.slop, y, 0.02);
    CHECK(ev.kind == TM_EV_NONE && s.scroll == scroll_at_start, "a sideways drag cancels the tap and does not scroll");
    ev = send(&s, TM_DOWN, x, y, 0.5);
    send(&s, TM_MOVE, x, y, 0.02);
    ev = send(&s, TM_UP, x + s.slop * 0.5f, y, 0.02);
    CHECK(ev.kind == TM_EV_SWITCH, "release slightly off centre still taps");

    /* dragging scrolls by exactly the finger travel and never fires */
    s.scroll = 0.f;
    ev = send(&s, TM_DOWN, 1500, 700, 0.5);
    send(&s, TM_MOVE, 1500, 650, 0.05);
    send(&s, TM_MOVE, 1500, 500, 0.05);
    CHECK(fabsf(s.scroll - 200.f) < 1.f, "scroll follows the finger: %f", s.scroll);
    ev = send(&s, TM_UP, 1500, 500, 0.4);   /* held still before lifting: no momentum */
    CHECK(ev.kind == TM_EV_NONE && s.velocity == 0.f, "no event and no momentum after a still release");
    send(&s, TM_DOWN, 1500, 200, 0.5);
    send(&s, TM_MOVE, 1500, 1000, 0.1);
    send(&s, TM_UP, 1500, 1000, 0.5);
    CHECK(s.scroll == 0.f, "scroll stops at the top: %f", s.scroll);
    send(&s, TM_DOWN, 1500, 1000, 0.5);
    send(&s, TM_MOVE, 1500, 100, 0.1);
    send(&s, TM_MOVE, 1500, -3000, 0.1);
    send(&s, TM_UP, 1500, -3000, 0.5);
    CHECK(s.scroll == s.scroll_max, "scroll stops at the bottom");
    s.scroll = 0.f;

    /* a quick swipe keeps going and slows down, within bounds */
    send(&s, TM_DOWN, 1500, 900, 0.5);
    for (int i = 1; i <= 6; i++) send(&s, TM_MOVE, 1500, 900 - 55 * i, 0.012);
    send(&s, TM_UP, 1500, 570, 0.005);
    CHECK(s.velocity > 500.f, "swipe velocity %f", s.velocity);
    const float at_release = s.scroll;
    double t = clock_s;
    float last = s.scroll;
    int frames = 0;
    for (; frames < 400 && s.velocity != 0.f; frames++) {
        t += 1.0 / 60;
        tm_update(&s, t);
        CHECK(s.scroll >= last - 0.01f && s.scroll >= 0.f && s.scroll <= s.scroll_max + 0.01f, "momentum stays monotonic and in bounds");
        last = s.scroll;
    }
    CHECK(s.velocity == 0.f && frames > 5 && frames < 300, "momentum ends (%d frames)", frames);
    CHECK(s.scroll > at_release + 30.f, "it travelled on after the release: %f -> %f", at_release, s.scroll);
    clock_s = t;
    /* touching a moving list stops it and is not a tap */
    s.scroll = 0.f;
    s.velocity = 2000.f;
    int vs = find_item(&s, TMI_VID_VSYNC);
    centre(&s, vs, &x, &y);
    ev = send(&s, TM_DOWN, x, y, 0.5);
    CHECK(s.velocity == 0.f, "a touch stops the momentum");
    ev = send(&s, TM_UP, x, y, 0.05);
    CHECK(ev.kind == TM_EV_NONE, "the touch that stopped a fling is not a tap");
    ev = send(&s, TM_DOWN, x, y, 0.5);
    ev = send(&s, TM_UP, x, y, 0.05);
    CHECK(ev.kind == TM_EV_SWITCH, "the next tap works");

    /* hit regions: the info line (and the rail) never hit a row */
    s.scroll = 60.f;
    ev = send(&s, TM_DOWN, s.list_x + 400, s.h - s.info_h / 2.f, 0.5);
    ev = send(&s, TM_UP, s.list_x + 400, s.h - s.info_h / 2.f, 0.05);
    CHECK(ev.kind == TM_EV_NONE, "the info line is not a row");
    s.scroll = 0.f;

    /* headers are not interactive; the info line shows the hint of the row touched last */
    {
        const int hdr = 0;
        CHECK(s.items[hdr].kind == TM_HEADER, "first row is a header");
        centre(&s, hdr, &x, &y);
        ev = send(&s, TM_DOWN, x, y, 0.5);
        ev = send(&s, TM_UP, x, y, 0.05);
        CHECK(ev.kind == TM_EV_NONE, "a header does nothing");
        int fps = find_item(&s, TMI_VID_FPS);
        centre(&s, fps, &x, &y);
        send(&s, TM_DOWN, x, y, 0.5);
        send(&s, TM_UP, x, y, 0.05);
        CHECK(s.focus == fps && s.items[s.focus].hint[0], "the touched row's hint is shown");
        tm_back(&s);
    }

    /* ===== 4. sliders ===== */
    setup(&s, 2400, 1080, PPMM, 0);
    {
        int fov = find_item(&s, TMI_VID_FOV);
        CHECK(fov >= 0, "fov row");
        int x0, x1, ty;
        tm_slider_track(&s, fov, &x0, &x1, &ty);
        s.scroll = s.item_y[fov] - 100.f > 0 ? (float)(s.item_y[fov] - 100) : 0.f;
        if (s.scroll > s.scroll_max) s.scroll = s.scroll_max;
        tm_slider_track(&s, fov, &x0, &x1, &ty);
        float lo = 60, hi = 160;
        ev = send(&s, TM_DOWN, (x0 + x1) / 2.f, (float)ty, 0.5);
        CHECK(ev.kind == TM_EV_SLIDER && ev.item == fov && !ev.commit, "touching the track starts the slider at once");
        CHECK(fabsf(ev.value - 110.f) <= 5.f && fmodf(ev.value - lo, 5.f) == 0.f, "middle = 110 and on a 5-step: %f", ev.value);
        const float scroll_before = s.scroll;
        ev = send(&s, TM_MOVE, x1 + 500.f, (float)ty + 400.f, 0.03);
        CHECK(ev.kind == TM_EV_SLIDER && ev.value == hi, "dragging past the end clamps to the maximum, even far below the row");
        ev = send(&s, TM_MOVE, -300.f, (float)ty - 600.f, 0.03);
        CHECK(ev.kind == TM_EV_SLIDER && ev.value == lo, "and past the start to the minimum");
        CHECK(s.scroll == scroll_before, "a slider drag never scrolls the list");
        ev = send(&s, TM_MOVE, x0 + (x1 - x0) * 0.5f, (float)ty, 0.03);
        ev = send(&s, TM_MOVE, x0 + (x1 - x0) * 0.5f + 1.f, (float)ty, 0.03);
        CHECK(ev.kind == TM_EV_NONE, "no event while the snapped value stays the same");
        ev = send(&s, TM_UP, x0 + (x1 - x0) * 0.25f, (float)ty, 0.03);
        CHECK(ev.kind == TM_EV_SLIDER && ev.commit && fabsf(ev.value - 85.f) <= 5.f, "lifting commits the value: %f", ev.value);
        /* the label half of the row scrolls instead of grabbing the slider */
        int rx, ry, rw, rh;
        tm_item_rect(&s, fov, &rx, &ry, &rw, &rh);
        ev = send(&s, TM_DOWN, rx + rw / 2.f, ry + 0.15f * rh, 0.5);
        CHECK(ev.kind == TM_EV_NONE, "the label area does not start the slider");
        ev = send(&s, TM_UP, rx + rw / 2.f, ry + 0.15f * rh, 0.05);
        CHECK(ev.kind == TM_EV_NONE, "and a tap on a slider row does nothing");
        /* every value the finger can pick is on a step */
        for (int px = x0 - 50; px <= x1 + 50; px += 7) {
            send(&s, TM_DOWN, (float)px, (float)ty, 0.2);
            ev = send(&s, TM_UP, (float)px, (float)ty, 0.02);
            CHECK(ev.kind == TM_EV_SLIDER && ev.value >= lo && ev.value <= hi && fmodf(ev.value - lo, 5.f) == 0.f, "value %f is not on a step", ev.value);
        }
    }
    /* a disabled slider cannot be dragged: the tap explains why */
    setup(&s, 2400, 1080, PPMM, 1);   /* MOTION BLUR, TMI_MB_MOVEMENT id % 7 == 3 is disabled by the test hook */
    {
        int dis = -1;
        for (int i = 0; i < s.item_count; i++) if (s.items[i].kind == TM_SLIDER && !s.items[i].enabled) { dis = i; break; }
        CHECK(dis >= 0, "test needs a disabled slider");
        int x0, x1, ty;
        tm_slider_track(&s, dis, &x0, &x1, &ty);
        ev = send(&s, TM_DOWN, (x0 + x1) / 2.f, (float)ty, 0.5);
        CHECK(ev.kind == TM_EV_NONE, "a disabled slider does not start");
        ev = send(&s, TM_UP, (x0 + x1) / 2.f, (float)ty, 0.05);
        CHECK(ev.kind == TM_EV_DISABLED && ev.item == dis, "its tap reports DISABLED");
    }

    /* ===== 5. choices open a sheet ===== */
    setup(&s, 1280, 720, 9.6f, 4);   /* ACCESS on a small phone: MONTH SPOOF has 13 entries */
    {
        const int month = find_item(&s, TMI_ACC_MONTH);
        CHECK(month >= 0, "month row");
        s.scroll = 0.f;
        centre(&s, month, &x, &y);
        if (y > s.list_y + s.list_h - 10) { s.scroll = s.scroll_max; centre(&s, month, &x, &y); }
        model_choice[TMI_ACC_MONTH % TM_MAX_ITEMS] = 11;
        ev = send(&s, TM_DOWN, x, y, 0.5);
        ev = send(&s, TM_UP, x, y, 0.05);
        CHECK(ev.kind == TM_EV_NONE && tm_sheet_open(&s), "tapping a choice opens the sheet (no value changes yet)");
        draw(&s);
        validate_drawing("month sheet");
        dump(&s, "sheet_month_720p");
        /* the current entry (November) is in view at once */
        int visible_current = 0;
        for (int i = 0; i < ncmd; i++) if (cmds[i].kind == 1 && !strcmp(cmds[i].text, "NOVEMBER")) visible_current = 1;
        CHECK(visible_current, "the current value is scrolled into view");
        /* panel stays on screen and shorter than the screen */
        int fills_max_h = 0;
        for (int i = 0; i < ncmd; i++) if (cmds[i].kind == 0 && cmds[i].w < s.w && cmds[i].h > fills_max_h && cmds[i].h < s.h) fills_max_h = cmds[i].h;
        CHECK(fills_max_h <= 0.95f * s.h + 6, "the sheet fits on the screen");
        /* tap outside closes without a pick */
        ev = send(&s, TM_DOWN, 5, 5, 0.5);
        ev = send(&s, TM_UP, 5, 5, 0.05);
        CHECK(ev.kind == TM_EV_NONE && !tm_sheet_open(&s), "tapping outside closes it");
        /* open again, scroll the sheet by dragging, reach the first entry and pick it */
        centre(&s, month, &x, &y);
        send(&s, TM_DOWN, x, y, 0.5); send(&s, TM_UP, x, y, 0.05);
        CHECK(tm_sheet_open(&s), "open again");
        send(&s, TM_DOWN, s.w / 2.f, s.h / 2.f, 0.5);
        for (int i = 1; i <= 40; i++) send(&s, TM_MOVE, s.w / 2.f, s.h / 2.f + 60 * i, 0.016);
        ev = send(&s, TM_UP, s.w / 2.f, s.h / 2.f + 2400, 0.3);
        CHECK(ev.kind == TM_EV_NONE && tm_sheet_open(&s), "dragging the sheet scrolls it, it does not pick");
        CHECK(s.sheet_scroll == 0.f, "scrolled to the first entries");
        /* first entry: REAL TIME. Find it by drawing */
        draw(&s);
        int tx = 0, ty = 0;
        for (int i = 0; i < ncmd; i++) if (cmds[i].kind == 1 && !strcmp(cmds[i].text, "REAL TIME")) { tx = cmds[i].x + 5; ty = cmds[i].y + 3; }
        CHECK(tx > 0, "REAL TIME is visible after scrolling up");
        send(&s, TM_DOWN, (float)tx, (float)ty, 0.5);
        ev = send(&s, TM_UP, (float)tx, (float)ty, 0.05);
        CHECK(ev.kind == TM_EV_PICK && ev.item == month && ev.choice == 0 && !tm_sheet_open(&s), "a tap on an entry picks it and closes the sheet");
        /* the last entry is disabled by the hook: picking it does nothing and keeps the sheet open */
        centre(&s, month, &x, &y);
        send(&s, TM_DOWN, x, y, 0.5); send(&s, TM_UP, x, y, 0.05);
        send(&s, TM_DOWN, s.w / 2.f, s.h / 2.f, 0.5);
        for (int i = 1; i <= 40; i++) send(&s, TM_MOVE, s.w / 2.f, s.h / 2.f - 60 * i, 0.016);
        send(&s, TM_UP, s.w / 2.f, s.h / 2.f - 2400, 0.3);
        CHECK(s.sheet_scroll == s.sheet_scroll_max, "scrolled to the last entries");
        draw(&s);
        tx = 0;
        for (int i = 0; i < ncmd; i++) if (cmds[i].kind == 1 && !strcmp(cmds[i].text, "DECEMBER")) { tx = cmds[i].x + 5; ty = cmds[i].y + 3; }
        CHECK(tx > 0, "the last entry can be reached");
        send(&s, TM_DOWN, (float)tx, (float)ty, 0.5);
        ev = send(&s, TM_UP, (float)tx, (float)ty, 0.05);
        CHECK(ev.kind == TM_EV_NONE && tm_sheet_open(&s), "a disabled entry cannot be picked");
        CHECK(tm_back(&s) && !tm_sheet_open(&s) && !tm_back(&s), "Back closes the sheet, and only then falls through");
    }
    /* a choice with few entries: tap picks, and the sheet is only as tall as it needs */
    setup(&s, 2400, 1080, PPMM, 1);
    {
        const int mode = find_item(&s, TMI_MB_MODE);
        centre(&s, mode, &x, &y);
        model_choice[TMI_MB_MODE % TM_MAX_ITEMS] = 2;
        send(&s, TM_DOWN, x, y, 0.5); send(&s, TM_UP, x, y, 0.05);
        CHECK(tm_sheet_open(&s), "motion blur mode sheet");
        dump(&s, "sheet_blur_a51");
        draw(&s);
        validate_drawing("blur sheet");
    }

    /* ===== 6. the rail ===== */
    setup(&s, 2400, 1080, PPMM, 0);
    {
        int cx, cy, cw, ch;
        s.scroll = 120.f;
        tm_cat_rect(&s, 3, &cx, &cy, &cw, &ch);
        send(&s, TM_DOWN, cx + cw / 2.f, cy + ch / 2.f, 0.5);
        CHECK(s.cat_current == 0, "touching a category does not switch yet");
        ev = send(&s, TM_UP, cx + cw / 2.f, cy + ch / 2.f, 0.05);
        CHECK(ev.kind == TM_EV_CATEGORY && ev.item == 3 && s.cat_current == 3 && s.scroll == 0.f, "tap switches category and starts at the top");
        send(&s, TM_DOWN, cx + cw / 2.f, cy + ch / 2.f, 0.5);
        ev = send(&s, TM_UP, cx + cw / 2.f, cy + ch / 2.f, 0.05);
        CHECK(ev.kind == TM_EV_NONE, "tapping the current category does nothing");
        tm_cat_rect(&s, 1, &cx, &cy, &cw, &ch);
        send(&s, TM_DOWN, cx + cw / 2.f, cy + ch / 2.f, 0.5);
        send(&s, TM_MOVE, cx + cw / 2.f, cy + ch / 2.f + 4 * s.slop, 0.05);
        ev = send(&s, TM_UP, cx + cw / 2.f, cy + ch / 2.f + 4 * s.slop, 0.05);
        CHECK(ev.kind == TM_EV_NONE && s.cat_current == 3, "dragging on the rail cancels");
        int bx, by, bw, bh;
        tm_back_rect(&s, &bx, &by, &bw, &bh);
        send(&s, TM_DOWN, bx + bw / 2.f, by + bh / 2.f, 0.5);
        ev = send(&s, TM_UP, bx + bw / 2.f, by + bh / 2.f, 0.05);
        CHECK(ev.kind == TM_EV_BACK, "BACK fires on release");
        send(&s, TM_DOWN, bx + bw / 2.f, by + bh / 2.f, 0.5);
        ev = send(&s, TM_UP, bx + bw + 200.f, by + bh / 2.f, 0.05);
        CHECK(ev.kind == TM_EV_NONE, "releasing outside the button cancels BACK");
        send(&s, TM_DOWN, bx + bw / 2.f, by + bh / 2.f, 0.5);
        ev = send(&s, TM_CANCEL, bx + bw / 2.f, by + bh / 2.f, 0.05);
        CHECK(ev.kind == TM_EV_NONE, "a cancelled touch (system gesture) fires nothing");
    }

    /* ===== 7. toast, odd states ===== */
    tm_toast(&s, "hello", 100.0);
    tm_update(&s, 101.0);
    CHECK(s.toast[0] == 'h', "toast stays for a while");
    tm_update(&s, 103.0);
    CHECK(s.toast[0] == 0, "toast goes away");
    {
        tm_state e;
        tm_init(&e);
        tm_event z;
        tm_begin_items(&e);
        tm_end_items(&e);
        CHECK(!tm_touch(&e, TM_DOWN, 1000, 500, 1.0, &z) && !tm_touch(&e, TM_UP, 1000, 500, 1.1, &z), "empty list: no event, no crash");
        draw(&e);
        tm_update(&e, 2.0);
        tm_set_screen(&e, 0, 0, 0.f);
        draw(&e);
        CHECK(e.w >= 16 && e.h >= 16 && e.list_h > 0, "degenerate screen sizes are clamped");
        /* fewer rows than before while scrolled: the offset is pulled back */
        setup(&e, 2400, 1080, PPMM, 0);
        e.scroll = e.scroll_max;
        tm_begin_items(&e);
        tm_add_item(&e, TM_SWITCH, 1, "ONE", "");
        tm_end_items(&e);
        CHECK(e.scroll == 0.f, "scroll is clamped when the list shrinks");
        tm_item *over[TM_MAX_ITEMS + 5];
        tm_begin_items(&e);
        int added = 0;
        for (int i = 0; i < TM_MAX_ITEMS + 5; i++) { over[i] = tm_add_item(&e, TM_SWITCH, i, "X", ""); if (over[i]) added++; }
        tm_end_items(&e);
        CHECK(added == TM_MAX_ITEMS, "item overflow is refused, not written");
    }

    /* ===== 8. pictures of the real thing for a human to look at ===== */
    if (getenv("TM_DUMP_DIR")) {
        for (int si = 0; si < NSCREENS; si++)
            for (int cat = 0; cat < tm_category_count; cat++) {
                tm_state d;
                setup(&d, screens[si].w, screens[si].h, screens[si].ppmm, cat);
                if (d.item_count > 3) d.focus = 2;
                char name[200];
                snprintf(name, sizeof name, "%s_%d", screens[si].name, cat);
                dump(&d, name);
            }
        tm_state d;
        setup(&d, 2400, 1080, PPMM, 1);
        d.scroll = 0;
        int fov = d.item_count > 2 ? 2 : 0;
        d.mode = 3; d.pressed = fov;   /* slider being dragged */
        d.focus = fov;
        dump(&d, "a51_slider_drag");
        tm_toast(&d, toast_text, 0.0);
        d.mode = 0; d.pressed = -1;
        dump(&d, "a51_toast");
    }

    puts("touch menu: millimetre layout, tap on release, drag and momentum, sliders, sheet, rail, content tables, drawing on 7 screens passed");
    return 0;
}
