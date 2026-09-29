package com.huddev.atak.prism;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;

import java.nio.charset.StandardCharsets;

/**
 * Minimal USB CDC-ACM link to the HUD's CH343 USB-serial bridge
 * (VID 0x1A86, PID 0x55D3), which enumerates as a standard CDC device.
 *
 * DTR and RTS are left de-asserted: on the Waveshare board they drive the
 * ESP32's EN/BOOT auto-reset circuit, so toggling them would reset the HUD.
 */
final class UsbCdcLink {
    interface Listener {
        void onState(String state);

        void onLine(String line);
    }

    static final int VID_WCH = 0x1A86;
    static final int PID_CH343 = 0x55D3;
    private static final String ACTION_PERMISSION = "com.huddev.atak.prism.USB_PERMISSION";

    private final Context appContext;
    private final UsbManager usb;
    private final Listener listener;

    private UsbDevice device;
    private UsbDeviceConnection conn;
    private UsbInterface ctrlIf, dataIf;
    private UsbEndpoint epOut, epIn;
    private Thread reader;
    private volatile boolean open;
    private boolean permissionAsked;

    private final BroadcastReceiver permissionReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent intent) {
            if (!ACTION_PERMISSION.equals(intent.getAction())) return;
            permissionAsked = false;
            if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                poll();
            } else {
                listener.onState("USB permission denied");
            }
        }
    };

    UsbCdcLink(Context appContext, Listener listener) {
        this.appContext = appContext;
        this.listener = listener;
        this.usb = (UsbManager) appContext.getSystemService(Context.USB_SERVICE);
        IntentFilter f = new IntentFilter(ACTION_PERMISSION);
        if (Build.VERSION.SDK_INT >= 33) {
            appContext.registerReceiver(permissionReceiver, f, Context.RECEIVER_EXPORTED);
        } else {
            appContext.registerReceiver(permissionReceiver, f);
        }
    }

    boolean isOpen() {
        return open;
    }

    /** Look for the HUD; ask for permission or open it. Safe to call repeatedly. */
    synchronized void poll() {
        if (usb == null) {
            listener.onState("no USB host support");
            return;
        }
        UsbDevice found = null;
        for (UsbDevice d : usb.getDeviceList().values()) {
            if (d.getVendorId() == VID_WCH && d.getProductId() == PID_CH343) {
                found = d;
                break;
            }
        }
        if (found == null) {
            if (open) close("HUD unplugged");
            else listener.onState("HUD not connected");
            return;
        }
        if (open) return;
        if (!usb.hasPermission(found)) {
            if (!permissionAsked) {
                permissionAsked = true;
                Intent i = new Intent(ACTION_PERMISSION).setPackage(appContext.getPackageName());
                int flags = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0;
                usb.requestPermission(found, PendingIntent.getBroadcast(appContext, 0, i, flags));
                listener.onState("waiting for USB permission (tap OK)");
            }
            return;
        }
        openDevice(found);
    }

    private void openDevice(UsbDevice d) {
        ctrlIf = null;
        dataIf = null;
        for (int i = 0; i < d.getInterfaceCount(); i++) {
            UsbInterface ifc = d.getInterface(i);
            if (ifc.getInterfaceClass() == UsbConstants.USB_CLASS_COMM && ctrlIf == null) ctrlIf = ifc;
            if (ifc.getInterfaceClass() == UsbConstants.USB_CLASS_CDC_DATA && dataIf == null) dataIf = ifc;
        }
        if (dataIf == null) {
            listener.onState("HUD found but no CDC data interface");
            return;
        }
        epOut = null;
        epIn = null;
        for (int i = 0; i < dataIf.getEndpointCount(); i++) {
            UsbEndpoint ep = dataIf.getEndpoint(i);
            if (ep.getType() != UsbConstants.USB_ENDPOINT_XFER_BULK) continue;
            if (ep.getDirection() == UsbConstants.USB_DIR_OUT) epOut = ep;
            else epIn = ep;
        }
        UsbDeviceConnection c = usb.openDevice(d);
        if (c == null || epOut == null) {
            listener.onState("could not open the HUD");
            return;
        }
        if (ctrlIf != null) c.claimInterface(ctrlIf, true);
        c.claimInterface(dataIf, true);
        // SET_LINE_CODING: 2,000,000 8N1 (the HUD's USB link, firmware/main/usb_link.h)
        byte[] coding = {(byte) 0x80, (byte) 0x84, 0x1E, 0x00, 0, 0, 8};
        int ifNum = ctrlIf != null ? ctrlIf.getId() : 0;
        c.controlTransfer(0x21, 0x20, 0, ifNum, coding, coding.length, 500);
        // SET_CONTROL_LINE_STATE: DTR=0, RTS=0 (never reset the ESP32)
        c.controlTransfer(0x21, 0x22, 0, ifNum, null, 0, 500);
        conn = c;
        device = d;
        open = true;
        listener.onState("connected");
        startReader();
    }

    private void startReader() {
        reader = new Thread(() -> {
            byte[] buf = new byte[512];
            StringBuilder line = new StringBuilder();
            while (open) {
                UsbDeviceConnection c = conn;
                if (c == null || epIn == null) break;
                int n = c.bulkTransfer(epIn, buf, buf.length, 250);
                if (n < 0) continue;
                for (int i = 0; i < n; i++) {
                    char ch = (char) (buf[i] & 0xFF);
                    if (ch == '\n') {
                        String s = line.toString().trim();
                        line.setLength(0);
                        if (!s.isEmpty()) listener.onLine(s);
                    } else if (ch != '\r' && line.length() < 512) {
                        line.append(ch);
                    }
                }
            }
        }, "prism-rx");
        reader.setDaemon(true);
        reader.start();
    }

    /** Send one console line. Returns false if the link is down. */
    synchronized boolean send(String line) {
        if (!open || conn == null) return false;
        byte[] b = (line + "\r\n").getBytes(StandardCharsets.US_ASCII);
        int off = 0;
        while (off < b.length) {
            int chunk = Math.min(64, b.length - off);
            byte[] part = new byte[chunk];
            System.arraycopy(b, off, part, 0, chunk);
            int n = conn.bulkTransfer(epOut, part, chunk, 500);
            if (n < 0) {
                close("write failed");
                return false;
            }
            off += chunk;
        }
        return true;
    }

    synchronized void close(String why) {
        open = false;
        if (conn != null) {
            try {
                if (dataIf != null) conn.releaseInterface(dataIf);
                if (ctrlIf != null) conn.releaseInterface(ctrlIf);
            } catch (RuntimeException ignored) {
            }
            conn.close();
        }
        conn = null;
        device = null;
        listener.onState(why);
    }

    void dispose() {
        close("stopped");
        try {
            appContext.unregisterReceiver(permissionReceiver);
        } catch (IllegalArgumentException ignored) {
        }
    }
}
