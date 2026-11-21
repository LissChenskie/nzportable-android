// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;

/**
 * Draws the floating stick over the whole screen. It never takes a touch (see DynamicStickView for why the touch surface
 * and the drawing are separate views): the knob can leave the zone, and the drawing must not be clipped to it.
 */
final class DynamicStickHud extends View {
    private final DynamicStick stick;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean shown;

    DynamicStickHud(Context context, DynamicStick stick) {
        super(context);
        this.stick = stick;
        fill.setStyle(Paint.Style.FILL);
        ring.setStyle(Paint.Style.STROKE);
    }

    void setShown(boolean value) {
        shown = value;
        invalidate();
    }

    @Override public boolean onTouchEvent(MotionEvent event) { return false; }

    @Override protected void onDraw(Canvas canvas) {
        if (!shown) return;
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
