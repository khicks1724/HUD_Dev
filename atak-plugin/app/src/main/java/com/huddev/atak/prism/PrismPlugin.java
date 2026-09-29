package com.huddev.atak.prism;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import com.atak.plugins.impl.PluginContextProvider;
import com.atakmap.android.maps.MapView;
import com.atakmap.android.maps.Marker;
import com.atakmap.coremap.maps.coords.GeoPoint;
import com.atakmap.coremap.maps.coords.MGRSPoint;

import org.json.JSONObject;

import java.util.Locale;

import gov.tak.api.commons.graphics.Bitmap;
import gov.tak.api.plugin.IPlugin;
import gov.tak.api.plugin.IServiceController;
import gov.tak.api.ui.IHostUIService;
import gov.tak.api.ui.Pane;
import gov.tak.api.ui.PaneBuilder;
import gov.tak.api.ui.ToolbarItem;
import gov.tak.api.ui.ToolbarItemAdapter;
import gov.tak.platform.marshal.MarshalManager;

/**
 * PRISM: ATAK side of the TAK HUD.
 *  - phone -> HUD over USB-C: own GPS (1 Hz) and map tracks (0.5 Hz)
 *  - HUD -> phone: "@HUD {json}" state stream (heading, attitude, tracks)
 *  - controls: display mode, thermal, brightness, heading sync, track range
 */
public final class PrismPlugin implements IPlugin {
    private static final long TICK_MS = 1000;
    private static final String[] MODES = {"Normal", "Minimal", "Calib", "Status"};
    private static final String[] THERMAL = {"Off", "Full", "Hot"};
    private static final String[] RANGES = {"5 km", "10 km", "20 km", "50 km"};
    private static final double[] RANGE_M = {5000, 10000, 20000, 50000};

    private final Context pluginContext;
    private final IHostUIService hostUi;
    private final ToolbarItem toolbarItem;
    private final HudFeeder feeder = new HudFeeder();

    private HandlerThread worker;
    private Handler handler;
    private UsbCdcLink link;
    private PhoneCompass compass;
    private PrismTheme t;
    private Pane pane;

    // view refs
    private PrismViews.Pill pill;
    private PrismViews.HeadingTape tape;
    private TextView vPitch, vRoll, vSrc, vFix, vSent, vHudTracks, vLink, vNote;
    private PrismViews.Segmented segMode, segThermal, segRange;

    // state (worker thread writes, UI reads)
    private volatile String linkState = "starting";
    private volatile String fixText = "—";
    private volatile int tracksSent, hudTracks = -1, hudLink = -1, hudMode = -1, hudThermal = -1;
    private volatile float hudH = Float.NaN, hudP, hudR;
    private volatile String hudSrc = "—";
    private volatile long lastStateMs;
    private volatile String note = "";
    private boolean greeted;
    private int tick;

    public PrismPlugin(IServiceController services) {
        PluginContextProvider cp = services.getService(PluginContextProvider.class);
        if (cp == null) throw new IllegalStateException("ATAK did not provide a plugin context");
        pluginContext = cp.getPluginContext();
        hostUi = services.getService(IHostUIService.class);
        Drawable icon = pluginContext.getResources().getDrawable(R.drawable.ic_prism);
        toolbarItem = new ToolbarItem.Builder(pluginContext.getString(R.string.app_name),
                MarshalManager.marshal(icon, Drawable.class, Bitmap.class))
                .setIdentifier(pluginContext.getPackageName())
                .setListener(new ToolbarItemAdapter() {
                    @Override
                    public void onClick(ToolbarItem item) {
                        showPane();
                    }
                })
                .build();
    }

    // ------------------------------------------------------------------ lifecycle

    @Override
    public void onStart() {
        if (hostUi != null) hostUi.addToolbarItem(toolbarItem);
        Context app = MapView.getMapView().getContext().getApplicationContext();
        compass = new PhoneCompass(app);
        compass.start();
        link = new UsbCdcLink(app, new UsbCdcLink.Listener() {
            @Override
            public void onState(String s) {
                linkState = s;
                if (!"connected".equals(s)) greeted = false;
                refresh();
            }

            @Override
            public void onLine(String line) {
                int k = line.indexOf("@HUD ");
                if (k >= 0) parseState(line.substring(k + 5));
            }
        });
        worker = new HandlerThread("prism");
        worker.start();
        handler = new Handler(worker.getLooper());
        handler.post(this::loop);
    }

