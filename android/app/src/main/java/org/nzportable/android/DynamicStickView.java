// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

import android.content.Context;
import android.view.MotionEvent;
import android.view.View;

/**
 * Touch surface of the floating left stick (see DynamicStick). It is only as large as the stick zone, placed at the
 * bottom left of the content layout by RetroTouchBridge (its size follows the player's zone setting), and it draws nothing (DynamicStickHud does).
 *
 * Why it must not cover the screen: when a view already receives a touch and another finger lands inside its bounds,
 * Android gives that finger to the same view without asking the views below. A full-screen stick layer swallowed the
 * camera finger as soon as the stick was held. With a zone-sized view a finger outside the zone can only reach the
 * RetroTouch view below, whatever the other finger is doing.
 */
final class DynamicStickView extends View {
    interface Listener {
        /** x right, y down, -1..1 (the convention of the fixed RetroTouch stick). */
        void onStick(float x, float y);
    }

    private final DynamicStick stick;
    private final DynamicStickHud hud;
    private Listener listener;
    private boolean inputEnabled = true;
    private float sentX, sentY;

    DynamicStickView(Context context, DynamicStick stick, DynamicStickHud hud) {
        super(context);
        this.stick = stick;
        this.hud = hud;
    }

    void setListener(Listener value) { listener = value; }

    /** False while the layout editor is open or the controls are hidden: touches are then not taken at all. */
    void setInputEnabled(boolean enabled) {
        if (!enabled) release();
        inputEnabled = enabled;
        hud.setShown(enabled);
    }

    /** Lets go of the stick (pause, menu, mode change) and reports neutral once if it was held. */
    void release() {
        final boolean wasActive = stick.isActive();
        stick.cancel();
        if (wasActive) report();
        hud.invalidate();
    }

    @Override protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        // Only the zone: the left part of the screen, below the top strip.
        final DynamicStickConfig config = stick.config();
        setMeasuredDimension(
                config.zoneWidthPx(MeasureSpec.getSize(widthMeasureSpec)),
                config.zoneHeightPx(MeasureSpec.getSize(heightMeasureSpec)));
    }

    // The stick works in coordinates of the whole content layout (the hud's), not of this small view.
    private float screenX(MotionEvent event, int index) { return event.getX(index) + getLeft(); }
    private float screenY(MotionEvent event, int index) { return event.getY(index) + getTop(); }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (!inputEnabled) return false;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int index = event.getActionIndex();
                if (!stick.begin(event.getPointerId(index), screenX(event, index), screenY(event, index),
                        hud.getWidth(), hud.getHeight()))
                    return false;
                report();
                hud.invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (!stick.isActive()) return false;
                final int index = event.findPointerIndex(stick.pointerId());
                if (index < 0) return false;
                stick.move(stick.pointerId(), screenX(event, index), screenY(event, index));
                report();
                hud.invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                if (!stick.end(event.getPointerId(event.getActionIndex()))) return false;
                report();
                hud.invalidate();
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                if (!stick.isActive()) return false;
                release();
                return true;
            }
            default:
                return false;
        }
    }

    // Only changes are sent: a held stick would otherwise post a native event for every touch sample.
    private void report() {
        final float x = stick.x(), y = stick.y();
        if (x == sentX && y == sentY) return;
        sentX = x;
        sentY = y;
        if (listener != null) listener.onStick(x, y);
    }
}
