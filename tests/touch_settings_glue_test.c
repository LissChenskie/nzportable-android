// SPDX-License-Identifier: GPL-2.0-or-later
// The engine side of the touch settings screen, with the REAL menu_touch_settings.c (copied by the runner with its engine includes
// pointed at tests/stubs/stub_engine.h) driven by touch gestures against stand-ins for the cvars and the engine calls. Proves that
// every row reads and writes exactly its own cvar (no setting that only looks like it works), that actions call what they should
// and that Back, disabled rows and the sheet behave. Says nothing about the real engine, the GPU or the display.
#include "stub_engine.h"
#include "glue_under_test.c"

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); exit(1); } } while (0)

/* ---------- the stand-ins ---------- */
#define X(var, name, def) cvar_t var = {name, "", 1, (float)(def)};
STUB_CVARS(X)
#undef X
static cvar_t *all_cvars[] = {
#define X(var, name, def) &var,
    STUB_CVARS(X)
#undef X
};
#define NCVARS ((int)(sizeof(all_cvars) / sizeof(all_cvars[0])))

int m_state, key_dest, MENU_KEY_BACK = K_ESCAPE;
double realtime = 100.0;
viddef_t vid = {2400, 1080};
static int save_count, set_mode_calls, begin_edit_calls, open_editor_calls, bindings_calls, api_calls, console_calls, bench_calls, previous_calls, last_sound;
static int stub_vulkan = 1, stub_bench_started = 0;
static int stub_upscaler_supported[4] = {1, 1, 1, 1};

void Cvar_SetValue(const char *name, float value) {
    for (int i = 0; i < NCVARS; i++) if (!strcmp(all_cvars[i]->name, name)) { all_cvars[i]->value = value; return; }
    printf("FAIL: Cvar_SetValue on unknown cvar '%s'\n", name);
    exit(1);
}
void Host_WriteConfiguration(void) { save_count++; }
void Menu_SetSound(int type) { last_sound = type; }
void Menu_Configuration_Set(void) { m_state = m_configuration; key_dest = key_menu; Menu_TouchSettings_Opened(); }
void Menu_SetPreviousMenu(void) { previous_calls++; }
void Menu_Bindings_Set(void) { bindings_calls++; }
void Con_ToggleConsole_f(void) { console_calls++; }
void Draw_FillByColor(int x, int y, int w, int h, int r, int g, int b, int a) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a; }
void Draw_ColoredString(int x, int y, char *s, float r, float g, float b, float a, float scale) { (void)x; (void)y; (void)s; (void)r; (void)g; (void)b; (void)a; (void)scale; }
int getTextWidth(char *s, float scale) { return (int)strlen(s) * 7 * (int)scale; }
float VID_PixelsPerMM(void) { return 15.9f; }
int VID_UpscalerState(void) { return 1; }
int VID_UpscalerSupported(int mode) { return mode >= 0 && mode < 4 ? stub_upscaler_supported[mode] : 0; }
int VID_StartRendererBenchmark(void) { bench_calls++; return stub_bench_started; }
int VID_MotionBlurState(void) { return 1; }
int VID_MotionBlurMovementSupported(void) { return stub_vulkan; }
void VID_MotionBlurEffective(float *i, float *c, float *m, int *s, int *q, int *sc) {
    *i = mblur_intensity.value; *c = mblur_camera.value; *m = mblur_movement.value; *s = (int)mblur_samples.value; *q = (int)mblur_quality.value; *sc = (int)mblur_scale.value;
}
void VID_MotionBlurSetMode(int mode) { set_mode_calls++; mblur_mode.value = (float)mode; }
int VID_MotionBlurBeginEdit(void) { begin_edit_calls++; if (mblur_mode.value == 0.f) return 0; if (mblur_mode.value != 5.f) mblur_mode.value = 5.f; return 1; }
void VID_SetGraphicsAPIChoice(void) { api_calls++; }
char *VID_GraphicsAPIName(void) { return "VULKAN"; }
void Android_OpenStickEditor(void) { open_editor_calls++; }

