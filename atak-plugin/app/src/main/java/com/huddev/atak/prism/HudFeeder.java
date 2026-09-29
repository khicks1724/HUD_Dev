package com.huddev.atak.prism;

import com.atakmap.android.maps.MapGroup;
import com.atakmap.android.maps.MapView;
import com.atakmap.android.maps.Marker;
import com.atakmap.android.maps.PointMapItem;
import com.atakmap.coremap.maps.coords.GeoPoint;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * Builds the HUD console lines (firmware/main/console_cmds.c):
 *   fix <lat> <lon> <hae>
 *   trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign|->
 * Tracks are nearest first, so the cap keeps the ones that matter.
 */
final class HudFeeder {
    static final int MAX_TRACKS = 40;
    static volatile double maxRangeM = 20000;

    /** Names that are really coordinates or UIDs, e.g. "32.4441496 -116.136" or a GUID. */
    private static final Pattern NOT_A_NAME = Pattern.compile(
            ".*-?\\d{1,3}\\.\\d{3,}.*|[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-.*|a-[a-z]-[A-Z](-.*)?");

    static final class Snapshot {
        String fix;
        final List<String> tracks = new ArrayList<>();
    }

    private static final class Track {
        final double range;
        final String line;

        Track(double range, String line) {
            this.range = range;
            this.line = line;
        }
    }

    /** One whitespace-free console token. */
    private static String token(String s) {
        return s.trim().replaceAll("[\\s\"]+", "_");
    }

    /** Display name for a unit, or "-" (HUD shows range only) when it has no real name. */
    static String name(PointMapItem item) {
        String[] candidates = {item.getMetaString("callsign", null), item.getTitle()};
        for (String c : candidates) {
            if (c == null) continue;
            String t = c.trim();
            if (t.isEmpty() || t.equals(item.getUID()) || NOT_A_NAME.matcher(t).matches()) continue;
            return token(t);
        }
        return "-";
    }

    private static double hae(GeoPoint p) {
        return p.isAltitudeValid() ? p.getAltitude() : 0.0;
    }

    Snapshot collect() {
        Snapshot out = new Snapshot();
        MapView mv = MapView.getMapView();
        if (mv == null) return out;
        Marker self = mv.getSelfMarker();
        GeoPoint me = self != null ? self.getPoint() : null;
        final String selfUid = self != null ? self.getUID() : "";
        final boolean haveMe = me != null && me.isValid() && (me.getLatitude() != 0 || me.getLongitude() != 0);
        if (haveMe) {
            out.fix = String.format(Locale.US, "fix %.7f %.7f %.1f", me.getLatitude(), me.getLongitude(), hae(me));
        }
        final GeoPoint origin = haveMe ? me : null;
        final List<Track> found = new ArrayList<>();
        mv.getRootGroup().deepForEachItem(new MapGroup.OnItemCallback<PointMapItem>(PointMapItem.class) {
            @Override
            protected boolean onMapItem(PointMapItem item) {
                String type = item.getType();
                if (type == null || !type.startsWith("a-")) return false;
                if (selfUid.equals(item.getUID())) return false;
                if (!item.getVisible()) return false;
                GeoPoint p = item.getPoint();
                if (p == null || !p.isValid()) return false;
                double r = origin != null ? origin.distanceTo(p) : 0;
                if (origin != null && r > maxRangeM) return false;
                found.add(new Track(r, String.format(Locale.US, "trk %s %s %.7f %.7f %.1f %d %s",
                        token(item.getUID()), token(type),
                        p.getLatitude(), p.getLongitude(), hae(p), 30, name(item))));
                return false;
            }
        });
        Collections.sort(found, (a, b) -> Double.compare(a.range, b.range));
        for (int i = 0; i < found.size() && i < MAX_TRACKS; i++) out.tracks.add(found.get(i).line);
        return out;
    }
}
