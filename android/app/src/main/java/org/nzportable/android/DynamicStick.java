// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

/**
 * Floating ("dynamic") left movement stick: the stick appears where the thumb lands in the left part of the screen and
 * the movement is measured from that point. Pure geometry, no Android classes, so the host tests can run it
 * (tests/DynamicStickTest.java).
 *
 * The reported values follow the fixed RetroTouch stick exactly, because the native side (and the auto-sprint threshold)
 * expects them: x to the right, y DOWN (the engine negates it), length limited to 1 and a dead zone of 0.08 per axis. The
 * zone and the radius come from DynamicStickConfig (at the default size the radius equals the fixed stick's).
 */
final class DynamicStick {
    static final float DEAD_ZONE = 0.08f;

    private final DynamicStickConfig config;
    private boolean active;
    private int pointer = -1;
    private float baseX, baseY, knobX, knobY, radius, outX, outY;

    DynamicStick(DynamicStickConfig config) { this.config = config; }

    DynamicStickConfig config() { return config; }

    /** Starts the stick under this finger. False if a finger already holds it or the touch is outside the zone. */
    boolean begin(int pointerId, float x, float y, float width, float height) {
        if (active || !config.inZone(x, y, width, height)) return false;
        active = true;
        pointer = pointerId;
        baseX = knobX = x;
        baseY = knobY = y;
        radius = config.radius(width, height);
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
