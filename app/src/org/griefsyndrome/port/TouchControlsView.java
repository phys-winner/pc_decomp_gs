package org.griefsyndrome.port;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.util.SparseIntArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import java.util.HashSet;
import java.util.Set;

final class TouchControlsView extends View {
    private static final int[] ACTIONS = {0, 1, 2, 3, 4, 5, 6, 7};
    private static final String[] KEYS = {"up", "down", "left", "right", "fast", "strong", "jump", "magic"};
    private static final String[] NAMES_RU = {"Вверх", "Вниз", "Влево", "Вправо", "Быстрая", "Сильная", "Прыжок", "Магия"};
    private static final String[] NAMES_EN = {"Up", "Down", "Left", "Right", "Fast", "Strong", "Jump", "Magic"};

    interface Listener { void editorClosed(); }

    private final PortSettings preferences;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SparseIntArray owners = new SparseIntArray();
    private final SparseIntArray ownerKeys = new SparseIntArray();
    private final Set<Integer> pressed = new HashSet<>();
    private final float[] positionsX = new float[ACTIONS.length];
    private final float[] positionsY = new float[ACTIONS.length];
    private final float[] sizes = new float[ACTIONS.length];
    private final float[] savedX = new float[ACTIONS.length];
    private final float[] savedY = new float[ACTIONS.length];
    private final float[] savedSizes = new float[ACTIONS.length];
    private final RectF resetRect = new RectF();
    private final RectF cancelRect = new RectF();
    private final RectF doneRect = new RectF();
    private final RectF minusRect = new RectF();
    private final RectF plusRect = new RectF();
    private final RectF alphaRect = new RectF();
    private final RectF mirrorRect = new RectF();
    private final RectF marginRect = new RectF();
    private final RectF labelsRect = new RectF();
    private float density;
    private int safeLeft;
    private int safeTop;
    private int safeRight;
    private int safeBottom;
    private float scale = 1.0f;
    private float opacity = 0.65f;
    private float margin = 24.0f;
    private float savedOpacity;
    private float savedMargin;
    private int editOwner = -1;
    private int editSecond = -1;
    private int editAction = -1;
    private int editSlider;
    private float editOffsetX;
    private float editOffsetY;
    private float pinchDistance;
    private float pinchSize;
    private boolean editing;
    private boolean leftHanded;
    private boolean savedLeftHanded;
    private boolean labels = true;
    private boolean savedLabels;
    private boolean russian = true;
    private Listener listener;

    TouchControlsView(Context context, PortSettings settings) {
        super(context);
        preferences = settings;
        density = getResources().getDisplayMetrics().density;
        scale = settings.controlScale();
        opacity = settings.controlAlpha();
        margin = settings.controlMargin();
        leftHanded = settings.leftHanded();
        labels = settings.controlLabels();
        setFocusable(false);
        setContentDescription("Touch controls");
    }

    void setListener(Listener value) { listener = value; }

    void setSafeInsets(int left, int top, int right, int bottom) {
        if (safeLeft == left && safeTop == top && safeRight == right && safeBottom == bottom) return;
        safeLeft = left;
        safeTop = top;
        safeRight = right;
        safeBottom = bottom;
        releaseAll();
        if (getWidth() > 0 && getHeight() > 0 && !editing) loadPositions();
        invalidate();
    }

    void cancelTouch() {
        releaseAll();
    }

    void setRussian(boolean value) {
        russian = value;
        invalidate();
    }

    void setControlScale(float value) {
        scale = value;
        invalidate();
    }

    void setControlAlpha(float value) {
        opacity = value;
        invalidate();
    }

    void setLeftHanded(boolean value) {
        leftHanded = value;
        resetLayout();
    }

    void setLabelsVisible(boolean value) {
        labels = value;
        invalidate();
    }

    private float dp(float value) { return value * density * scale; }
    private float editorDp(float value) { return value * density; }
    private float radius(int action) { return dp(28) * sizes[action]; }