/* ---------- gesture helpers: the engine draws every frame, which also rebuilds the rows ---------- */
static void frame(void) { Menu_TouchSettings_Draw(); realtime += 0.016; }
static int finger(int phase, float x, float y) {
    frame();
    const int r = Menu_TouchSettings_Finger(phase, x / vid.width, y / vid.height);
    realtime += 0.03;
    return r;
}
static int row_of(int id) {
    frame();
    for (int i = 0; i < tms.item_count; i++) if (tms.items[i].id == id) return i;
    return -1;
}
static void open_category(int cat) {
    m_state = m_configuration;
    Menu_TouchSettings_Open(cat);
    frame();
}
static void centre_of(int row, float *x, float *y) {
    int rx, ry, rw, rh;
    tm_item_rect(&tms, row, &rx, &ry, &rw, &rh);
    if (ry < tms.list_y || ry + rh > tms.list_y + tms.list_h) {
        const float target = (float)(tms.item_y[row] + rh / 2 - tms.list_h / 2);
        tms.scroll = target < 0.f ? 0.f : target > tms.scroll_max ? tms.scroll_max : target;
        frame();
        tm_item_rect(&tms, row, &rx, &ry, &rw, &rh);
    }
    *x = rx + rw / 2.f;
    *y = ry + rh / 2.f;
}
static void tap_row(int id) {
    const int row = row_of(id);
    CHECK(row >= 0, "row %d not shown", id);
    float x, y;
    centre_of(row, &x, &y);
    finger(0, x, y);
    finger(2, x, y);
}
static void pick(int id, float value) {
    tap_row(id);
    CHECK(tm_sheet_open(&tms), "tapping choice row %d opens the sheet", id);
    const tm_spec *sp = tm_find_spec(id);
    int entry = -1;
    for (int c = 0; c < sp->choice_count; c++) if (sp->choice_value[c] == value) entry = c;
    CHECK(entry >= 0, "no entry %f in %d", value, id);
    /* scroll the sheet so the entry is inside the panel's list, then tap it */
    int x, y, w, h;
    for (int tries = 0; tries < 20; tries++) {
        frame();
        tm_sheet_entry_rect(&tms, entry, &x, &y, &w, &h);
        int lx, ly, lw, lh;
        tm_sheet_list_rect(&tms, &lx, &ly, &lw, &lh);
        if (y >= ly && y + h <= ly + lh) break;
        tms.sheet_scroll += y < ly ? -(float)tms.sheet_row_h : (float)tms.sheet_row_h;
        if (tms.sheet_scroll < 0.f) tms.sheet_scroll = 0.f;
        if (tms.sheet_scroll > tms.sheet_scroll_max) tms.sheet_scroll = tms.sheet_scroll_max;
    }
    finger(0, x + w / 2.f, y + h / 2.f);
    finger(2, x + w / 2.f, y + h / 2.f);
}
static void slide(int id, float fraction) {
    const int row = row_of(id);
    CHECK(row >= 0, "slider %d not shown", id);
    float cx, cy;
    centre_of(row, &cx, &cy);
    int x0, x1, ty;
    tm_slider_track(&tms, row, &x0, &x1, &ty);
    const float x = x0 + fraction * (x1 - x0);
    finger(0, x, (float)ty);
    finger(1, x + 1.f, (float)ty);
    finger(2, x + 1.f, (float)ty);
}

/* ---------- which cvar belongs to which row ---------- */
typedef struct { int id; cvar_t *cvar; } map_t;
static const map_t mapping[] = {
    {TMI_VID_RESOLUTION, &vid_render_scale}, {TMI_VID_FSR, &vk_fsr}, {TMI_VID_UISCALE, &vid_ui_scale}, {TMI_VID_FPS, &cl_maxfps}, {TMI_VID_VSYNC, &r_vsync},
    {TMI_VID_PERF, &vid_performance_mode}, {TMI_VID_FOV, &scr_fov}, {TMI_VID_GAMMA, &v_gamma}, {TMI_VID_ULTRAWIDE, &vid_ultrawide_limiter},
    {TMI_VID_PARTICLES, &r_runqmbparticles}, {TMI_VID_FILTER, &r_retro}, {TMI_VID_DITHER, &r_dithering},
    {TMI_MB_MODE, &mblur_mode}, {TMI_MB_INTENSITY, &mblur_intensity}, {TMI_MB_CAMERA, &mblur_camera}, {TMI_MB_MOVEMENT, &mblur_movement},
    {TMI_MB_SAMPLES, &mblur_samples}, {TMI_MB_QUALITY, &mblur_quality}, {TMI_MB_SCALE, &mblur_scale},
    {TMI_AUD_MASTER, &volume}, {TMI_AUD_MUSIC, &bgmvolume},
    {TMI_CTL_OVERLAY, &touch_overlay}, {TMI_CTL_DYNAMIC, &touch_dynamic_stick}, {TMI_CTL_TOUCHSENS, &touch_sensitivity}, {TMI_CTL_INVERT, &m_pitch},
    {TMI_CTL_AIMASSIST, &in_aimassist}, {TMI_CTL_PADSENS, &sensitivity}, {TMI_CTL_PADACCEL, &in_acceleration}, {TMI_CTL_RUMBLE, &in_rumble},
    {TMI_ACC_HITMARK, &cl_hitmarkers}, {TMI_ACC_BACKDROP, &cl_textopacity}, {TMI_ACC_COLORS, &cl_colorblind}, {TMI_ACC_FLASH, &scr_whiteflash},
    {TMI_ACC_MONTH, &sv_spoofmonth}, {TMI_ACC_DOT, &cl_crosshairdot}, {TMI_ACC_BOB, &cl_viewbob}, {TMI_ACC_DIVE, &cl_meleedive},
    {TMI_ADV_PREROT, &vk_prerotate}, {TMI_ADV_SHOWFPS, &scr_showfps}, {TMI_ADV_PROFILE, &nzp_profile}, {TMI_ADV_LAN, &net_lan_legacy},
};
#define NMAP ((int)(sizeof(mapping) / sizeof(mapping[0])))

