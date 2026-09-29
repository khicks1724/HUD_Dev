package com.huddev.atak.prism;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.Locale;

/** Custom-drawn pieces of the PRISM panel. */
final class PrismViews {
    private PrismViews() {
    }

    /** The PRISM mark: white beam in, spectrum out. */
    static final class Mark extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();

        Mark(Context c) {
            super(c);
        }

        @Override
        protected void onDraw(Canvas c) {
            float w = getWidth(), h = getHeight(), s = Math.min(w, h) / 48f;
            c.save();
            c.translate((w - 48 * s) / 2, (h - 48 * s) / 2);
            c.scale(s, s);
            // spectrum fan
            float top = 14, band = 3.5f;
            for (int i = 0; i < PrismTheme.SPECTRUM.length; i++) {
                path.reset();
                path.moveTo(28, 22);
                path.lineTo(46, top + i * band);
                path.lineTo(46, top + (i + 1) * band);
                path.close();
                p.setStyle(Paint.Style.FILL);
                p.setColor(PrismTheme.SPECTRUM[i]);
                c.drawPath(path, p);
            }
            // incoming beam
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeCap(Paint.Cap.ROUND);
            p.setStrokeWidth(2.2f);
            p.setColor(Color.WHITE);
            c.drawLine(3, 27, 17, 24, p);
            // prism
            path.reset();
            path.moveTo(24, 8);
            path.lineTo(37, 34);
            path.lineTo(11, 34);
            path.close();
            p.setStyle(Paint.Style.FILL);
            p.setColor(Color.rgb(11, 12, 14));
            c.drawPath(path, p);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeJoin(Paint.Join.ROUND);
            p.setStrokeWidth(2f);
            p.setColor(Color.WHITE);
            c.drawPath(path, p);
            p.setStrokeWidth(1.2f);
            p.setColor(0x80FFFFFF);
            c.drawLine(17, 24, 28, 22, p);
            c.restore();
        }
    }

    /** Hairline that fades from white into the spectrum. */
    static final class SpectrumRule extends View {
        private final Paint p = new Paint();

        SpectrumRule(Context c) {
            super(c);
        }

        @Override
        protected void onSizeChanged(int w, int h, int ow, int oh) {
            int[] cols = new int[PrismTheme.SPECTRUM.length + 2];
            cols[0] = 0x00FFFFFF;
            cols[1] = Color.WHITE;
            System.arraycopy(PrismTheme.SPECTRUM, 0, cols, 2, PrismTheme.SPECTRUM.length);
            p.setShader(new LinearGradient(0, 0, w, 0, cols, null, Shader.TileMode.CLAMP));
        }

        @Override
        protected void onDraw(Canvas c) {
            c.drawRect(0, 0, getWidth(), getHeight(), p);
        }
    }

    /** Heading tape + attitude, drawn like the HUD itself (white on black). */
    static final class HeadingTape extends View {
        private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint txt = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint big = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint box = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fill = new Paint();
        private final RectF readout = new RectF();
        private float heading = Float.NaN, pitch, roll;
        private boolean live;

        HeadingTape(Context c, Typeface cond) {
            super(c);
            float d = getResources().getDisplayMetrics().density;
            line.setColor(Color.WHITE);
            line.setStrokeWidth(1.2f * d);
            txt.setColor(Color.WHITE);
            txt.setTypeface(cond);
            txt.setTextSize(12 * d);
            txt.setTextAlign(Paint.Align.CENTER);
            big.setColor(Color.WHITE);
            big.setTypeface(cond);
            big.setTextSize(20 * d);
            big.setTextAlign(Paint.Align.CENTER);
            box.setStyle(Paint.Style.STROKE);
            box.setStrokeWidth(1.4f * d);
            box.setColor(Color.WHITE);
        }

        void set(float h, float p, float r, boolean isLive) {
            heading = h;
            pitch = p;
            roll = r;
            live = isLive;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas c) {
            float w = getWidth(), h = getHeight(), d = getResources().getDisplayMetrics().density;
            c.drawColor(Color.BLACK);
            float cx = w / 2f;
            if (Float.isNaN(heading)) {
                txt.setColor(PrismTheme.MUTED);
                c.drawText("NO HUD DATA", cx, h / 2 + 4 * d, txt);
                txt.setColor(Color.WHITE);
                return;
            }
            float pxPerDeg = w / 90f; // 90 degree window
            int start = (int) Math.floor((heading - 45) / 5) * 5;
            for (int deg = start; deg <= heading + 45; deg += 5) {
                float x = cx + (deg - heading) * pxPerDeg;
                int hd = ((deg % 360) + 360) % 360;
                boolean major = hd % 10 == 0;
                line.setAlpha(major ? 255 : 130);
                c.drawLine(x, 0, x, major ? 9 * d : 5 * d, line);
                if (hd % 30 == 0 && Math.abs(x - cx) > 30 * d) {
                    String lab = hd == 0 ? "N" : hd == 90 ? "E" : hd == 180 ? "S" : hd == 270 ? "W" : String.valueOf(hd / 10);
                    c.drawText(lab, x, 23 * d, txt);
                }
            }
            line.setAlpha(255);
            // centre readout box
            String hs = String.format(Locale.US, "%03d", Math.round(heading) % 360);
            readout.set(cx - 30 * d, 6 * d, cx + 30 * d, 34 * d);
            fill.setColor(Color.BLACK);
            c.drawRect(readout, fill);
            c.drawRect(readout, box);
            c.drawText(hs + "°", cx, 27 * d, big);
            // artificial horizon strip below: roll-rotated line, offset by pitch
            float hy = h * 0.72f + pitch * 0.9f * d;
            c.save();
            c.rotate(-roll, cx, h * 0.72f);
            line.setAlpha(170);
            c.drawLine(cx - w * 0.42f, hy, cx - 18 * d, hy, line);
            c.drawLine(cx + 18 * d, hy, cx + w * 0.42f, hy, line);
            line.setAlpha(255);
            c.restore();
            // boresight
            float by = h * 0.72f;
            c.drawLine(cx - 10 * d, by, cx - 4 * d, by, line);
            c.drawLine(cx + 4 * d, by, cx + 10 * d, by, line);
            c.drawLine(cx, by - 10 * d, cx, by - 4 * d, line);
            if (!live) {
                txt.setColor(PrismTheme.WARN);
                c.drawText("STALE", w - 26 * d, h - 8 * d, txt);
                txt.setColor(Color.WHITE);
            }
        }
    }

    /** Segmented control: one selected option, white fill. */
    static final class Segmented extends LinearLayout {
        interface OnPick {
            void onPick(int index);
        }

        private final PrismTheme t;
        private final TextView[] items;
        private int selected = -1;

        Segmented(PrismTheme t, String[] labels, OnPick onPick) {
            super(t.ctx);
            this.t = t;
            setOrientation(HORIZONTAL);
            setBackground(t.box(PrismTheme.PANEL, PrismTheme.LINE, 5));
            setPadding(t.dp(3), t.dp(3), t.dp(3), t.dp(3));
            items = new TextView[labels.length];
            for (int i = 0; i < labels.length; i++) {
                final int idx = i;
                TextView v = t.text(labels[i].toUpperCase(), t.cond, 12, PrismTheme.MUTED);
                v.setLetterSpacing(0.1f);
                v.setGravity(Gravity.CENTER);
                v.setPadding(0, t.dp(8), 0, t.dp(8));
                v.setOnClickListener(x -> {
                    select(idx);
                    onPick.onPick(idx);
                });
                addView(v, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
                items[i] = v;
            }
        }

        void select(int idx) {
            if (idx == selected || idx < 0 || idx >= items.length) return;
            selected = idx;
            for (int i = 0; i < items.length; i++) {
                boolean on = i == idx;
                items[i].setBackground(on ? t.box(Color.rgb(236, 236, 236), 0, 3) : null);
                items[i].setTextColor(on ? Color.rgb(9, 9, 9) : PrismTheme.MUTED);
            }
        }
    }

    /** Status pill: coloured dot + caps text. */
    static final class Pill extends LinearLayout {
        private final View dot;
        private final TextView label;
        private final PrismTheme t;

        Pill(PrismTheme t) {
            super(t.ctx);
            this.t = t;
            setOrientation(HORIZONTAL);
            setGravity(Gravity.CENTER_VERTICAL);
            setPadding(t.dp(9), t.dp(5), t.dp(10), t.dp(5));
            setBackground(t.box(PrismTheme.PANEL, PrismTheme.LINE, 14));
            dot = new View(t.ctx);
            addView(dot, new LayoutParams(t.dp(7), t.dp(7)));
            label = t.text("", t.cond, 11, PrismTheme.TEXT);
            label.setLetterSpacing(0.14f);
            LayoutParams lp = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
            lp.leftMargin = t.dp(7);
            addView(label, lp);
        }

        void set(String text, int color) {
            label.setText(text.toUpperCase());
            dot.setBackground(t.box(color, 0, 4));
        }
    }
}
