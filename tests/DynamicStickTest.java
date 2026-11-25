// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

/** Geometry of the floating left stick: zone, radius, reported values and finger ownership. */
public class DynamicStickTest {
    static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    static boolean near(float a, float b) { return Math.abs(a - b) < 1e-4f; }
    // A phone in landscape: 2400 x 1080.
    static final float W = 2400f, H = 1080f;
    static final DynamicStickConfig C = new DynamicStickConfig(); // defaults: zone 30% x 80%, size 100%

    public static void main(String[] args) {
        // Zone: the left 30% of the width, below the top 20%. Nothing to the right of it (RUN starts at ~31%).
        check(C.inZone(10, 900, W, H), "bottom left corner is inside");
        check(C.inZone(0.30f * W, 600, W, H), "the zone edge is inside");
        check(!C.inZone(0.31f * W, 600, W, H), "RUN's side is outside");
        check(!C.inZone(0.50f * W, 900, W, H), "the middle is outside");
        check(!C.inZone(100, 0.10f * H, W, H), "the top strip is outside");
        check(!C.inZone(-1, 600, W, H) && !C.inZone(100, 600, 0, 0), "off-screen / empty view");

        // The radius equals the fixed RetroTouch stick: half of (0.33 of the shorter side).
        check(near(C.radius(W, H), 0.33f * H * 0.5f), "radius matches the fixed stick");
        check(near(C.radius(H, W), C.radius(W, H)), "radius uses the shorter side");
        check(C.radius(0, 0) >= 1f, "radius never zero");

        DynamicStick s = new DynamicStick(C);
        check(!s.begin(1, 1200, 900, W, H), "a touch outside the zone does not start it");
        check(!s.isActive(), "still idle");

        // A touch starts the stick right under the finger: neutral, and the base is where it landed.
        check(s.begin(7, 300, 800, W, H), "start");
        check(s.isActive() && s.pointerId() == 7, "owned by pointer 7");
        check(near(s.baseX(), 300) && near(s.baseY(), 800) && near(s.x(), 0) && near(s.y(), 0), "base at the touch, neutral");
        final float r = s.radius();

        // Only the owning finger moves it; a second finger is ignored and cannot take it over.
        check(!s.begin(8, 200, 900, W, H), "second finger cannot start a second stick");
        check(!s.move(8, 400, 800) && near(s.x(), 0), "second finger cannot move it");

        // Right is +x, UP the screen is -y (the fixed stick reports y DOWN positive; the engine negates it).
        s.move(7, 300 + 0.5f * r, 800);
        check(near(s.x(), 0.5f) && near(s.y(), 0f), "half right");
        s.move(7, 300, 800 - 0.9f * r);
        check(near(s.x(), 0f) && near(s.y(), -0.9f), "up = negative y (forward after the engine's negation)");
        s.move(7, 300, 800 + 0.6f * r);
        check(near(s.y(), 0.6f), "down = positive y");

        // The dead zone is per axis and 0.08, like the fixed stick; just above it passes unchanged.
        s.move(7, 300 + 0.07f * r, 800 + 0.3f * r);
        check(near(s.x(), 0f) && near(s.y(), 0.3f), "x inside the dead zone is dropped");
        s.move(7, 300 + 0.09f * r, 800);
        check(near(s.x(), 0.09f), "x above the dead zone is kept");

        // Pulling far away is limited to length 1 (and the diagonal is normalised, not clipped per axis).
        s.move(7, 300 + 5 * r, 800);
        check(near(s.x(), 1f) && near(s.y(), 0f), "far right = 1");
        s.move(7, 300 + 3 * r, 800 - 3 * r);
        check(near(s.x(), 0.70710678f) && near(s.y(), -0.70710678f), "diagonal has length 1");
        check(near((float) Math.hypot(s.knobX() - s.baseX(), s.knobY() - s.baseY()), r), "the knob stays on the ring");

        // Auto-sprint (native side) needs length >= 0.88 and forward >= 0.66: reachable straight ahead.
        s.move(7, 300, 800 - 0.95f * r);
        check(Math.hypot(s.x(), s.y()) >= 0.88 && -s.y() >= 0.66, "sprint threshold reachable");

        // The finger may leave the zone while holding the stick.
        s.move(7, 2000, 100);
        check(s.isActive() && Math.hypot(s.x(), s.y()) <= 1.0001, "tracking continues outside the zone");

        // Lifting the wrong finger does nothing; lifting the right one resets everything.
        check(!s.end(8) && s.isActive(), "another finger lifting does not end it");
        check(s.end(7) && !s.isActive() && near(s.x(), 0) && near(s.y(), 0) && s.pointerId() == -1, "end");
        check(!s.end(7), "ending twice is harmless");
        check(!s.move(7, 500, 500) && near(s.x(), 0), "no movement after the end");

        // Cancel (pause, menu, layout editor) neutralises a held stick.
        check(s.begin(3, 100, 700, W, H), "restart");
        s.move(3, 100 + r, 700);
        s.cancel();
        check(!s.isActive() && near(s.x(), 0) && near(s.y(), 0), "cancel");

        // The same finger id can start it again at a new place: it is really floating.
        check(s.begin(3, 500, 400, W, H) && near(s.baseX(), 500) && near(s.baseY(), 400), "floats to a new place");

        // A portrait/odd screen still works.
        DynamicStick p = new DynamicStick(C);
        check(p.begin(1, 100, 1500, 1080, 2400), "portrait start");
        p.move(1, 100 + 0.5f * p.radius(), 1500);
        check(near(p.x(), 0.5f), "portrait radius");

        // The touch view is exactly the zone: its rectangle and inZone agree on every screen size, so a finger outside the view
        // (the camera finger, a button finger) can never be handed to the stick, whatever else is held.
        final int[][] screens = {{2400, 1080}, {1920, 1080}, {2340, 1080}, {2560, 1440}, {1280, 720}, {1080, 2400}, {800, 480}};
        for (int[] screen : screens) {
            final int sw = screen[0], sh = screen[1];
            final int zoneW = C.zoneWidthPx(sw), zoneH = C.zoneHeightPx(sh), top = C.zoneTopPx(sh);
            check(zoneW > 0 && zoneH > 0 && top + zoneH == sh, "zone rectangle ends at the bottom edge " + sw + "x" + sh);
            check(zoneW <= 0.30f * sw + 1e-3f, "the view is never wider than the zone " + sw + "x" + sh);
            check(top >= 0.20f * sh - 1e-3f, "the view never reaches into the top strip " + sw + "x" + sh);
            for (int x = 0; x < sw; x += 7) for (int y = 0; y < sh; y += 7) {
                final boolean insideView = x < zoneW && y >= top;
                if (insideView) check(C.inZone(x, y, sw, sh), "every point of the view is in the zone " + x + "," + y);
                else if (x > zoneW || y < top - 1) // (one pixel of rounding at the very edge is allowed)
                    check(!C.inZone(x, y, sw, sh), "a point outside the view is not a touch for the stick " + x + "," + y);
            }
            // The camera zone and RUN lie outside: the far side of the screen and 31% of the width.
            check(!(0.46f * sw < zoneW) && !(0.31f * sw < zoneW), "RUN and the look zone are outside the view " + sw);
        }

        configAndEditor();
        System.out.println("Floating left stick: zone, radius, dead zone, clamping, sign, finger ownership, editable zone and size, editor passed");
    }

