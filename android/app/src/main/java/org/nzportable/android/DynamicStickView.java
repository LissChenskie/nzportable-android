// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;

/**
 * Transparent layer above the RetroTouch overlay that runs the floating left stick (see DynamicStick).
 *
 * It only accepts a touch that starts in the stick zone. Every other touch is refused, so it falls through to the
 * RetroTouch view below (the content layout splits touches between its children, see RetroTouchBridge) and buttons and
 * camera work exactly as before. The finger that started the stick stays with it until it is lifted.
 */
final class DynamicStickView extends View {
    interface Listener {
        /** x right, y down, -1..1 (the convention of the fixed RetroTouch stick). */
        void onStick(float x, float y);
    }

    private final DynamicStick stick = new DynamicStick();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private Listener listener;
    private boolean inputEnabled = true;
    private float sentX, sentY;

    DynamicStickView(Context context) {
        super(context);
        fill.setStyle(Paint.Style.FILL);
        ring.setStyle(Paint.Style.STROKE);
    }

    void setListener(Listener value) { listener = value; }

    /** False while the layout editor is open or the controls are hidden: touches are then not taken at all. */
    void setInputEnabled(boolean enabled) {
        if (!enabled) release();
        inputEnabled = enabled;
        invalidate();
    }

    /** Lets go of the stick (pause, menu, mode change) and reports neutral once if it was held. */
    void release() {
        final boolean wasActive = stick.isActive();
        stick.cancel();
        if (wasActive) report();
        invalidate();
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (!inputEnabled) return false;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int index = event.getActionIndex();
                if (!stick.begin(event.getPointerId(index), event.getX(index), event.getY(index), getWidth(), getHeight()))
                    return false;
                report();
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (!stick.isActive()) return false;
                final int index = event.findPointerIndex(stick.pointerId());
                if (index < 0) return false;
                stick.move(stick.pointerId(), event.getX(index), event.getY(index));
                report();
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                if (!stick.end(event.getPointerId(event.getActionIndex()))) return false;
                report();
                invalidate();
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

    @Override protected void onDraw(Canvas canvas) {
        if (!inputEnabled) return;
        final float width = getWidth(), height = getHeight();
        final float stroke = Math.max(2f, Math.min(width, height) * 0.004f);
        ring.setStrokeWidth(stroke);
        if (stick.isActive()) {
            final float radius = stick.radius();
            fill.setColor(0x33FFFFFF);
            canvas.drawCircle(stick.baseX(), stick.baseY(), radius, fill);
            ring.setColor(0x99FFFFFF);
            canvas.drawCircle(stick.baseX(), stick.baseY(), radius, ring);
            fill.setColor(0x88FFFFFF);
            canvas.drawCircle(stick.knobX(), stick.knobY(), radius * 0.42f, fill);
        } else if (width > 0f && height > 0f) {
            // Where a thumb can land: a faint ring where the fixed stick used to be.
            ring.setColor(0x38FFFFFF);
            canvas.drawCircle(DynamicStick.HINT_X * width, DynamicStick.HINT_Y * height, DynamicStick.radiusFor(width, height), ring);
        }
    }
}