    @Override
    public void onStop() {
        if (hostUi != null) {
            if (pane != null) hostUi.closePane(pane);
            hostUi.removeToolbarItem(toolbarItem);
        }
        if (handler != null) {
            handler.removeCallbacksAndMessages(null);
            if (link != null && link.isOpen()) link.send("stream off");
        }
        handler = null;
        if (worker != null) worker.quitSafely();
        worker = null;
        if (link != null) link.dispose();
        link = null;
        if (compass != null) compass.stop();
        pane = null;
    }

    // ------------------------------------------------------------------ link loop (worker thread)

    private void loop() {
        try {
            link.poll();
            if (!link.isOpen()) {
                HudFeeder.Snapshot s = feeder.collect();   // still show where we are
                fixText = s.fix != null ? formatFix(s.fix) : "—";
            } else {
                if (!greeted) {
                    link.send("");
                    link.send("fake off");       // HUD boots into demo targets
                    link.send("stream on 2");    // HUD state back to us at 2 Hz
                    greeted = true;
                }
                HudFeeder.Snapshot s = feeder.collect();
                fixText = s.fix != null ? formatFix(s.fix) : "—";
                if (s.fix != null) link.send(s.fix);
                if (tick % 2 == 0) {
                    int n = 0;
                    for (String line : s.tracks) {
                        if (!link.send(line)) break;
                        n++;
                    }
                    tracksSent = n;
                }
            }
        } catch (RuntimeException e) {
            linkState = "error: " + e.getMessage();
        }
        tick++;
        refresh();
        if (handler != null) handler.postDelayed(this::loop, TICK_MS);
    }

    /** "fix <lat> <lon> <hae>" as 10-digit MGRS, e.g. "11S MS 12345 67890". */
    private static String formatFix(String fixLine) {
        String[] p = fixLine.split(" ");
        if (p.length < 4) return "—";
        try {
            MGRSPoint m = new MGRSPoint(Double.parseDouble(p[1]), Double.parseDouble(p[2]));
            return String.format(Locale.US, "%s %s %05d %05d", m.getZoneDescriptor(), m.getGridDescriptor(),
                    digits(m.getEastingDescriptor()), digits(m.getNorthingDescriptor()));
        } catch (RuntimeException e) {
            return "—";
        }
    }

    private static int digits(String metres) {
        return (int) Math.floor(Double.parseDouble(metres.trim())) % 100000;
    }

    private void send(String cmd) {
        Handler h = handler;
        if (h != null) h.post(() -> {
            if (link != null) link.send(cmd);
        });
    }

    private void parseState(String json) {
        try {
            JSONObject o = new JSONObject(json);
            JSONObject att = o.getJSONObject("att");
            hudH = (float) att.getDouble("h");
            hudP = (float) att.getDouble("p");
            hudR = (float) att.getDouble("r");
            hudSrc = att.optString("src", "—");
            hudTracks = o.optInt("tracks", -1);
            hudLink = o.optInt("link", -1);
            hudMode = o.optInt("mode", -1);
            hudThermal = o.optInt("thermal", -1);
            lastStateMs = SystemClock.elapsedRealtime();
            refresh();
        } catch (Exception ignored) {
            // partial line from the serial stream
        }
    }

    // ------------------------------------------------------------------ UI

    private void refresh() {
        final View root = tape;
        if (root == null) return;
        root.post(this::render);
    }

