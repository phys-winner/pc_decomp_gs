package org.griefsyndrome.port;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import java.nio.charset.Charset;

public final class GdiBridge {
    private static Paint paint(int height, int weight, boolean italic) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setTypeface(Typeface.create(Typeface.DEFAULT,
            weight >= 600 ? Typeface.BOLD : Typeface.NORMAL));
        paint.setTextSize(Math.max(1, height));
        paint.setFakeBoldText(weight >= 600);
        paint.setTextSkewX(italic ? -0.25f : 0.0f);
        return paint;
    }

    private static String text(byte[] bytes, String charset) {
        return new String(bytes, Charset.forName(charset));
    }

    public static int[] metrics(int height, int weight, boolean italic) {
        Paint paint = paint(height, weight, italic);
        Paint.FontMetricsInt metrics = paint.getFontMetricsInt();
        int width = Math.max(1, Math.round(paint.measureText("0")));
        int maxWidth = Math.max(width, Math.round(paint.measureText("W")));
        return new int[]{metrics.bottom - metrics.top, -metrics.ascent, metrics.descent,
            Math.max(0, -metrics.top + metrics.ascent), Math.max(0, metrics.bottom - metrics.descent),
            width, maxWidth, weight, 0, height, height};
    }

    public static int[] measure(byte[] bytes, String charset, int height, int weight, boolean italic) {
        Paint paint = paint(height, weight, italic);
        String value = text(bytes, charset);
        Rect bounds = new Rect();
        paint.getTextBounds(value, 0, value.length(), bounds);
        Paint.FontMetricsInt metrics = paint.getFontMetricsInt();
        return new int[]{Math.max(0, bounds.width()), metrics.bottom - metrics.top,
            -metrics.ascent, metrics.descent};
    }

    public static int[] render(byte[] bytes, String charset, int height, int weight, boolean italic,
        int width, int bitmapHeight, int x, int y, int color, int background, int mode) {
        Bitmap bitmap = Bitmap.createBitmap(width, bitmapHeight, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        if (mode == 2) canvas.drawColor(background);
        Paint paint = paint(height, weight, italic);
        paint.setColor(color);
        canvas.drawText(text(bytes, charset), x, y - paint.ascent(), paint);
        int[] pixels = new int[width * bitmapHeight];
        bitmap.getPixels(pixels, 0, width, 0, 0, width, bitmapHeight);
        bitmap.recycle();
        return pixels;
    }

    public static int[] glyph(int codepoint, int height, int weight, boolean italic) {
        Paint paint = paint(height, weight, italic);
        String value = new String(Character.toChars(codepoint));
        Rect bounds = new Rect();
        paint.getTextBounds(value, 0, value.length(), bounds);
        int left = bounds.left;
        int top = bounds.top;
        int width = Math.max(1, bounds.width());
        int heightPixels = Math.max(1, bounds.height());
        Bitmap bitmap = Bitmap.createBitmap(width, heightPixels, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        paint.setColor(0xFFFFFFFF);
        canvas.drawText(value, -left, -top, paint);
        int[] rgba = new int[width * heightPixels];
        bitmap.getPixels(rgba, 0, width, 0, 0, width, heightPixels);
        bitmap.recycle();
        int[] result = new int[5 + width * heightPixels];
        result[0] = width;
        result[1] = heightPixels;
        result[2] = left;
        result[3] = top;
        result[4] = Math.round(paint.measureText(value));
        for (int i = 0; i < rgba.length; ++i) result[5 + i] = rgba[i] >>> 24;
        return result;
    }

    private static void menuText(Canvas canvas, String text, float x, float y, int size, int color) {
        Paint outline = paint(size, 700, false);
        outline.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        outline.setTextAlign(Paint.Align.CENTER);
        outline.setStyle(Paint.Style.STROKE);
        outline.setStrokeWidth(2.5f);
        outline.setColor(0xFF101018);
        canvas.drawText(text, x, y, outline);
        Paint inner = paint(size, 700, false);
        inner.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        inner.setTextAlign(Paint.Align.CENTER);
        inner.setColor(color);
        canvas.drawText(text, x, y, inner);
    }

    private static void menuRow(Canvas canvas, String text, float x, float y, boolean selected) {
        if (selected) {
            Paint highlight = paint(20, 400, false);
            highlight.setColor(0x806B467C);
            canvas.drawRect(80, y - 23, 720, y + 23, highlight);
        }
        menuText(canvas, text, x, y + 7, 19, 0xFFF2EAF7);
    }

    private static String keyName(int code, boolean pad) {
        if (code < 0) return "—";
        if (pad) {
            if (code == 524) return "A";
            if (code == 526) return "B";
            if (code == 525) return "X";
            if (code == 527) return "Y";
            if (code == 768) return "START";
            if (code >= 0 && code <= 3) return "AXIS " + code;
            return "PAD " + code;
        }
        int[] dik = {0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
            0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C};
        for (int i = 0; i < dik.length; ++i) if (dik[i] == code) return String.valueOf((char)('A' + i));
        if (code == 200) return "UP";
        if (code == 208) return "DOWN";
        if (code == 203) return "LEFT";
        if (code == 205) return "RIGHT";
        if (code == 28) return "ENTER";
        if (code == 57) return "SPACE";
        if (code == 1) return "ESC";
        if (code == 15) return "TAB";
        if (code >= 59 && code <= 68) return "F" + (code - 58);
        return String.format("DIK %02X", code);
    }

    public static int[] menu(int[] state) {
        Bitmap bitmap = Bitmap.createBitmap(800, 600, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        boolean jp = state[7] != 0;
        int mode = state[0], selected = state[1];
        if (mode == 0) {
            String[] labels = jp ? new String[]{"ゲームスタート", "LAN", "設定", "終了"} :
                new String[]{"Начать игру", "LAN", "Настройки", "Выход"};
            for (int row = 0; row < labels.length; ++row) {
                int y = 250 + row * 38;
                if (selected == row) {
                    Paint highlight = paint(20, 400, false);
                    highlight.setColor(0xCCB06C92);
                    canvas.drawRect(500, y - 11, 790, y + 11, highlight);
                }
                menuText(canvas, labels[row], 645, y + 6, 19, 0xFFFFF4FA);
            }
        } else {
            canvas.drawColor(0xFF17141F);
            menuText(canvas, jp ? "設定" : "Настройки", 400, 39, 28, 0xFFF0D9FF);
            String[] tabs = {jp ? "一般" : "Общие", jp ? "キーボード" : "Клавиатура", jp ? "パッド" : "Геймпад"};
            for (int i = 0; i < 3; ++i) {
                if ((i == 0 && mode == 1) || (i == 1 && (mode == 2 || (mode == 4 && state[6] < 27))) ||
                    (i == 2 && (mode == 3 || (mode == 4 && state[6] >= 27)))) {
                    Paint active = paint(16, 400, false);
                    active.setColor(0xFF684A78);
                    canvas.drawRect(20 + i * 260, 54, 270 + i * 260, 86, active);
                }
                menuText(canvas, tabs[i], 145 + i * 260, 77, 17, 0xFFE8DFF0);
            }
            if (mode == 1) {
                String[] labels = jp ? new String[]{"音量", "アンチエイリアス", "FPS limit", "タッチレイアウト", "アイコン", "デフォルト", "OK", "キャンセル"} :
                    new String[]{"Громкость", "Сглаживание", "FPS limit", "Сенсорная раскладка", "Значки кнопок", "По умолчанию", "ОК", "Отмена"};
                String[] antiAliasing = jp ? new String[]{"バイリニア", "FSR 1.0", "シャープピクセル", "バイキュービック"} :
                    new String[]{"Билинейное", "FSR 1.0", "Чёткие пиксели", "Бикубическое"};
                for (int i = 0; i < labels.length; ++i) {
                    float y = 137 + i * 50;
                    String value = "";
                    if (i == 0) value = Integer.toString(state[2]);
                    if (i == 1) value = antiAliasing[Math.max(0, Math.min(3, state[3]))];
                    if (i == 4) value = state[62] != 0 ? (jp ? "オン" : "Вкл.") : (jp ? "オフ" : "Выкл.");
                    if (i == 2) {
                        float fps = Float.intBitsToFloat(state[4]);
                        value = fps == 0.0f ? (jp ? "画面レート" : "Частота дисплея") : Long.toString(Math.round(fps));
                    }
                    menuRow(canvas, labels[i] + (value.isEmpty() ? "" : "  ·  " + value), 400, y, selected == i);
                    if (i == 0) {
                        Paint track = paint(10, 400, false);
                        track.setColor(0xFF57475F);
                        canvas.drawRect(360, y + 17, 700, y + 23, track);
                        track.setColor(0xFFD7A2D8);
                        canvas.drawRect(360, y + 17, 360 + 340 * state[2] / 100.0f, y + 23, track);
                    }
                }
            } else {
                for (int i = 0; i < 3; ++i) {
                    menuText(canvas, (jp ? "P" : "Игрок ") + (i + 1), 345 + i * 170, 108, 17,
                        state[5] == i ? 0xFFFFB9F1 : 0xFFAEA3B9);
                }
                String[] actions = jp ? new String[]{"上", "下", "左", "右", "弱攻撃", "強攻撃", "ジャンプ", "魔法", "ポーズ"} :
                    new String[]{"Вверх", "Вниз", "Влево", "Вправо", "Быстрая атака", "Сильная атака", "Прыжок", "Магия", "Пауза"};
                boolean pad = mode == 3 || (mode == 4 && state[6] >= 27);
                for (int i = 0; i < 9; ++i) {
                    int index = (pad ? 27 : 0) + state[5] * 9 + i;
                    String value = keyName(state[8 + index], pad);
                    menuRow(canvas, actions[i] + "  ·  " + value, 400, 149 + i * 50, mode != 4 && selected == i);
                }
                if (mode == 4) {
                    Paint veil = paint(20, 400, false);
                    veil.setColor(0xE0100D16);
                    canvas.drawRect(70, 230, 730, 365, veil);
                    menuText(canvas, jp ? "入力を待っています" : "Нажмите клавишу или кнопку", 400, 280, 23, 0xFFFFE6FA);
                    menuText(canvas, jp ? "X — クリア / ESC — 戻る" : "X — очистить, Esc — отмена", 400, 324, 17, 0xFFC7B8D0);
                }
            }
            menuText(canvas, jp ? "Z — 選択 / X — 戻る / Tab — デバイス / 1–3 — プレイヤー" :
                "Z — выбор, X — назад, Tab — устройство, 1–3 — игрок", 400, 580, 14, 0xFFAFA0BA);
        }
        int[] pixels = new int[800 * 600];
        bitmap.getPixels(pixels, 0, 800, 0, 0, 800, 600);
        bitmap.recycle();
        return pixels;
    }
}
