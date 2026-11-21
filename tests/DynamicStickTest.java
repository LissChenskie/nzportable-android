// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

/** Geometry of the floating left stick: zone, radius, reported values and finger ownership. */
public class DynamicStickTest {
    static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    static boolean near(float a, float b) { return Math.abs(a - b) < 1e-4f; }
    // A phone in landscape: 2400 x 1080.
    static final float W = 2400f, H = 1080f;

    public static void main(String[] args) {
        // Zone: the left 30% of the width, below the top 20%. Nothing to the right of it (RUN starts at ~31%).
        check(DynamicStick.inZone(10, 900, W, H), "bottom left corner is inside");
        check(DynamicStick.inZone(0.30f * W, 600, W, H), "the zone edge is inside");
        check(!DynamicStick.inZone(0.31f * W, 600, W, H), "RUN's side is outside");
        check(!DynamicStick.inZone(0.50f * W, 900, W, H), "the middle is outside");
        check(!DynamicStick.inZone(100, 0.10f * H, W, H), "the top strip is outside");
        check(!DynamicStick.inZone(-1, 600, W, H) && !DynamicStick.inZone(100, 600, 0, 0), "off-screen / empty view");

        // The radius equals the fixed RetroTouch stick: half of (0.33 of the shorter side).
        check(near(DynamicStick.radiusFor(W, H), 0.33f * H * 0.5f), "radius matches the fixed stick");
        check(near(DynamicStick.radiusFor(H, W), DynamicStick.radiusFor(W, H)), "radius uses the shorter side");
        check(DynamicStick.radiusFor(0, 0) >= 1f, "radius never zero");

        DynamicStick s = new DynamicStick();
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
        DynamicStick p = new DynamicStick();
        check(p.begin(1, 100, 1500, 1080, 2400), "portrait start");
        p.move(1, 100 + 0.5f * p.radius(), 1500);
        check(near(p.x(), 0.5f), "portrait radius");

        // The touch view is exactly the zone: its rectangle and inZone agree on every screen size, so a finger outside the view
        // (the camera finger, a button finger) can never be handed to the stick, whatever else is held.
        final int[][] screens = {{2400, 1080}, {1920, 1080}, {2340, 1080}, {2560, 1440}, {1280, 720}, {1080, 2400}, {800, 480}};
        for (int[] screen : screens) {
            final int sw = screen[0], sh = screen[1];
            final int zoneW = DynamicStick.zoneWidthPx(sw), zoneH = DynamicStick.zoneHeightPx(sh), top = DynamicStick.zoneTopPx(sh);
            check(zoneW > 0 && zoneH > 0 && top + zoneH == sh, "zone rectangle ends at the bottom edge " + sw + "x" + sh);
            check(zoneW <= 0.30f * sw + 1e-3f, "the view is never wider than the zone " + sw + "x" + sh);
            check(top >= 0.20f * sh - 1e-3f, "the view never reaches into the top strip " + sw + "x" + sh);
            for (int x = 0; x < sw; x += 7) for (int y = 0; y < sh; y += 7) {
                final boolean insideView = x < zoneW && y >= top;
                if (insideView) check(DynamicStick.inZone(x, y, sw, sh), "every point of the view is in the zone " + x + "," + y);
                else if (x > zoneW || y < top - 1) // (one pixel of rounding at the very edge is allowed)
                    check(!DynamicStick.inZone(x, y, sw, sh), "a point outside the view is not a touch for the stick " + x + "," + y);
            }
            // The camera zone and RUN lie outside: the far side of the screen and 31% of the width.
            check(!(0.46f * sw < zoneW) && !(0.31f * sw < zoneW), "RUN and the look zone are outside the view " + sw);
        }

        System.out.println("Floating left stick: zone, radius, dead zone, clamping, sign, finger ownership passed");
    }
}