    private void render() {
        if (tape == null) return;
        boolean connected = "connected".equals(linkState);
        boolean live = SystemClock.elapsedRealtime() - lastStateMs < 3000;
        if (connected && live) pill.set("Linked", PrismTheme.OK);
        else if (connected) pill.set("Waiting", PrismTheme.WARN);
        else pill.set(linkState.startsWith("waiting") ? "Allow USB" : "No HUD", PrismTheme.BAD);

        tape.set(hudH, hudP, hudR, live);
        vPitch.setText(Float.isNaN(hudH) ? "—" : String.format(Locale.US, "%+.1f°", hudP));
        vRoll.setText(Float.isNaN(hudH) ? "—" : String.format(Locale.US, "%+.1f°", hudR));
        vSrc.setText(hudSrc);
        vFix.setText(fixText);
        vSent.setText(String.valueOf(tracksSent));
        vHudTracks.setText(hudTracks < 0 ? "—" : String.valueOf(hudTracks));
        vLink.setText(hudLink == 2 ? "WI-FI + TAK" : hudLink == 1 ? "WI-FI" : hudLink == 0 ? "USB" : "—");
        if (live && hudMode >= 0) segMode.select(hudMode);
        if (live && hudThermal >= 0) segThermal.select(hudThermal);
        vNote.setText(note);
    }

    private void showPane() {
        if (hostUi == null) return;
        if (pane == null) pane = new PaneBuilder(buildView())
                .setMetaValue(Pane.RELATIVE_LOCATION, Pane.Location.Default)
                .setMetaValue(Pane.PREFERRED_WIDTH_RATIO, 0.36D)
                .setMetaValue(Pane.PREFERRED_HEIGHT_RATIO, 0.9D)
                .build();
        render();
        if (!hostUi.isPaneVisible(pane)) hostUi.showPane(pane, null);
    }

    private View buildView() {
        t = new PrismTheme(pluginContext);
        ScrollView scroll = new ScrollView(pluginContext);
        scroll.setBackgroundColor(PrismTheme.BG);
        LinearLayout col = new LinearLayout(pluginContext);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(t.dp(16), t.dp(14), t.dp(16), t.dp(20));
        scroll.addView(col);

        // header: mark, wordmark, status pill
        LinearLayout head = new LinearLayout(pluginContext);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.addView(new PrismViews.Mark(pluginContext), new LinearLayout.LayoutParams(t.dp(40), t.dp(40)));
        LinearLayout words = new LinearLayout(pluginContext);
        words.setOrientation(LinearLayout.VERTICAL);
        TextView word = t.text("PRISM", t.mark, 22, PrismTheme.TEXT);
        word.setLetterSpacing(0.32f);
        word.setSingleLine(true);
        TextView sub = t.text("TAK HUD LINK", t.cond, 10.5f, PrismTheme.MUTED);
        sub.setLetterSpacing(0.3f);
        words.addView(word);
        words.addView(t.space(3));
        words.addView(sub);
        LinearLayout.LayoutParams wl = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        wl.leftMargin = t.dp(10);
        head.addView(words, wl);
        col.addView(head);
        col.addView(t.space(12));
        col.addView(new PrismViews.SpectrumRule(pluginContext),
                new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, t.dp(2)));
        col.addView(t.space(16));

        // heading tape + attitude tiles
        LinearLayout hvRow = new LinearLayout(pluginContext);
        hvRow.setOrientation(LinearLayout.HORIZONTAL);
        hvRow.setGravity(Gravity.CENTER_VERTICAL);
        hvRow.addView(t.label("HUD view"), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        pill = new PrismViews.Pill(t);
        hvRow.addView(pill);
        col.addView(hvRow);
        col.addView(t.space(6));
        tape = new PrismViews.HeadingTape(pluginContext, t.cond);
        tape.setBackground(t.box(Color.BLACK, PrismTheme.LINE, 4));
        col.addView(tape, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, t.dp(96)));
        col.addView(t.space(8));
        LinearLayout att = row();
        vPitch = value();
        vRoll = value();
        vSrc = value();
        att.addView(t.tile("Pitch", vPitch), t.weight(1, 3));
        att.addView(t.tile("Roll", vRoll), t.weight(1, 3));
        att.addView(t.tile("Hdg src", vSrc), t.weight(1, 3));
        col.addView(att);
        col.addView(t.space(18));

        // feed
        col.addView(t.label("Feed"));
        col.addView(t.space(6));
        LinearLayout f1 = row();
        vFix = value();
        vFix.setTextSize(15);
        f1.addView(t.tile("Your position (MGRS) → HUD", vFix), t.weight(1, 3));
        col.addView(f1);
        col.addView(t.space(6));
        LinearLayout f2 = row();
        vSent = value();
        vHudTracks = value();
        vLink = value();
        f2.addView(t.tile("Sent", vSent), t.weight(1, 3));
        f2.addView(t.tile("On HUD", vHudTracks), t.weight(1, 3));
        f2.addView(t.tile("HUD link", vLink), t.weight(1, 3));
        col.addView(f2);
        col.addView(t.space(8));
        segRange = new PrismViews.Segmented(t, RANGES, i -> HudFeeder.maxRangeM = RANGE_M[i]);
        segRange.select(2);
        col.addView(segRange);
        col.addView(t.space(18));

