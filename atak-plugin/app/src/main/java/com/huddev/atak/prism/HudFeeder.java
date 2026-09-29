package com.huddev.atak.prism;

import com.atakmap.android.maps.MapGroup;
import com.atakmap.android.maps.MapItem;
import com.atakmap.android.maps.MapView;
import com.atakmap.android.maps.Marker;
import com.atakmap.android.maps.PointMapItem;
import com.atakmap.coremap.maps.coords.GeoPoint;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Builds the HUD console lines (firmware/main/console_cmds.c):
 *   fix <lat> <lon> <hae>
 *   trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign>
 */
final class HudFeeder {
    static final int MAX_TRACKS = 60;
    static volatile double maxRangeM = 20000;

    static final class Snapshot {
        String fix;
        final List<String> tracks = new ArrayList<>();
    }

    private static String clean(String s, String fallback) {
        if (s == null || s.trim().isEmpty()) return fallback;
        // The HUD console splits on whitespace; uid/type must be one token.
        return s.trim().replaceAll("[\\s\"]+", "_");
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
        if (me != null && me.isValid() && (me.getLatitude() != 0 || me.getLongitude() != 0)) {
            out.fix = String.format(Locale.US, "fix %.7f %.7f %.1f", me.getLatitude(), me.getLongitude(), hae(me));
        }
        final GeoPoint origin = me;
        mv.getRootGroup().deepForEachItem(new MapGroup.OnItemCallback<PointMapItem>(PointMapItem.class) {
            @Override
            protected boolean onMapItem(PointMapItem item) {
                if (out.tracks.size() >= MAX_TRACKS) return true; // stop
                String type = item.getType();
                if (type == null || !type.startsWith("a-")) return false;
                if (selfUid.equals(item.getUID())) return false;
                if (!item.getVisible()) return false;
                GeoPoint p = item.getPoint();
                if (p == null || !p.isValid()) return false;
                if (origin != null && origin.isValid() && origin.distanceTo(p) > maxRangeM) return false;
                String cs = item.getMetaString("callsign", item.getTitle());
                out.tracks.add(String.format(Locale.US, "trk %s %s %.7f %.7f %.1f %d %s",
                        clean(item.getUID(), "unknown"), clean(type, "a-u-G"),
                        p.getLatitude(), p.getLongitude(), hae(p), 30,
                        clean(cs, clean(item.getUID(), "?"))));
                return false;
            }
        });
        return out;
    }
}
