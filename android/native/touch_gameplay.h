/* SPDX-License-Identifier: GPL-2.0-or-later
 * Input policy shared by Android SDL bridge and portable host tests.
 * Return engine button commands, not assumed physical-key bindings:
 * imported game archives can ship different keyboard config.cfg files.
 */
#ifndef NZP_ANDROID_TOUCH_GAMEPLAY_H
#define NZP_ANDROID_TOUCH_GAMEPLAY_H
#include <math.h>
#include <string.h>

static const char *Touch_ActionCommand(const char *action)
{
    static const struct { const char *id, *command; } commands[] = {
        {"fire","+attack"}, {"aim","+aim"}, {"jump","+jump"},
        {"reload","+reload"}, {"use","+use"}, {"swap","+switch"},
        {"knife","+knife"}, {"nade","+grenade"}
    };
    if (!action) return NULL;
    for (unsigned i=0; i<sizeof(commands)/sizeof(commands[0]); ++i)
        if (strcmp(action,commands[i].id)==0)
            return commands[i].command;
    return NULL;
}

typedef struct {
    int auto_requested;
    int manual_requested;
    int active;
} TouchSprintState;

/* Starting sprint requires a substantial forward tilt. Hysteresis prevents
 * repeated sprint impulses near the outer ring of the joystick. */
static int TouchSprint_Update(TouchSprintState *state, float x, float y,
                              int enabled, int playing)
{
    float magnitude = sqrtf(x*x + y*y);
    if (!enabled || !playing) state->auto_requested=0;
    else if (!state->auto_requested) {
        if (magnitude >= 0.88f && y >= 0.66f)
            state->auto_requested=1;
    } else if (magnitude < 0.73f || y < 0.50f) {
        state->auto_requested=0;
    }
    int desired = playing && (state->auto_requested || state->manual_requested);
    if (desired == state->active) return 0;
    state->active=desired;
    return desired ? 23 : 24; /* Existing NZ:P QuakeC start/stop sprint. */
}
static int TouchSprint_Stop(TouchSprintState *state)
{
    int was_active=state->active;
    state->active=state->auto_requested=state->manual_requested=0;
    return was_active ? 24 : 0;
}
#endif