    static void configAndEditor() {
        // --- configuration: defaults, limits, sanitising ---
        DynamicStickConfig d = new DynamicStickConfig();
        check(d.isDefault() && near(d.zoneRight, 0.30f) && near(d.zoneTop, 0.20f) && near(d.sizeScale, 1f), "defaults");
        DynamicStickConfig w = new DynamicStickConfig(9f, -3f, 99f);
        check(near(w.zoneRight, DynamicStickConfig.MAX_RIGHT) && near(w.zoneTop, DynamicStickConfig.MIN_TOP) && near(w.sizeScale, DynamicStickConfig.MAX_SIZE), "limits are applied");
        DynamicStickConfig n = new DynamicStickConfig(Float.NaN, Float.NaN, Float.NaN);
        check(near(n.zoneRight, DynamicStickConfig.MIN_RIGHT) && near(n.sizeScale, DynamicStickConfig.MIN_SIZE), "NaN from corrupt preferences is clamped");
        check(DynamicStickConfig.MAX_RIGHT < 0.46f, "the zone never reaches the camera zone (starts at 46%)");

        // --- the zone and the size are really used by the stick ---
        DynamicStickConfig big = new DynamicStickConfig(0.40f, 0.10f, 1.5f);
        check(big.inZone(0.39f * W, 0.15f * H, W, H) && !big.inZone(0.41f * W, 0.5f * H, W, H) && !big.inZone(100, 0.05f * H, W, H), "wider and taller zone");
        check(!C.inZone(0.39f * W, 0.5f * H, W, H) && !C.inZone(100, 0.15f * H, W, H), "the default zone is unchanged");
        check(near(big.radius(W, H), 1.5f * C.radius(W, H)), "size scales the radius");
        DynamicStick st = new DynamicStick(big);
        check(st.begin(1, 0.35f * W, 0.15f * H, W, H), "starts in the enlarged zone");
        check(near(st.radius(), 1.5f * C.radius(W, H)), "the stick uses the chosen radius");
        st.move(1, st.baseX() + 0.5f * st.radius(), st.baseY());
        check(near(st.x(), 0.5f), "half of the bigger radius is half deflection");
        DynamicStick small = new DynamicStick(new DynamicStickConfig(0.20f, 0.50f, 0.5f));
        check(!small.begin(1, 0.25f * W, 0.8f * H, W, H) && small.begin(1, 0.15f * W, 0.8f * H, W, H) && !small.begin(2, 0.1f * W, 0.3f * H, W, H), "narrower, shorter zone");
        // Changing the shared configuration takes effect for the next touch.
        DynamicStickConfig shared = new DynamicStickConfig();
        DynamicStick live = new DynamicStick(shared);
        check(!live.begin(1, 0.4f * W, 0.8f * H, W, H), "outside before the edit");
        shared.zoneRight = 0.45f;
        check(live.begin(1, 0.4f * W, 0.8f * H, W, H), "inside after the edit");

        // The hint ring sits inside the zone for every allowed zone.
        for (float r = DynamicStickConfig.MIN_RIGHT; r <= DynamicStickConfig.MAX_RIGHT + 1e-4f; r += 0.03f)
            for (float t = DynamicStickConfig.MIN_TOP; t <= DynamicStickConfig.MAX_TOP + 1e-4f; t += 0.1f) {
                DynamicStickConfig c = new DynamicStickConfig(r, t, 1f);
                check(c.inZone(c.hintX(W), c.hintY(H), W, H), "hint inside the zone " + r + "," + t);
            }

        // --- editor ---
        DynamicStickEditor e = new DynamicStickEditor(d);
        e.setScreen(W, H, 60f);
        check(e.config != d && e.config.sameAs(d), "the editor works on a copy");
        // Buttons: all on the right side, never inside the largest zone, never overlapping each other.
        DynamicStickEditor.Action[] all = {DynamicStickEditor.Action.SIZE_DOWN, DynamicStickEditor.Action.SIZE_UP, DynamicStickEditor.Action.RESET, DynamicStickEditor.Action.CANCEL, DynamicStickEditor.Action.DONE};
        for (int i = 0; i < all.length; i++) {
            float[] a = e.buttonRect(all[i]);
            check(a[0] > DynamicStickConfig.MAX_RIGHT * W && a[2] <= W && a[1] >= 0 && a[3] <= H, "button " + all[i] + " is on screen, right of any zone");
            check(e.buttonAt((a[0] + a[2]) / 2, (a[1] + a[3]) / 2) == all[i], "hit test " + all[i]);
            for (int j = i + 1; j < all.length; j++) {
                float[] b = e.buttonRect(all[j]);
                check(a[2] <= b[0] || b[2] <= a[0] || a[3] <= b[1] || b[3] <= a[1], "buttons " + all[i] + "/" + all[j] + " do not overlap");
            }
        }
        float[] label = e.sizeLabelRect();
        check(label[2] > label[0] && label[3] > label[1], "size label has room");

        // Dragging the right-edge handle changes only the width, without jumping to the finger.
        float hx = e.rightHandleX(), hy = e.rightHandleY();
        check(e.down(hx + 10, hy), "grab the right handle");
        e.move(0.40f * W + 10, hy + 300);
        check(near(e.config.zoneRight, 0.40f) && near(e.config.zoneTop, 0.20f), "only the width moved, no jump (offset kept)");
        check(e.up(0.40f * W + 10, hy + 300) == DynamicStickEditor.Action.NONE && !e.isDragging(), "release");
        // The top handle changes only the height.
        check(e.down(e.topHandleX(), e.topHandleY() - 5), "grab the top handle");
        e.move(e.topHandleX() + 500, 0.45f * H - 5);
        check(near(e.config.zoneTop, 0.45f) && near(e.config.zoneRight, 0.40f), "only the height moved");
        e.up(0, 0);
        // The corner moves both; limits hold however far the finger goes.
        check(e.down(e.cornerX(), e.cornerY()), "grab the corner");
        e.move(W * 3, -H);
        check(near(e.config.zoneRight, DynamicStickConfig.MAX_RIGHT) && near(e.config.zoneTop, DynamicStickConfig.MIN_TOP), "limits hold at the far corner");
        e.move(-W, H * 3);
        check(near(e.config.zoneRight, DynamicStickConfig.MIN_RIGHT) && near(e.config.zoneTop, DynamicStickConfig.MAX_TOP), "limits hold at the other end");
        e.up(0, 0);
        // A touch on nothing grabs nothing and changes nothing.
        DynamicStickConfig before = e.config.copy();
        check(!e.down(0.9f * W, 0.95f * H), "empty place");
        e.move(0.5f * W, 0.5f * H);
        check(e.config.sameAs(before), "nothing changed");
        e.up(0.9f * W, 0.95f * H);
        // Moving a handle with the finger far away from the buttons does not press them, and a press that slides off does nothing.
        float[] up = e.buttonRect(DynamicStickEditor.Action.SIZE_UP);
        float sBefore = e.config.sizeScale;
        check(e.down((up[0] + up[2]) / 2, (up[1] + up[3]) / 2), "press +");
        check(e.up(10, 10) == DynamicStickEditor.Action.NONE && near(e.config.sizeScale, sBefore), "sliding off the button cancels the press");

        // Size buttons: steps of 10%, rounded to 5%, limited to 50..160%.
        DynamicStickEditor s2 = new DynamicStickEditor(d);
        s2.setScreen(W, H, 60f);
        float[] plus = s2.buttonRect(DynamicStickEditor.Action.SIZE_UP), minus = s2.buttonRect(DynamicStickEditor.Action.SIZE_DOWN);
        for (int i = 0; i < 3; i++) { s2.down((plus[0] + plus[2]) / 2, (plus[1] + plus[3]) / 2); s2.up((plus[0] + plus[2]) / 2, (plus[1] + plus[3]) / 2); }
        check(near(s2.config.sizeScale, 1.3f), "three presses of + = 130%");
        for (int i = 0; i < 10; i++) { s2.down((plus[0] + plus[2]) / 2, (plus[1] + plus[3]) / 2); s2.up((plus[0] + plus[2]) / 2, (plus[1] + plus[3]) / 2); }
        check(near(s2.config.sizeScale, DynamicStickConfig.MAX_SIZE), "capped at 160%");
        for (int i = 0; i < 20; i++) { s2.down((minus[0] + minus[2]) / 2, (minus[1] + minus[3]) / 2); s2.up((minus[0] + minus[2]) / 2, (minus[1] + minus[3]) / 2); }
        check(near(s2.config.sizeScale, DynamicStickConfig.MIN_SIZE), "floored at 50%");

        // RESET, CANCEL and DONE.
        float[] reset = s2.buttonRect(DynamicStickEditor.Action.RESET), cancel = s2.buttonRect(DynamicStickEditor.Action.CANCEL), done = s2.buttonRect(DynamicStickEditor.Action.DONE);
        s2.config.zoneRight = 0.4f;
        s2.down((reset[0] + reset[2]) / 2, (reset[1] + reset[3]) / 2);
        check(s2.up((reset[0] + reset[2]) / 2, (reset[1] + reset[3]) / 2) == DynamicStickEditor.Action.RESET && s2.config.isDefault(), "reset restores the defaults");
        s2.down((done[0] + done[2]) / 2, (done[1] + done[3]) / 2);
        check(s2.up((done[0] + done[2]) / 2, (done[1] + done[3]) / 2) == DynamicStickEditor.Action.DONE, "DONE is reported to the caller");
        s2.down((cancel[0] + cancel[2]) / 2, (cancel[1] + cancel[3]) / 2);
        check(s2.up((cancel[0] + cancel[2]) / 2, (cancel[1] + cancel[3]) / 2) == DynamicStickEditor.Action.CANCEL, "CANCEL is reported to the caller");
        check(d.isDefault(), "the original configuration was never touched by the editor");

        // Narrow zone: where the handles overlap the corner wins and the zone can still be widened.
        DynamicStickEditor nz = new DynamicStickEditor(new DynamicStickConfig(DynamicStickConfig.MIN_RIGHT, DynamicStickConfig.MAX_TOP, 1f));
        nz.setScreen(W, H, 60f);
        check(nz.down(nz.cornerX(), nz.cornerY()), "corner grabbed in the smallest zone");
        nz.move(0.3f * W, 0.2f * H);
        check(near(nz.config.zoneRight, 0.3f) && near(nz.config.zoneTop, 0.2f), "the smallest zone can be enlarged");
        // Portrait and tiny screens do not break anything.
        DynamicStickEditor pe = new DynamicStickEditor(d);
        pe.setScreen(1080, 2400, 40f);
        check(pe.down(pe.cornerX(), pe.cornerY()), "portrait grab");
        pe.move(500, 900);
        check(pe.config.zoneRight <= DynamicStickConfig.MAX_RIGHT && pe.config.zoneTop >= DynamicStickConfig.MIN_TOP, "portrait stays in limits");
    }
}
