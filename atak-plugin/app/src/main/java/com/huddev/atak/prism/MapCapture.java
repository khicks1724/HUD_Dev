package com.huddev.atak.prism;

import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Handler;
import android.view.PixelCopy;

import com.atakmap.android.maps.MapView;
import com.atakmap.map.opengl.GLMapSurface;

import java.io.ByteArrayOutputStream;

/**
 * Puts ATAK's map on the HUD: copies the middle of the map surface (imagery,
 * unit icons, overlays - what ATAK draws) with PixelCopy, JPEG-encodes it and
 * sends it as a usb_link map packet (firmware/main/usb_link.h, type 4).
 * 240x240 at ~4 fps in the HUD's Map mode, 96x96 at 1 fps for the map inset.
 */
final class MapCapture {
    static final int HUD_MODE_MAP = 4;
    static final int LAYER_MAP_INSET = 1 << 10;

    interface Sink {
        boolean send(byte[] packet);
    }

    private final Handler handler;
    private final Sink sink;
    private volatile boolean busy;
    private long lastMs;
    volatile double zoom = 1.0;
    volatile String status = "";
    private int count;
    private long fpsT0 = System.currentTimeMillis();
    volatile double fps;

    MapCapture(Handler handler, Sink sink) {
        this.handler = handler;
        this.sink = sink;
    }

    /** Call often (worker thread); captures when the HUD wants a map. */
    void tick(int hudMode, int hudLayers, boolean live) {
        boolean full = live && hudMode == HUD_MODE_MAP;
        boolean inset = live && !full && hudMode == 0 && hudLayers >= 0 && (hudLayers & LAYER_MAP_INSET) != 0;
        if (!full && !inset) {
            status = "";
            return;
        }
        long now = System.currentTimeMillis();
        if (busy || now - lastMs < (full ? 250 : 1000)) return;
        MapView mv = MapView.getMapView();
        GLMapSurface surface = mv != null ? mv.getGLSurface() : null;
        if (surface == null || surface.getWidth() <= 0 || !surface.getHolder().getSurface().isValid()) {
            status = "Map view not ready";
            return;
        }
        int w = surface.getWidth(), h = surface.getHeight();
        int side = (int) (Math.min(w, h) / Math.max(1.0, zoom));
        Rect src = new Rect((w - side) / 2, (h - side) / 2, (w + side) / 2, (h + side) / 2);
        final int size = full ? 240 : 96;
        final Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        busy = true;
        lastMs = now;
        try {
            PixelCopy.request(surface, src, bmp, result -> {
                try {
                    if (result != PixelCopy.SUCCESS) {
                        status = "Map copy failed (" + result + ")";
                        return;
                    }
                    ByteArrayOutputStream out = new ByteArrayOutputStream(16384);
                    bmp.compress(Bitmap.CompressFormat.JPEG, 72, out);
                    if (sink.send(packet(out.toByteArray(), size))) {
                        status = "";
                        count++;
                        long t = System.currentTimeMillis();
                        if (t - fpsT0 >= 2000) {
                            fps = count * 1000.0 / (t - fpsT0);
                            count = 0;
                            fpsT0 = t;
                        }
                    }
                } finally {
                    bmp.recycle();
                    busy = false;
                }
            }, handler);
        } catch (RuntimeException e) {
            status = "Map copy: " + e.getMessage();
            busy = false;
        }
    }

    /** A5 5A | type 4 | flags | w | h | fov | len | jpeg | sum16 */
    static byte[] packet(byte[] jpg, int size) {
        byte[] b = new byte[2 + 12 + jpg.length + 2];
        b[0] = (byte) 0xA5;
        b[1] = 0x5A;
        b[2] = 4;
        b[4] = (byte) size;
        b[5] = (byte) (size >> 8);
        b[6] = (byte) size;
        b[7] = (byte) (size >> 8);
        int len = jpg.length;
        b[10] = (byte) len;
        b[11] = (byte) (len >> 8);
        b[12] = (byte) (len >> 16);
        b[13] = (byte) (len >> 24);
        System.arraycopy(jpg, 0, b, 14, len);
        int sum = 0;
        for (int i = 2; i < 14 + len; i++) sum += b[i] & 0xFF;
        b[14 + len] = (byte) sum;
        b[15 + len] = (byte) (sum >> 8);
        return b;
    }
}
