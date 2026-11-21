// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

/**
 * Floating ("dynamic") left movement stick: the stick appears where the thumb lands in the left part of the screen and
 * the movement is measured from that point. Pure geometry, no Android classes, so the host tests can run it
 * (tests/DynamicStickTest.java).
 *
 * The reported values follow the fixed RetroTouch stick exactly, because the native side (and the auto-sprint threshold)
 * expects them: x to the right, y DOWN (the engine negates it), length limited to 1, a dead zone of 0.08 per axis, and the
 * same radius as the fixed stick (half of 0.33 of the shorter side).
 */
final class DynamicStick {
    /** A touch that starts left of this fraction of the screen width starts the stick (RUN begins at about 0.31). */
    static final float ZONE_RIGHT = 0.30f;
    /** ... and lower than this fraction of the height (the top edge stays free). */
    static final float ZONE_TOP = 0.20f;
    static final float DEAD_ZONE = 0.08f;
    /** Radius as a fraction of the shorter screen side: the fixed stick is 0.33 wide and RetroTouch halves it. */
    static final float RADIUS_OF_SHORT_SIDE = 0.165f;
    /** Where the fixed stick used to sit; the view draws a faint hint there while nothing touches the stick. */
    static final float HINT_X = 0.17f, HINT_Y = 0.75f;

    private boolean active;
    private int pointer = -1;
    private float baseX, baseY, knobX, knobY, radius, outX, outY;

    static boolean inZone(float x, float y, float width, float height) {
        return width > 0f && height > 0f && x >= 0f && x <= ZONE_RIGHT * width && y >= ZONE_TOP * height && y <= height;
    }

    /**
     * The touch view covers exactly the zone (and nothing else). That matters: Android hands a further finger to a view that
     * already receives touches whenever the finger lies inside its bounds, without asking the views below it. A full-screen
     * stick view would therefore swallow the camera finger as soon as the stick is held.
     */
    static int zoneWidthPx(int screenWidth) { return Math.max(1, (int) Math.floor(ZONE_RIGHT * screenWidth)); }
    static int zoneTopPx(int screenHeight) { return Math.min(screenHeight - 1, (int) Math.ceil(ZONE_TOP * screenHeight)); }
    static int zoneHeightPx(int screenHeight) { return Math.max(1, screenHeight - zoneTopPx(screenHeight)); }

    static float radiusFor(float width, float height) {
        return Math.max(1f, Math.min(width, height) * RADIUS_OF_SHORT_SIDE);
    }

    /** Starts the stick under this finger. False if a finger already holds it or the touch is outside the zone. */
    boolean begin(int pointerId, float x, float y, float width, float height) {
        if (active || !inZone(x, y, width, height)) return false;
        active = true;
        pointer = pointerId;
        baseX = knobX = x;
        baseY = knobY = y;
        radius = radiusFor(width, height);
        outX = outY = 0f;
        return true;
    }

    /** Moves the knob. Only the finger that started the stick counts, wherever it goes afterwards. */
    boolean move(int pointerId, float x, float y) {
        if (!active || pointerId != pointer) return false;
        float dx = (x - baseX) / radius, dy = (y - baseY) / radius;
        final float length = (float) Math.sqrt(dx * dx + dy * dy);
        if (length > 1f) {
            dx /= length;
            dy /= length;
        }
        knobX = baseX + dx * radius;
        knobY = baseY + dy * radius;
        outX = Math.abs(dx) < DEAD_ZONE ? 0f : dx;
        outY = Math.abs(dy) < DEAD_ZONE ? 0f : dy;
        return true;
    }

    /** The finger that started the stick left the screen. */
    boolean end(int pointerId) {
        if (!active || pointerId != pointer) return false;
        cancel();
        return true;
    }

    void cancel() {
        active = false;
        pointer = -1;
        outX = outY = 0f;
        knobX = baseX;
        knobY = baseY;
    }

    boolean isActive() { return active; }
    int pointerId() { return pointer; }
    float baseX() { return baseX; }
    float baseY() { return baseY; }
    float knobX() { return knobX; }
    float knobY() { return knobY; }
    float radius() { return radius; }
    /** Movement to report: x right, y down, each -1..1. */
    float x() { return outX; }
    float y() { return outY; }
}
