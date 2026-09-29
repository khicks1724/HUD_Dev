package com.huddev.atak.prism;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * PRISM look: near-black glass, hairline structure, white type, and the
 * spectrum only where light "passes through" (the mark and the rule).
 * Barlow is a free DIN-style face (the closest open match to Bahnschrift);
 * Orbitron is kept for the PRISM wordmark.
 */
final class PrismTheme {
    static final int BG = Color.rgb(7, 8, 10);
    static final int PANEL = Color.rgb(14, 16, 19);
    static final int PANEL_HI = Color.rgb(22, 25, 29);
    static final int LINE = Color.rgb(35, 38, 43);
    static final int TEXT = Color.rgb(241, 241, 241);
    static final int MUTED = Color.rgb(140, 145, 153);
    static final int OK = Color.rgb(52, 211, 153);
    static final int WARN = Color.rgb(247, 185, 85);
    static final int BAD = Color.rgb(248, 113, 113);
    static final int FRIEND = Color.rgb(56, 189, 248);
    static final int[] SPECTRUM = {
            Color.rgb(122, 92, 255), Color.rgb(61, 169, 255), Color.rgb(52, 211, 153),
            Color.rgb(255, 230, 92), Color.rgb(255, 159, 67), Color.rgb(255, 77, 109)};

    final Context ctx;
    final Typeface body, bodyMedium, cond, mark;

    PrismTheme(Context ctx) {
        this.ctx = ctx;
        body = font(R.font.barlow_regular, Typeface.SANS_SERIF);
        bodyMedium = font(R.font.barlow_medium, Typeface.SANS_SERIF);
        cond = font(R.font.barlow_condensed_semibold, Typeface.create("sans-serif-condensed", Typeface.BOLD));
        mark = font(R.font.orbitron_semibold, Typeface.DEFAULT_BOLD);
    }

    private Typeface font(int id, Typeface fallback) {
        if (Build.VERSION.SDK_INT >= 26) {
            try {
                Typeface t = ctx.getResources().getFont(id);
                if (t != null) return t;
            } catch (RuntimeException ignored) {
            }
        }
        return fallback;
    }

    int dp(float v) {
        return Math.round(v * ctx.getResources().getDisplayMetrics().density);
    }

    TextView text(String s, Typeface tf, float sp, int color) {
        TextView t = new TextView(ctx);
        t.setText(s);
        t.setTypeface(tf);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setIncludeFontPadding(false);
        return t;
    }

    /** Small spaced caps label, e.g. "DISPLAY". */
    TextView label(String s) {
        TextView t = text(s.toUpperCase(), cond, 11, MUTED);
        t.setLetterSpacing(0.18f);
        return t;
    }

    GradientDrawable box(int fill, int stroke, float radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(radiusDp));
        if (stroke != 0) d.setStroke(Math.max(1, dp(1)), stroke);
        return d;
    }

    /** Label over a big value, used for readouts. */
    LinearLayout tile(String label, TextView value) {
        LinearLayout t = new LinearLayout(ctx);
        t.setOrientation(LinearLayout.VERTICAL);
        t.setPadding(dp(10), dp(8), dp(10), dp(9));
        t.setBackground(box(PANEL, LINE, 4));
        t.addView(label(label));
        View gap = new View(ctx);
        gap.setLayoutParams(new LinearLayout.LayoutParams(1, dp(4)));
        t.addView(gap);
        t.addView(value);
        return t;
    }

    LinearLayout.LayoutParams weight(float w, int marginDp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, w);
        lp.setMargins(dp(marginDp), 0, dp(marginDp), 0);
        return lp;
    }

    View space(int dpH) {
        View v = new View(ctx);
        v.setLayoutParams(new LinearLayout.LayoutParams(1, dp(dpH)));
        return v;
    }

    /** Full-width outline button with condensed caps text. */
    TextView button(String s) {
        TextView b = text(s.toUpperCase(), cond, 13, TEXT);
        b.setLetterSpacing(0.12f);
        b.setGravity(Gravity.CENTER);
        b.setPadding(dp(12), dp(11), dp(12), dp(11));
        b.setBackground(box(PANEL_HI, Color.rgb(70, 74, 80), 5));
        b.setClickable(true);
        return b;
    }
}