    private float defaultX(int action) {
        float width = getWidth() > 0 ? getWidth() : getResources().getDisplayMetrics().widthPixels;
        float r = dp(28);
        float pad = editorDp(margin);
        float left = safeLeft + pad + 3 * r;
        if (action == 2) return left - 1.85f * r;
        if (action == 3) return left + 1.85f * r;
        if (action == 0 || action == 1) return left;
        float right = width - safeRight - pad - 3 * r;
        if (action == 4) return right - 1.65f * r;
        if (action == 5) return right + 1.65f * r;
        return right;
    }

    private float defaultY(int action) {
        float height = getHeight() > 0 ? getHeight() : getResources().getDisplayMetrics().heightPixels;
        float r = dp(28);
        float center = height - safeBottom - editorDp(margin) - 3 * r;
        if (action == 0) return center - 1.7f * r;
        if (action == 1) return center + 1.7f * r;
        if (action == 2 || action == 3) return center;
        if (action == 6) return center - 1.7f * r;
        if (action == 7) return center + 1.7f * r;
        return center;
    }

    private void loadPositions() {
        for (int action : ACTIONS) {
            float x = defaultX(action);
            float y = defaultY(action);
            if (leftHanded) x = getWidth() - x;
            positionsX[action] = preferences.controlX(KEYS[action], x / getWidth());
            positionsY[action] = preferences.controlY(KEYS[action], y / getHeight());
            sizes[action] = preferences.controlSize(KEYS[action]);
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        releaseAll();
        if (w > 0 && h > 0 && !editing) loadPositions();
    }

    private float centerX(int action) {
        float minimum = safeLeft + radius(action);
        float maximum = getWidth() - safeRight - radius(action);
        if (minimum > maximum) return (safeLeft + getWidth() - safeRight) * 0.5f;
        return Math.max(minimum, Math.min(maximum, positionsX[action] * getWidth()));
    }

    private float centerY(int action) {
        float minimum = safeTop + radius(action);
        float maximum = getHeight() - safeBottom - radius(action);
        if (minimum > maximum) return (safeTop + getHeight() - safeBottom) * 0.5f;
        return Math.max(minimum, Math.min(maximum, positionsY[action] * getHeight()));
    }

    private int actionAt(float x, float y) {
        for (int i = ACTIONS.length - 1; i >= 0; --i) {
            int action = ACTIONS[i];
            float dx = x - centerX(action);
            float dy = y - centerY(action);
            float radius = radius(action) + dp(7);
            if (dx * dx + dy * dy <= radius * radius) return action;
        }
        return -1;
    }

    boolean handlesTouch(float x, float y) {
        return owners.size() > 0 || actionAt(x, y) >= 0;
    }

    private boolean keyOwned(int key) {
        for (int i = 0; i < ownerKeys.size(); ++i) if (ownerKeys.valueAt(i) == key) return true;
        return false;
    }

    private void releaseAll() {
        Set<Integer> keys = new HashSet<>();
        for (int i = 0; i < ownerKeys.size(); ++i) keys.add(ownerKeys.valueAt(i));
        for (Integer key : keys) GameActivity.touchKey(key, false);
        owners.clear();
        ownerKeys.clear();
        pressed.clear();
        editOwner = editSecond = -1;
        editAction = -1;
        editSlider = 0;
        invalidate();
    }

    private void snapshot() {
        System.arraycopy(positionsX, 0, savedX, 0, ACTIONS.length);
        System.arraycopy(positionsY, 0, savedY, 0, ACTIONS.length);
        System.arraycopy(sizes, 0, savedSizes, 0, ACTIONS.length);
        savedOpacity = opacity;
        savedMargin = margin;
        savedLeftHanded = leftHanded;
        savedLabels = labels;
    }

    void setEditing(boolean value) {
        if (editing == value) return;
        if (value) {
            releaseAll();
            snapshot();
            editing = true;
        } else {
            System.arraycopy(savedX, 0, positionsX, 0, ACTIONS.length);
            System.arraycopy(savedY, 0, positionsY, 0, ACTIONS.length);
            System.arraycopy(savedSizes, 0, sizes, 0, ACTIONS.length);
            opacity = savedOpacity;
            margin = savedMargin;
            leftHanded = savedLeftHanded;
            labels = savedLabels;
            releaseAll();
            editing = false;
            if (listener != null) listener.editorClosed();
        }
        invalidate();
    }

    private void finishEditing() {
        for (int action : ACTIONS) {
            preferences.controlPosition(KEYS[action], positionsX[action], positionsY[action]);
            preferences.controlSize(KEYS[action], sizes[action]);
        }
        preferences.controlAlpha(opacity);
        preferences.controlMargin(margin);
        preferences.leftHanded(leftHanded);
        preferences.controlLabels(labels);
        GameActivity.onPortSetting(16, labels ? 1.0f : 0.0f);
        releaseAll();
        editing = false;
        if (listener != null) listener.editorClosed();
        invalidate();
    }

    boolean isEditing() { return editing; }

    private void resetLayout() {
        for (int action : ACTIONS) {
            float x = defaultX(action);
            float y = defaultY(action);
            if (leftHanded) x = getWidth() - x;
            positionsX[action] = x / getWidth();
            positionsY[action] = y / getHeight();
            sizes[action] = 1.0f;
        }
        invalidate();
    }

    private void layoutEditorRects() {
        float left = editorDp(12);
        float top = editorDp(10);
        resetRect.set(left, top, left + editorDp(82), top + editorDp(36));
        cancelRect.set(getWidth() / 2f - editorDp(48), top, getWidth() / 2f + editorDp(48), top + editorDp(36));
        doneRect.set(getWidth() - left - editorDp(82), top, getWidth() - left, top + editorDp(36));
        float row = top + editorDp(48);
        mirrorRect.set(left, row, left + editorDp(82), row + editorDp(36));
        marginRect.set(left + editorDp(90), row, left + editorDp(172), row + editorDp(36));
        labelsRect.set(left + editorDp(180), row, left + editorDp(262), row + editorDp(36));
        minusRect.set(getWidth() - left - editorDp(164), row, getWidth() - left - editorDp(124), row + editorDp(36));
        plusRect.set(getWidth() - left - editorDp(116), row, getWidth() - left - editorDp(76), row + editorDp(36));
        alphaRect.set(left, row + editorDp(50), getWidth() - left - editorDp(72), row + editorDp(72));
    }

    private void updateAlpha(float x) {
        float value = (x - alphaRect.left) / alphaRect.width();
        setControlAlpha(Math.max(0.15f, Math.min(0.9f, value)));
    }

    private float pointerDistance(MotionEvent event, int first, int second) {
        int a = event.findPointerIndex(first);
        int b = event.findPointerIndex(second);
        if (a < 0 || b < 0) return pinchDistance;
        float dx = event.getX(a) - event.getX(b);
        float dy = event.getY(a) - event.getY(b);
        return (float) Math.hypot(dx, dy);
    }

    private boolean editorTouch(MotionEvent event) {
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        int id = event.getPointerId(index);
        float x = event.getX(index);
        float y = event.getY(index);
        layoutEditorRects();
        if (action == MotionEvent.ACTION_DOWN) {
            if (doneRect.contains(x, y)) { finishEditing(); return true; }
            if (cancelRect.contains(x, y)) { setEditing(false); return true; }
            if (resetRect.contains(x, y)) { resetLayout(); return true; }
            if (mirrorRect.contains(x, y)) {
                leftHanded = !leftHanded;
                resetLayout();
                return true;
            }
            if (marginRect.contains(x, y)) {
                margin = margin >= 48.0f ? 12.0f : margin + 12.0f;
                resetLayout();
                return true;
            }
            if (labelsRect.contains(x, y)) {
                labels = !labels;
                invalidate();
                return true;
            }
            if (minusRect.contains(x, y) || plusRect.contains(x, y)) {
                if (editAction >= 0) {
                    float delta = minusRect.contains(x, y) ? -0.1f : 0.1f;
                    sizes[editAction] = Math.max(0.5f, Math.min(2.5f, sizes[editAction] + delta));
                    invalidate();
                }
                return true;
            }
            if (alphaRect.contains(x, y)) {
                editOwner = id;
                editSlider = 1;
                editAction = -1;
                updateAlpha(x);
                return true;
            }
            int hit = actionAt(x, y);
            if (hit >= 0) {
                editOwner = id;
                editSecond = -1;
                editAction = hit;
                editOffsetX = x - centerX(hit);
                editOffsetY = y - centerY(hit);
                invalidate();
            }
            return true;
        }
        if (action == MotionEvent.ACTION_POINTER_DOWN && editOwner >= 0 && editSecond < 0 && editAction >= 0) {
            editSecond = id;
            pinchDistance = Math.max(editorDp(20), pointerDistance(event, editOwner, editSecond));
            pinchSize = sizes[editAction];
            return true;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            if (editOwner >= 0 && event.findPointerIndex(editOwner) >= 0) {
                if (editSlider != 0) {
                    int at = event.findPointerIndex(editOwner);
                    updateAlpha(event.getX(at));
                } else if (editAction >= 0) {
                    if (editSecond >= 0 && event.findPointerIndex(editSecond) >= 0) {
                        float distance = pointerDistance(event, editOwner, editSecond);
                        sizes[editAction] = Math.max(0.5f, Math.min(2.5f, pinchSize * distance / pinchDistance));
                    } else {
                        int at = event.findPointerIndex(editOwner);
                        float mx = event.getX(at) - editOffsetX;
                        float my = event.getY(at) - editOffsetY;
                        float radius = radius(editAction);
                        positionsX[editAction] = Math.max(radius / getWidth(), Math.min(1.0f - radius / getWidth(), mx / getWidth()));
                        positionsY[editAction] = Math.max(radius / getHeight(), Math.min(1.0f - radius / getHeight(), my / getHeight()));
                    }
                }
                invalidate();
            }
            return true;
        }
        if (action == MotionEvent.ACTION_POINTER_UP) {
            if (id == editSecond) {
                editSecond = -1;
            } else if (id == editOwner) {
                editOwner = editSecond;
                editSecond = -1;
                if (editOwner >= 0 && editAction >= 0) {
                    int at = event.findPointerIndex(editOwner);
                    if (at >= 0) {
                        editOffsetX = event.getX(at) - centerX(editAction);
                        editOffsetY = event.getY(at) - centerY(editAction);
                    }
                }
            }
            return true;
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            if (id == editOwner || action == MotionEvent.ACTION_CANCEL) {
                editOwner = editSecond = -1;
                editAction = -1;
                editSlider = 0;
                invalidate();
            }
            return true;
        }
        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (editing) return editorTouch(event);
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) { releaseAll(); return true; }
        int index = event.getActionIndex();
        int id = event.getPointerId(index);
        float x = event.getX(index);
        float y = event.getY(index);
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int item = actionAt(x, y);
            if (item < 0) return false;
            int key = GameActivity.nativeTouchBinding(item);
            if (!keyOwned(key)) GameActivity.touchKey(key, true);
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            owners.put(id, item);
            ownerKeys.put(id, key);
            pressed.add(item);
            invalidate();
            return true;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); ++i) {
                int movingId = event.getPointerId(i);
                if (owners.indexOfKey(movingId) < 0 || owners.get(movingId) >= 4) continue;
                int next = actionAt(event.getX(i), event.getY(i));
                if (next >= 4) next = -1;
                if (next == owners.get(movingId)) continue;
                int oldKey = ownerKeys.get(movingId);
                owners.delete(movingId);
                ownerKeys.delete(movingId);
                if (oldKey != 0 && !keyOwned(oldKey)) GameActivity.touchKey(oldKey, false);
                int nextKey = next >= 0 ? GameActivity.nativeTouchBinding(next) : 0;
                if (nextKey != 0 && !keyOwned(nextKey)) GameActivity.touchKey(nextKey, true);
                owners.put(movingId, next);
                ownerKeys.put(movingId, nextKey);
            }
            pressed.clear();
            for (int i = 0; i < owners.size(); ++i) if (owners.valueAt(i) >= 0) pressed.add(owners.valueAt(i));
            invalidate();
            return owners.size() > 0;
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            int item = owners.get(id, -1);
            if (owners.indexOfKey(id) >= 0) {
                int key = ownerKeys.get(id);
                owners.delete(id);
                ownerKeys.delete(id);
                if (key != 0 && !keyOwned(key)) GameActivity.touchKey(key, false);
                boolean stillPressed = false;
                for (int i = 0; i < owners.size(); ++i) if (owners.valueAt(i) == item) stillPressed = true;
                if (!stillPressed) pressed.remove(item);
                invalidate();
                return true;
            }
        }
        return owners.size() > 0;
    }

    @Override
    public void setVisibility(int visibility) {
        if (visibility != VISIBLE) releaseAll();
        super.setVisibility(visibility);
    }

    @Override
    protected void onDetachedFromWindow() {
        releaseAll();
        super.onDetachedFromWindow();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        for (int action : ACTIONS) {
            float x = centerX(action);
            float y = centerY(action);
            boolean held = pressed.contains(action);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(held ? 0xFF8B6DB1 : 0xFF20242C);
            paint.setAlpha(Math.round((held ? 145 : 82) * opacity));
            canvas.drawCircle(x, y, radius(action), paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(1.5f));
            paint.setColor(0xFFE6EDF7);
            paint.setAlpha(Math.round(130 * opacity));
            canvas.drawCircle(x, y, radius(action) - dp(1), paint);
            if (labels || editing) drawIcon(canvas, action, x, y, radius(action));
            if (editing && editAction == action) {
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(editorDp(2));
                paint.setColor(0xFFE8B7FF);
                paint.setAlpha(255);
                canvas.drawCircle(x, y, radius(action) + editorDp(5), paint);
            }
        }
        if (!editing) return;
        layoutEditorRects();
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xE010121A);
        canvas.drawRect(0, 0, getWidth(), editorDp(156), paint);
        paint.setColor(0xFFFFFFFF);
        paint.setAlpha(255);
        paint.setTextAlign(Paint.Align.LEFT);
        paint.setTextSize(editorDp(12));
        String hint = russian ? "Перетаскивайте; размер — двумя пальцами" : "Drag; pinch to resize";
        canvas.drawText(hint, editorDp(12), editorDp(151), paint);
        if (editAction >= 0) {
            String name = (russian ? NAMES_RU : NAMES_EN)[editAction];
            canvas.drawText(name + "  " + Math.round(sizes[editAction] * 100) + "%", editorDp(280), editorDp(126), paint);
        }
        drawEditorButton(canvas, resetRect, russian ? "СБРОС" : "RESET");
        drawEditorButton(canvas, cancelRect, russian ? "ОТМЕНА" : "CANCEL");
        drawEditorButton(canvas, doneRect, russian ? "ГОТОВО" : "DONE");
        drawEditorButton(canvas, mirrorRect, leftHanded ? "LEFT" : "RIGHT");
        drawEditorButton(canvas, marginRect, "PAD " + Math.round(margin));
        drawEditorButton(canvas, labelsRect, labels ? "ICONS" : "NO ICON");
        drawEditorButton(canvas, minusRect, "−");
        drawEditorButton(canvas, plusRect, "+");
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xFF596273);
        canvas.drawRoundRect(alphaRect, editorDp(8), editorDp(8), paint);
        float knob = alphaRect.left + alphaRect.width() * ((opacity - 0.15f) / 0.75f);
        paint.setColor(0xFFD7B7EF);
        canvas.drawCircle(knob, alphaRect.centerY(), editorDp(11), paint);
        paint.setColor(0xFFFFFFFF);
        paint.setTextAlign(Paint.Align.RIGHT);
        paint.setTextSize(editorDp(13));
        canvas.drawText(Math.round(opacity * 100) + "%", getWidth() - editorDp(12), alphaRect.centerY() + editorDp(5), paint);
    }

    private void drawEditorButton(Canvas canvas, RectF rect, String text) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xFF343B49);
        paint.setAlpha(255);
        canvas.drawRoundRect(rect, editorDp(7), editorDp(7), paint);
        paint.setColor(0xFFFFFFFF);
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(editorDp(11));
        canvas.drawText(text, rect.centerX(), rect.centerY() + editorDp(4), paint);
    }

    private void drawIcon(Canvas canvas, int action, float x, float y, float radius) {
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(2.2f));
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStrokeJoin(Paint.Join.ROUND);
        paint.setColor(0xFFFFFFFF);
        paint.setAlpha(Math.round(230 * opacity));
        float r = radius * 0.58f;
        if (action <= 3) {
            float degrees = action == 0 ? 0 : action == 1 ? 180 : action == 2 ? -90 : 90;
            canvas.save();
            canvas.rotate(degrees, x, y);
            Path path = new Path();
            path.moveTo(x - r * 0.75f, y + r * 0.1f);
            path.lineTo(x, y - r * 0.65f);
            path.lineTo(x + r * 0.75f, y + r * 0.1f);
            canvas.drawPath(path, paint);
            canvas.drawLine(x, y - r * 0.6f, x, y + r * 0.65f, paint);
            canvas.restore();
            return;
        }
        if (action == 4) {
            canvas.drawLine(x - r * 0.55f, y + r * 0.55f, x + r * 0.55f, y - r * 0.55f, paint);
            canvas.drawLine(x - r * 0.45f, y + r * 0.1f, x - r * 0.1f, y + r * 0.45f, paint);
            canvas.drawLine(x - r * 0.6f, y + r * 0.65f, x - r * 0.35f, y + r * 0.4f, paint);
            return;
        }
        if (action == 5) {
            canvas.drawLine(x - r * 0.45f, y + r * 0.6f, x + r * 0.35f, y - r * 0.2f, paint);
            Path head = new Path();
            head.moveTo(x - r * 0.1f, y - r * 0.55f);
            head.lineTo(x + r * 0.6f, y - r * 0.1f);
            head.lineTo(x + r * 0.35f, y + r * 0.15f);
            head.lineTo(x - r * 0.35f, y - r * 0.3f);
            head.close();
            canvas.drawPath(head, paint);
            return;
        }
        if (action == 6) {
            Path arrow = new Path();
            arrow.moveTo(x - r * 0.65f, y);
            arrow.lineTo(x, y - r * 0.65f);
            arrow.lineTo(x + r * 0.65f, y);
            canvas.drawPath(arrow, paint);
            canvas.drawLine(x, y - r * 0.6f, x, y + r * 0.3f, paint);
            canvas.drawLine(x - r * 0.7f, y + r * 0.65f, x + r * 0.7f, y + r * 0.65f, paint);
            return;
        }
        Path star = new Path();
        for (int i = 0; i < 8; ++i) {
            double angle = -Math.PI / 2 + i * Math.PI / 4;
            float length = (i & 1) == 0 ? r * 0.75f : r * 0.28f;
            float px = x + (float) Math.cos(angle) * length;
            float py = y + (float) Math.sin(angle) * length;
            if (i == 0) star.moveTo(px, py); else star.lineTo(px, py);
        }
        star.close();
        canvas.drawPath(star, paint);
    }
}
