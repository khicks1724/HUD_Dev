package com.huddev.atak.hudlink;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.TypedValue;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.atak.plugins.impl.PluginContextProvider;
import com.atakmap.android.maps.MapView;

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
 * HUD Link: when the TAK HUD is plugged into this phone's USB-C port, send it
 * the phone's own position once a second and the map's tracks every two
 * seconds, as text lines its serial console understands.
 */
public final class HudLinkPlugin implements IPlugin {
    private static final long TICK_MS = 1000;

    private final Context pluginContext;
    private final IHostUIService hostUi;
    private final ToolbarItem toolbarItem;
    private final HudFeeder feeder = new HudFeeder();

    private HandlerThread worker;
    private Handler handler;
    private UsbCdcLink link;
    private Pane pane;
    private TextView stateView, statsView, hudView;
    private volatile String state = "starting";
    private volatile String lastHud = "";
    private volatile String lastFix = "no GPS fix yet";
    private int tick, lastTrackCount;
    private long linesSent;
    private boolean greeted;

    public HudLinkPlugin(IServiceController services) {
        PluginContextProvider cp = services.getService(PluginContextProvider.class);
        if (cp == null) throw new IllegalStateException("ATAK did not provide a plugin context");
        pluginContext = cp.getPluginContext();
        hostUi = services.getService(IHostUIService.class);
        Drawable icon = pluginContext.getResources().getDrawable(R.drawable.ic_hudlink);
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

    @Override
    public void onStart() {
        if (hostUi != null) hostUi.addToolbarItem(toolbarItem);
        Context app = MapView.getMapView().getContext().getApplicationContext();
        link = new UsbCdcLink(app, new UsbCdcLink.Listener() {
            @Override
            public void onState(String s) {
                state = s;
                if (!"connected".equals(s)) greeted = false;
                refresh();
            }

            @Override
            public void onLine(String line) {
                // show the HUD's replies (e.g. "status") but skip our own echoes
                if (!line.startsWith("hud>") && !line.startsWith("fix ") && !line.startsWith("trk ")) {
                    lastHud = line;
                    refresh();
                }
            }
        });
        worker = new HandlerThread("hudlink");
        worker.start();
        handler = new Handler(worker.getLooper());
        handler.post(this::loop);
    }

    private void loop() {
        try {
            link.poll();
            if (link.isOpen()) {
                if (!greeted) {
                    // The HUD boots into fake targets; switch to real data.
                    link.send("");
                    link.send("fake off");
                    greeted = true;
                }
                HudFeeder.Snapshot s = feeder.collect();
                if (s.fix != null && link.send(s.fix)) {
                    linesSent++;
                    lastFix = s.fix.substring(4);
                }
                if (tick % 2 == 0) {
                    for (String t : s.tracks) {
                        if (!link.send(t)) break;
                        linesSent++;
                    }
                    lastTrackCount = s.tracks.size();
                }
            }
        } catch (RuntimeException e) {
            state = "error: " + e.getMessage();
        }
        tick++;
        refresh();
        if (handler != null) handler.postDelayed(this::loop, TICK_MS);
    }

    private void refresh() {
        final TextView sv = stateView;
        if (sv == null) return;
        sv.post(() -> {
            stateView.setText("HUD: " + state);
            stateView.setTextColor("connected".equals(state) ? Color.rgb(52, 211, 153) : Color.rgb(247, 185, 85));
            statsView.setText(String.format(Locale.US, "Own position: %s\nTracks sent: %d (within %.0f km)\nLines sent: %d",
                    lastFix, lastTrackCount, HudFeeder.MAX_RANGE_M / 1000, linesSent));
            hudView.setText(lastHud.isEmpty() ? "" : "HUD says: " + lastHud);
        });
    }

    private void showPane() {
        if (hostUi == null) return;
        if (pane == null) {
            LinearLayout root = new LinearLayout(pluginContext);
            root.setOrientation(LinearLayout.VERTICAL);
            int pad = dp(14);
            root.setPadding(pad, pad, pad, pad);
            root.setBackgroundColor(Color.rgb(4, 4, 4));
            TextView title = text("HUD LINK", 15, Color.WHITE);
            title.setTypeface(Typeface.DEFAULT_BOLD);
            title.setLetterSpacing(0.2f);
            stateView = text("HUD: " + state, 14, Color.rgb(247, 185, 85));
            statsView = text("", 13, Color.rgb(199, 199, 199));
            hudView = text("", 12, Color.rgb(163, 163, 163));
            TextView help = text("Plug the HUD into this phone's USB-C port (data cable) and tap OK on the USB prompt. "
                    + "Your GPS and the map's tracks are sent over the cable; the phone also powers the HUD.", 12,
                    Color.rgb(163, 163, 163));
            Button status = new Button(pluginContext);
            status.setText("Ask HUD for status");
            status.setOnClickListener(v -> {
                if (handler != null) handler.post(() -> link.send("status"));
            });
            root.addView(title);
            root.addView(space());
            root.addView(stateView);
            root.addView(statsView);
            root.addView(space());
            root.addView(status);
            root.addView(hudView);
            root.addView(space());
            root.addView(help);
            pane = new PaneBuilder(root)
                    .setMetaValue(Pane.RELATIVE_LOCATION, Pane.Location.Default)
                    .setMetaValue(Pane.PREFERRED_WIDTH_RATIO, 0.35D)
                    .setMetaValue(Pane.PREFERRED_HEIGHT_RATIO, 0.5D)
                    .build();
        }
        refresh();
        if (!hostUi.isPaneVisible(pane)) hostUi.showPane(pane, null);
    }

    private TextView text(String s, int sp, int color) {
        TextView t = new TextView(pluginContext);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        return t;
    }

    private View space() {
        View v = new View(pluginContext);
        v.setLayoutParams(new LinearLayout.LayoutParams(1, dp(10)));
        return v;
    }

    private int dp(int v) {
        return Math.round(v * pluginContext.getResources().getDisplayMetrics().density);
    }

    @Override
    public void onStop() {
        if (hostUi != null) {
            if (pane != null) hostUi.closePane(pane);
            hostUi.removeToolbarItem(toolbarItem);
        }
        if (handler != null) handler.removeCallbacksAndMessages(null);
        handler = null;
        if (worker != null) worker.quitSafely();
        worker = null;
        if (link != null) link.dispose();
        link = null;
        pane = null;
        stateView = statsView = hudView = null;
    }
}
