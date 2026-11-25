// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/**
 * Full-screen editor for the floating stick (logic: DynamicStickEditor). It shows the stick zone as a highlighted rectangle at
 * the bottom left with three handles (right edge, top edge, corner) and a preview of the stick at the size it will have. Drag a
 * handle to resize the zone; - / + change the stick size; RESET restores the defaults; CANCEL discards, DONE keeps.
 * It consumes every touch while it is open.
 */
final class DynamicStickEditorView extends View {
    interface Listener {
        void onFinished(boolean accepted, DynamicStickConfig edited);
    }

    private final DynamicStickEditor editor;
    private final Listener listener;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final float density;
    private boolean finished;
    private int pointer = -1;

    DynamicStickEditorView(Context context, DynamicStickConfig start, Listener listener) {
        super(context);
        this.editor = new DynamicStickEditor(start);
        this.listener = listener;
        density = context.getResources().getDisplayMetrics().density;
        fill.setStyle(Paint.Style.FILL);
        line.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setColor(0xFFFFFFFF);
        setClickable(true);
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        editor.setScreen(w, h, 30f * density);
    }

    /** Back key: leave without saving. */
    void cancel() { finish(false); }

    private void finish(boolean accepted) {
        if (finished) return;
        finished = true;
        listener.onFinished(accepted, editor.config);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pointer = event.getPointerId(0);
                editor.down(event.getX(), event.getY());
                invalidate();
                break;
            case MotionEvent.ACTION_MOVE: {
                final int index = event.findPointerIndex(pointer);
                if (index >= 0) editor.move(event.getX(index), event.getY(index));
                invalidate();
                break;
            }
            case MotionEvent.ACTION_UP: {
                final int index = event.findPointerIndex(pointer);
                final DynamicStickEditor.Action action = editor.up(index >= 0 ? event.getX(index) : event.getX(), index >= 0 ? event.getY(index) : event.getY());
                pointer = -1;
                invalidate();
                if (action == DynamicStickEditor.Action.DONE) finish(true);
                else if (action == DynamicStickEditor.Action.CANCEL) finish(false);
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                editor.cancelTouch();
                pointer = -1;
                invalidate();
                break;
            default:
                break; // extra fingers are ignored
        }
        return true; // everything is ours while the editor is open
    }

    private void button(Canvas canvas, DynamicStickEditor.Action action, String label) {
        final float[] r = editor.buttonRect(action);
        rect.set(r[0], r[1], r[2], r[3]);
        fill.setColor(editor.pressedButton() == action ? 0xCC5A8CFF : 0xAA202838);
        canvas.drawRoundRect(rect, 12f * density, 12f * density, fill);
        line.setColor(0xFFFFFFFF);
        canvas.drawRoundRect(rect, 12f * density, 12f * density, line);
        text.setTextSize(Math.min(rect.height() * 0.4f, 22f * density));
        canvas.drawText(label, rect.centerX(), rect.centerY() + text.getTextSize() * 0.35f, text);
    }

    private void handle(Canvas canvas, float x, float y, boolean emphasised) {
        fill.setColor(emphasised ? 0xFFFFD060 : 0xFFFFFFFF);
        canvas.drawCircle(x, y, editor.handleRadius() * 0.5f, fill);
        line.setColor(0xFF202838);
        canvas.drawCircle(x, y, editor.handleRadius() * 0.5f, line);
    }

    @Override protected void onDraw(Canvas canvas) {
        final float w = getWidth(), h = getHeight();
        if (w <= 0f || h <= 0f) return;
        final DynamicStickConfig config = editor.config;
        line.setStrokeWidth(Math.max(2f, 2f * density));

        fill.setColor(0xB0000000);                     // dim everything behind
        canvas.drawRect(0f, 0f, w, h, fill);

        rect.set(0f, config.zoneTop * h, config.zoneRight * w, h);   // the zone
        fill.setColor(0x4440A0FF);
        canvas.drawRect(rect, fill);
        line.setColor(0xFF70B0FF);
        canvas.drawRect(rect, line);

        // The stick as it will appear, at the size chosen.
        final float radius = config.radius(w, h);
        fill.setColor(0x33FFFFFF);
        canvas.drawCircle(config.hintX(w), config.hintY(h), radius, fill);
        line.setColor(0xCCFFFFFF);
        canvas.drawCircle(config.hintX(w), config.hintY(h), radius, line);
        fill.setColor(0x88FFFFFF);
        canvas.drawCircle(config.hintX(w), config.hintY(h), radius * 0.42f, fill);

        handle(canvas, editor.cornerX(), editor.cornerY(), editor.isDragging());
        handle(canvas, editor.rightHandleX(), editor.rightHandleY(), false);
        handle(canvas, editor.topHandleX(), editor.topHandleY(), false);

        text.setTextSize(Math.min(h * 0.055f, 26f * density));
        canvas.drawText("STICK ZONE: drag the handles", w * 0.5f, h * 0.10f, text);
        text.setTextSize(Math.min(h * 0.04f, 18f * density));
        canvas.drawText("width " + Math.round(config.zoneRight * 100f) + "%   height " + Math.round((1f - config.zoneTop) * 100f) + "%",
                w * 0.5f, h * 0.17f, text);

        button(canvas, DynamicStickEditor.Action.SIZE_DOWN, "-");
        button(canvas, DynamicStickEditor.Action.SIZE_UP, "+");
        final float[] label = editor.sizeLabelRect();
        text.setTextSize(Math.min((label[3] - label[1]) * 0.4f, 22f * density));
        canvas.drawText("SIZE " + Math.round(config.sizeScale * 100f) + "%", (label[0] + label[2]) * 0.5f,
                (label[1] + label[3]) * 0.5f + text.getTextSize() * 0.35f, text);
        button(canvas, DynamicStickEditor.Action.RESET, "RESET");
        button(canvas, DynamicStickEditor.Action.CANCEL, "CANCEL");
        button(canvas, DynamicStickEditor.Action.DONE, "DONE");
    }
}
