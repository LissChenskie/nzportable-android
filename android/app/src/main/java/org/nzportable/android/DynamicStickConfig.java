// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

/**
 * The player's choice for the floating left stick: how big the touch zone is and how big the stick is. One shared, mutable
 * instance is used by the stick, its views and the editor, so a change shows at once. Pure Java (host-tested).
 *
 * The zone is anchored at the bottom left of the screen: it spans the left {@code zoneRight} of the width and everything
 * below {@code zoneTop} of the height.
 */
final class DynamicStickConfig {
    static final float DEFAULT_RIGHT = 0.30f, DEFAULT_TOP = 0.20f, DEFAULT_SIZE = 1.0f;
    /** The camera zone starts at 46% of the width, so the stick zone may not reach further than 45%. */
    static final float MIN_RIGHT = 0.12f, MAX_RIGHT = 0.45f;
    static final float MIN_TOP = 0.05f, MAX_TOP = 0.70f;
    static final float MIN_SIZE = 0.50f, MAX_SIZE = 1.60f, SIZE_STEP = 0.10f;
    /** Radius of the default stick as a fraction of the shorter screen side (the fixed RetroTouch stick: 0.33 / 2). */
    static final float RADIUS_OF_SHORT_SIDE = 0.165f;

    float zoneRight = DEFAULT_RIGHT, zoneTop = DEFAULT_TOP, sizeScale = DEFAULT_SIZE;

    static float clamp(float value, float low, float high) {
        if (!(value >= low)) return low; // also catches NaN
        return value > high ? high : value;
    }

    DynamicStickConfig() {}

    DynamicStickConfig(float right, float top, float size) {
        zoneRight = right;
        zoneTop = top;
        sizeScale = size;
        sanitize();
    }

    DynamicStickConfig copy() { return new DynamicStickConfig(zoneRight, zoneTop, sizeScale); }

    void set(DynamicStickConfig other) {
        zoneRight = other.zoneRight;
        zoneTop = other.zoneTop;
        sizeScale = other.sizeScale;
        sanitize();
    }

    void reset() {
        zoneRight = DEFAULT_RIGHT;
        zoneTop = DEFAULT_TOP;
        sizeScale = DEFAULT_SIZE;
    }

    /** Keeps every value inside its limits (saved preferences can be anything). */
    void sanitize() {
        zoneRight = clamp(zoneRight, MIN_RIGHT, MAX_RIGHT);
        zoneTop = clamp(zoneTop, MIN_TOP, MAX_TOP);
        sizeScale = clamp(sizeScale, MIN_SIZE, MAX_SIZE);
    }

    boolean isDefault() {
        return zoneRight == DEFAULT_RIGHT && zoneTop == DEFAULT_TOP && sizeScale == DEFAULT_SIZE;
    }

    boolean sameAs(DynamicStickConfig other) {
        return zoneRight == other.zoneRight && zoneTop == other.zoneTop && sizeScale == other.sizeScale;
    }

    /** A touch that starts here starts the stick. */
    boolean inZone(float x, float y, float width, float height) {
        return width > 0f && height > 0f && x >= 0f && x <= zoneRight * width && y >= zoneTop * height && y <= height;
    }

    /*
     * The touch view is exactly the zone: Android hands a further finger to a view that already receives touches whenever
     * the finger lies inside that view, without asking the views below, so the view must not cover anything else.
     */
    int zoneWidthPx(int screenWidth) { return Math.max(1, (int) Math.floor(zoneRight * screenWidth)); }
    int zoneTopPx(int screenHeight) { return Math.min(screenHeight - 1, (int) Math.ceil(zoneTop * screenHeight)); }
    int zoneHeightPx(int screenHeight) { return Math.max(1, screenHeight - zoneTopPx(screenHeight)); }

    float radius(float width, float height) {
        return Math.max(1f, Math.min(width, height) * RADIUS_OF_SHORT_SIDE * sizeScale);
    }

    /** Where the idle hint ring is drawn: inside the zone, low enough for a thumb, and never outside the screen. */
    float hintX(float width) {
        return zoneRight * width * 0.5f;
    }

    float hintY(float height) {
        final float top = zoneTop * height;
        return top + (height - top) * 0.68f;
    }
}