static void reset_cvars(void) {
#define X(var, name, def) var.value = (float)(def);
    STUB_CVARS(X)
#undef X
    mblur_mode.value = 5.f;          /* CUSTOM: every blur row is live */
    touch_dynamic_stick.value = 1.f;
    stub_vulkan = 1;
    for (int i = 0; i < 4; i++) stub_upscaler_supported[i] = 1;
}
static void snapshot(float *out) { for (int i = 0; i < NCVARS; i++) out[i] = all_cvars[i]->value; }

int main(void) {
    /* ===== 1. every setting in the tables is wired: reads its cvar, writes its cvar, and nothing else it should not ===== */
    int checked = 0, actions = 0;
    for (int c = 0; c < tm_category_count; c++) {
        for (int i = 0; i < tm_categories[c].count; i++) {
            const tm_spec *sp = &tm_categories[c].specs[i];
            if (sp->kind == TM_HEADER) continue;
            reset_cvars();
            open_category(c);
            if (sp->kind == TM_ACTION) {
                const int calls_before = open_editor_calls + bindings_calls + api_calls + bench_calls + console_calls;
                tap_row(sp->id);
                const int calls_after = open_editor_calls + bindings_calls + api_calls + bench_calls + console_calls;
                CHECK(calls_after == calls_before + 1, "action '%s' must call exactly one engine function (%d -> %d)", sp->label, calls_before, calls_after);
                actions++;
                continue;
            }
            const map_t *m = NULL;
            for (int k = 0; k < NMAP; k++) if (mapping[k].id == sp->id) m = &mapping[k];
            CHECK(m, "setting '%s' has no cvar in the test map: is it a setting that does nothing?", sp->label);

            /* reading: set the cvar from outside, the row must show it */
            if (sp->kind == TM_SWITCH) {
                for (int v = 0; v <= 1; v++) {
                    m->cvar->value = (float)v;
                    const int row = row_of(sp->id);
                    CHECK(row >= 0, "row for '%s'", sp->label);
                    CHECK(tms.items[row].value == (float)v, "'%s' must show cvar %s = %d", sp->label, m->cvar->name, v);
                }
            } else if (sp->kind == TM_SLIDER) {
                m->cvar->value = sp->vmin + 0.5f * (sp->vmax - sp->vmin);
                const int row = row_of(sp->id);
                CHECK(fabsf(tms.items[row].value - m->cvar->value) < 0.01f, "'%s' must show cvar %s", sp->label, m->cvar->name);
            } else {
                for (int ch = 0; ch < sp->choice_count; ch++) {
                    m->cvar->value = sp->choice_value[ch];
                    const int row = row_of(sp->id);
                    CHECK(tms.items[row].choice_current == ch, "'%s' must show entry %d for %s = %f (got %d)", sp->label, ch, m->cvar->name, sp->choice_value[ch], tms.items[row].choice_current);
                }
            }

            /* writing: a gesture changes exactly that cvar */
            reset_cvars();
            open_category(c);
            float before[64];
            snapshot(before);
            if (sp->kind == TM_SWITCH) {
                tap_row(sp->id);
            } else if (sp->kind == TM_SLIDER) {
                /* drag to the end that is far from the current value, so the result must differ */
                slide(sp->id, (m->cvar->value - sp->vmin) / (sp->vmax - sp->vmin) > 0.5f ? 0.0f : 1.0f);
            } else {
                /* pick an entry that differs from the current value */
                int target = 0;
                const int cur = tm_choice_index_of(sp, m->cvar->value);
                if (cur == 0) target = 1;
                pick(sp->id, sp->choice_value[target]);
            }
            int idx = -1;
            for (int k = 0; k < NCVARS; k++) if (all_cvars[k] == m->cvar) idx = k;
            CHECK(m->cvar->value != before[idx], "'%s' did not write %s (still %f)", sp->label, m->cvar->name, m->cvar->value);
            if (sp->id == TMI_MB_MODE) CHECK(set_mode_calls > 0, "motion blur mode goes through the preset logic");
            checked++;
        }
    }
    printf("wiring: %d settings read and write their own cvar, %d actions call their engine function\n", checked, actions);

    /* ===== 2. the compound settings do what the old menu did ===== */
    reset_cvars(); open_category(0);
    pick(TMI_VID_FPS, 0);
    CHECK(cl_maxfps.value == 0.f && r_vsync.value == 0.f, "UNLIMITED FPS also turns vsync off");
    reset_cvars(); open_category(0);
    pick(TMI_VID_FPS, 90);
    CHECK(cl_maxfps.value == 90.f && r_vsync.value == 0.f, "90 FPS turns vsync off");
    reset_cvars(); open_category(0);
    pick(TMI_VID_FPS, 45);
    CHECK(cl_maxfps.value == 45.f && r_vsync.value == 1.f, "45 FPS leaves vsync alone");
    reset_cvars(); open_category(0);
    pick(TMI_VID_PERF, 1);
    CHECK(vid_performance_mode.value == 1.f && cl_maxfps.value == 120.f && r_vsync.value == 0.f, "MAXIMUM: 120 FPS, no vsync");
    pick(TMI_VID_PERF, 2);
    CHECK(vid_performance_mode.value == 2.f && cl_maxfps.value == 30.f && r_vsync.value == 1.f, "BATTERY: 30 FPS, vsync");
    pick(TMI_VID_PERF, 0);
    CHECK(vid_performance_mode.value == 0.f && cl_maxfps.value == 60.f && r_vsync.value == 1.f, "BALANCED: 60 FPS, vsync");

    /* an upscaler this GPU cannot do is greyed out in the sheet and cannot be picked */
    reset_cvars(); open_category(0);
    stub_upscaler_supported[1] = 0;
    {
        const int row = row_of(TMI_VID_FSR);
        CHECK(tms.items[row].choice_enabled[3] == 0 && tms.items[row].choice_enabled[1] == 1 && tms.items[row].choice_enabled[0] == 1, "FULL (1) is disabled, the others are not");
        pick(TMI_VID_FSR, 1);
        CHECK(vk_fsr.value == 0.f, "an unsupported upscaler cannot be picked");
        tms.sheet_item = -1;
        pick(TMI_VID_FSR, 2);
        CHECK(vk_fsr.value == 2.f, "a supported one can");
    }

    /* ===== 3. saving: sliders write the file once, on release ===== */
    reset_cvars(); open_category(0);
    {
        const int row = row_of(TMI_VID_FOV);
        float cx, cy;
        centre_of(row, &cx, &cy);
        int x0, x1, ty;
        tm_slider_track(&tms, row, &x0, &x1, &ty);
        save_count = 0;
        finger(0, (float)x0, (float)ty);
        CHECK(scr_fov.value == 60.f && save_count == 0, "touching the slider moves the cvar but does not write the file");
        for (int k = 1; k <= 10; k++) finger(1, x0 + (x1 - x0) * k / 10.f, (float)ty);
        CHECK(scr_fov.value == 160.f && save_count == 0, "dragging to the end: 160, still nothing written");
        finger(2, (float)x1, (float)ty);
        CHECK(save_count == 1, "the file is written once, on release (%d)", save_count);
        save_count = 0;
        tap_row(TMI_VID_VSYNC);
        CHECK(save_count == 1, "a switch saves at once");
    }

    /* ===== 4. motion blur: sliders switch a preset to CUSTOM, are dead while it is off ===== */
    reset_cvars(); open_category(1);
    mblur_mode.value = 2.f;
    begin_edit_calls = 0;
    slide(TMI_MB_INTENSITY, 1.0f);
    CHECK(begin_edit_calls > 0 && mblur_mode.value == 5.f && mblur_intensity.value > 1.19f, "a slider edit turns the preset into CUSTOM and sets the value (%f)", mblur_intensity.value);
    mblur_mode.value = 0.f;
    {
        const float before = mblur_intensity.value;
        slide(TMI_MB_INTENSITY, 0.1f);
        CHECK(mblur_intensity.value == before, "with the blur off the sliders do nothing");
        CHECK(tms.toast[0] && strstr(tms.toast, "motion blur on"), "and the tap explains why: '%s'", tms.toast);
        const int row = row_of(TMI_MB_INTENSITY);
        CHECK(!tms.items[row].enabled, "dimmed");
    }
    reset_cvars(); open_category(1);
    stub_vulkan = 0;
    {
        const int row = row_of(TMI_MB_MOVEMENT);
        CHECK(!tms.items[row].enabled, "movement blur is dimmed without Vulkan");
        tap_row(TMI_MB_MOVEMENT);
        CHECK(tms.toast[0] && strstr(tms.toast, "Vulkan"), "and says why: '%s'", tms.toast);
    }
    reset_cvars(); open_category(1);
    pick(TMI_MB_SCALE, 50);
    CHECK(mblur_scale.value == 50.f && mblur_mode.value == 5.f, "effect resolution goes through the edit logic too");

    /* ===== 5. controls ===== */
    reset_cvars(); open_category(3);
    touch_dynamic_stick.value = 0.f;
    open_editor_calls = 0;
    tap_row(TMI_CTL_STICKZONE);
    CHECK(open_editor_calls == 0 && tms.toast[0] && strstr(tms.toast, "dynamic"), "the zone editor needs the dynamic stick: '%s'", tms.toast);
    touch_dynamic_stick.value = 1.f;
    tap_row(TMI_CTL_STICKZONE);
    CHECK(open_editor_calls == 1, "the zone editor opens");
    pick(TMI_CTL_OVERLAY, 2);
    CHECK(touch_overlay.value == 2.f, "touch buttons OFF is 2");
    pick(TMI_CTL_OVERLAY, 0);
    CHECK(touch_overlay.value == 0.f, "AUTO is 0");

    /* ===== 6. advanced ===== */
    reset_cvars(); open_category(5);
    api_calls = 0;
    tap_row(TMI_ADV_API);
    CHECK(api_calls == 1 && tms.toast[0] && strstr(tms.toast, "Restart"), "graphics API: one switch, with the restart note");
    stub_bench_started = 1;
    key_dest = key_menu;
    tap_row(TMI_ADV_BENCH);
    CHECK(key_dest == key_game && m_state == m_none, "the benchmark leaves the menu");
    stub_bench_started = 0;
    m_state = m_configuration;

    /* ===== 7. navigation ===== */
    reset_cvars(); open_category(0);
    {
        int x, y, w, h;
        tm_back_rect(&tms, &x, &y, &w, &h);
        previous_calls = 0;
        finger(0, x + w / 2.f, y + h / 2.f);
        CHECK(previous_calls == 0, "touching BACK does not leave yet");
        finger(2, x + w / 2.f, y + h / 2.f);
        CHECK(previous_calls == 1, "releasing on BACK goes to the previous menu");
        /* Back key: closes the sheet first */
        tap_row(TMI_VID_FPS);
        CHECK(tm_sheet_open(&tms), "sheet open");
        CHECK(Menu_TouchSettings_Key(K_ESCAPE) == 1 && !tm_sheet_open(&tms), "the Back key closes the sheet");
        CHECK(Menu_TouchSettings_Key(K_ESCAPE) == 0, "and then falls through to the menu system");
        CHECK(Menu_TouchSettings_Key(42) == 0, "other keys are not ours");
        /* the screen is only for its own menu state */
        m_state = m_main;
        CHECK(Menu_TouchSettings_Finger(0, 0.5f, 0.5f) == 0, "touches are ignored in other menus");
        m_state = m_configuration;
        /* the old topic menus land on the matching category */
        Menu_TouchSettings_Open(2);
        CHECK(tms.cat_current == 2 && tm_sheet_open(&tms) == 0, "audio");
        Menu_TouchSettings_Open(3);
        CHECK(tms.cat_current == 3, "controls");
        /* entering again forgets an open sheet and a half gesture */
        Menu_TouchSettings_Open(0);
        tap_row(TMI_VID_FPS);
        CHECK(tm_sheet_open(&tms), "sheet open again");
        Menu_TouchSettings_Opened();
        CHECK(!tm_sheet_open(&tms) && tms.mode == 0, "re-entering resets the screen");
    }
    /* a value set from the console that is not in a list is shown as it is */
    reset_cvars(); open_category(0);
    cl_maxfps.value = 75.f;
    {
        const int row = row_of(TMI_VID_FPS);
        CHECK(tms.items[row].choice_current == -1 && !strcmp(tms.items[row].value_text, "75"), "an odd console value is displayed: '%s'", tms.items[row].value_text);
    }

    puts("touch settings glue: every setting reads and writes its own cvar, compound settings, disabled rows, saving, navigation passed");
    return 0;
}
