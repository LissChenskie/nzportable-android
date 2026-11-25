// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

/**
 * Logic of the stick editor screen (see DynamicStickEditorView): drag the two edges of the zone by their handles, change the
 * size with - / +, reset, cancel or accept. Pure Java, host-tested. It works on its own copy of the configuration, so cancelling
 * leaves the real one untouched.
 *
 * Layout (landscape, fractions of the screen): the zone is the highlighted rectangle at the bottom left with a handle on its
 * right edge, one on its top edge and one on the corner; the buttons are a column on the right side, which the zone (at most
 * 45% wide) never reaches.
 */
final class DynamicStickEditor {
    enum Action { NONE, SIZE_DOWN, SIZE_UP, RESET, CANCEL, DONE }

    private enum Drag { NONE, CORNER, RIGHT, TOP }

    /** The configuration being edited. */
    final DynamicStickConfig config;

    private float width, height, handleRadius = 40f;
    private Drag drag = Drag.NONE;
    private Action pressed = Action.NONE;
    private float grabDx, grabDy;

    DynamicStickEditor(DynamicStickConfig start) { config = start.copy(); }

    void setScreen(float screenWidth, float screenHeight, float handleRadiusPx) {
        width = screenWidth;
        height = screenHeight;
        handleRadius = Math.max(8f, handleRadiusPx);
    }

    float screenWidth() { return width; }
    float screenHeight() { return height; }
    float handleRadius() { return handleRadius; }

    // Handles: the corner moves both edges, the other two one each.
    float cornerX() { return config.zoneRight * width; }
    float cornerY() { return config.zoneTop * height; }
    float rightHandleX() { return cornerX(); }
    float rightHandleY() { return (cornerY() + height) * 0.5f; }
    float topHandleX() { return cornerX() * 0.5f; }
    float topHandleY() { return cornerY(); }

    /** Button rectangle {left, top, right, bottom}. */
    float[] buttonRect(Action action) {
        final float left = 0.52f * width, right = 0.96f * width, rowH = 0.13f * height;
        switch (action) {
            case SIZE_DOWN: return new float[] {left, 0.28f * height, left + 0.09f * width, 0.28f * height + rowH};
            case SIZE_UP:   return new float[] {right - 0.09f * width, 0.28f * height, right, 0.28f * height + rowH};
            case RESET:     return new float[] {left, 0.46f * height, right, 0.46f * height + rowH};
            case CANCEL:    return new float[] {left, 0.64f * height, left + 0.21f * width, 0.64f * height + rowH};
            case DONE:      return new float[] {right - 0.21f * width, 0.64f * height, right, 0.64f * height + rowH};
            default:        return new float[] {0f, 0f, 0f, 0f};
        }
    }

    /** The label area between - and + shows the current size. */
    float[] sizeLabelRect() {
        final float[] down = buttonRect(Action.SIZE_DOWN), up = buttonRect(Action.SIZE_UP);
        return new float[] {down[2], down[1], up[0], down[3]};
    }

    Action buttonAt(float x, float y) {
        final Action[] all = {Action.SIZE_DOWN, Action.SIZE_UP, Action.RESET, Action.CANCEL, Action.DONE};
        for (Action a : all) {
            final float[] r = buttonRect(a);
            if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]) return a;
        }
        return Action.NONE;
    }

    private boolean near(float x, float y, float cx, float cy) {
        final float dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= handleRadius * handleRadius;
    }

    /** A finger went down. The editor consumes every touch; true if it grabbed a button or a handle. */
    boolean down(float x, float y) {
        pressed = buttonAt(x, y);
        drag = Drag.NONE;
        if (pressed != Action.NONE) return true;
        // The corner wins where the handles overlap (narrow zones), then the right edge, then the top.
        if (near(x, y, cornerX(), cornerY())) { drag = Drag.CORNER; grabDx = x - cornerX(); grabDy = y - cornerY(); return true; }
        if (near(x, y, rightHandleX(), rightHandleY())) { drag = Drag.RIGHT; grabDx = x - rightHandleX(); grabDy = 0f; return true; }
        if (near(x, y, topHandleX(), topHandleY())) { drag = Drag.TOP; grabDx = 0f; grabDy = y - topHandleY(); return true; }
        return false;
    }

    void move(float x, float y) {
        if (drag == Drag.NONE || width <= 0f || height <= 0f) return;
        if (drag == Drag.CORNER || drag == Drag.RIGHT) config.zoneRight = (x - grabDx) / width;
        if (drag == Drag.CORNER || drag == Drag.TOP) config.zoneTop = (y - grabDy) / height;
        config.sanitize();
    }

    /** The finger lifted. A button acts only if the finger is still on it; returns the action (DONE / CANCEL are for the caller). */
    Action up(float x, float y) {
        Action result = Action.NONE;
        if (pressed != Action.NONE && buttonAt(x, y) == pressed) {
            result = pressed;
            switch (pressed) {
                case SIZE_DOWN: step(-DynamicStickConfig.SIZE_STEP); break;
                case SIZE_UP: step(DynamicStickConfig.SIZE_STEP); break;
                case RESET: config.reset(); break;
                default: break;
            }
        }
        drag = Drag.NONE;
        pressed = Action.NONE;
        return result;
    }

    void cancelTouch() {
        drag = Drag.NONE;
        pressed = Action.NONE;
    }

    private void step(float delta) {
        // Round to 5% so repeated presses stay on clean values.
        config.sizeScale = Math.round((config.sizeScale + delta) * 20f) / 20f;
        config.sanitize();
    }

    boolean isDragging() { return drag != Drag.NONE; }
    Action pressedButton() { return pressed; }
}