        // display
        col.addView(t.label("Display"));
        col.addView(t.space(6));
        segMode = new PrismViews.Segmented(t, MODES, i -> send("mode " + i));
        col.addView(segMode);
        col.addView(t.space(8));
        col.addView(t.label("Thermal"));
        col.addView(t.space(6));
        segThermal = new PrismViews.Segmented(t, THERMAL, i -> send("thermal " + i));
        col.addView(segThermal);
        col.addView(t.space(10));
        col.addView(t.label("Brightness"));
        SeekBar bright = new SeekBar(pluginContext);
        bright.setMax(100);
        bright.setProgress(80);
        android.content.res.ColorStateList white = android.content.res.ColorStateList.valueOf(PrismTheme.TEXT);
        bright.setProgressTintList(white);
        bright.setThumbTintList(white);
        bright.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(PrismTheme.LINE));
        bright.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int v, boolean fromUser) {
            }

            @Override
            public void onStartTrackingTouch(SeekBar s) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar s) {
                send("bright " + s.getProgress());
            }
        });
        col.addView(bright);
        col.addView(t.space(18));

        // align
        col.addView(t.label("Align"));
        col.addView(t.space(6));
        TextView sync = t.button("Sync heading to phone");
        sync.setOnClickListener(v -> syncHeading());
        col.addView(sync);
        col.addView(t.space(6));
        LinearLayout a2 = row();
        TextView level = t.button("Level trim");
        level.setOnClickListener(v -> {
            send("cal trim");
            send("save");
            note = "Level trim saved: hold the HUD level on the true horizon when you tap it.";
            refresh();
        });
        TextView save = t.button("Save settings");
        save.setOnClickListener(v -> {
            send("save");
            note = "Saved on the HUD.";
            refresh();
        });
        a2.addView(level, t.weight(1, 3));
        a2.addView(save, t.weight(1, 3));
        col.addView(a2);
        col.addView(t.space(6));
        TextView flip = t.button("Flip IMU");
        flip.setOnClickListener(v -> {
            send("cal flip");
            send("save");
            note = "IMU turned 180° and saved: use if pitch and roll move the wrong way. Tap again to undo.";
            refresh();
        });
        col.addView(flip);
        col.addView(t.space(10));
        vNote = t.text("", t.body, 12, PrismTheme.MUTED);
        col.addView(vNote);
        col.addView(t.space(14));

        TextView help = t.text("Plug the HUD into this phone's USB-C port with a data cable and allow USB access. "
                + "The phone powers the HUD. Sync heading: point the phone's back camera the same way the HUD is looking, then tap.",
                t.body, 12, PrismTheme.MUTED);
        help.setLineSpacing(0, 1.2f);
        col.addView(help);
        return scroll;
    }

    private void syncHeading() {
        MapView mv = MapView.getMapView();
        Marker self = mv != null ? mv.getSelfMarker() : null;
        GeoPoint p = self != null ? self.getPoint() : null;
        if (p == null || !p.isValid()) {
            note = "No GPS fix on the phone yet.";
        } else {
            float h = compass.trueHeading(p.getLatitude(), p.getLongitude(), p.isAltitudeValid() ? p.getAltitude() : 0);
            if (Float.isNaN(h)) {
                note = "Phone compass not ready.";
            } else {
                send(String.format(Locale.US, "hdg %.1f 1", h));
                note = String.format(Locale.US, "HUD heading set to %.0f° true (phone compass).", h);
            }
        }
        refresh();
    }

    private LinearLayout row() {
        LinearLayout r = new LinearLayout(pluginContext);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setPadding(-t.dp(3), 0, -t.dp(3), 0);
        return r;
    }

    private TextView value() {
        return t.text("—", t.cond, 19, PrismTheme.TEXT);
    }
}
