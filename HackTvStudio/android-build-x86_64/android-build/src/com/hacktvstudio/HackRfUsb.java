package com.hacktvstudio;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.util.Log;

import java.util.HashMap;

/**
 * Android USB-host bridge for the HackRF.
 *
 * libusb cannot scan usbfs inside an unprivileged Android app, so the device
 * is found, permission-requested and opened through the Java UsbManager; the
 * resulting file descriptor is handed to libusb via libusb_wrap_sys_device()
 * on the native side (hackrf_open_wrapped). All methods are static and are
 * called over JNI from AndroidUsb.cpp. Context comes from QtNative.activity().
 */
public final class HackRfUsb {
    private static final String TAG = "HackRfUsb";
    private static final String ACTION_PERMISSION = "com.hacktvstudio.USB_PERMISSION";

    // HackRF One / Jawbreaker / rad1o : VID 0x1d50, PIDs 0x6089 / 0x604b / 0xcc15
    private static final int VID = 0x1d50;
    private static final int[] PIDS = {0x6089, 0x604b, 0xcc15, 0x6088};

    private static UsbManager manager;
    private static UsbDevice device;
    private static UsbDeviceConnection connection;
    private static int currentFd = -1;
    private static volatile int permissionState = 0; // 0 pending/none, 1 granted, -1 denied
    private static boolean receiverRegistered = false;

    private HackRfUsb() {}

    private static boolean isHackRf(UsbDevice d) {
        if (d.getVendorId() != VID) return false;
        for (int pid : PIDS) if (d.getProductId() == pid) return true;
        return false;
    }

    /** @return true if a HackRF is physically attached to the USB host port. */
    public static synchronized boolean isPresent(Context ctx) {
        try {
            if (manager == null) manager = (UsbManager) ctx.getSystemService(Context.USB_SERVICE);
            if (manager == null) return false;
            for (UsbDevice d : manager.getDeviceList().values()) if (isHackRf(d)) return true;
        } catch (Exception e) {
            Log.e(TAG, "isPresent", e);
        }
        return false;
    }

    /** A short identifier for the attached HackRF (device name), or "" if none. */
    public static synchronized String deviceName(Context ctx) {
        try {
            if (manager == null) manager = (UsbManager) ctx.getSystemService(Context.USB_SERVICE);
            if (manager == null) return "";
            for (UsbDevice d : manager.getDeviceList().values())
                if (isHackRf(d)) return d.getDeviceName() + " #" + d.getDeviceId();
        } catch (Exception e) {
            Log.e(TAG, "deviceName", e);
        }
        return "";
    }

    private static final BroadcastReceiver RECEIVER = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent intent) {
            if (!ACTION_PERMISSION.equals(intent.getAction())) return;
            synchronized (HackRfUsb.class) {
                boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
                permissionState = granted ? 1 : -1;
                Log.i(TAG, "USB permission " + (granted ? "granted" : "denied"));
            }
        }
    };

    private static void ensureReceiver(Context ctx) {
        if (receiverRegistered) return;
        IntentFilter f = new IntentFilter(ACTION_PERMISSION);
        if (Build.VERSION.SDK_INT >= 33) {
            ctx.registerReceiver(RECEIVER, f, Context.RECEIVER_NOT_EXPORTED);
        } else {
            ctx.registerReceiver(RECEIVER, f);
        }
        receiverRegistered = true;
    }

    /**
     * Start opening the HackRF: finds it, and if permission is missing fires the
     * system permission dialog. Non-blocking. Poll {@link #pollFd(Context)}.
     * @return 1 already open (fd ready), 0 permission requested / pending, -1 no device.
     */
    public static synchronized int requestOpen(Context ctx) {
        try {
            if (currentFd >= 0) return 1;
            if (manager == null) manager = (UsbManager) ctx.getSystemService(Context.USB_SERVICE);
            if (manager == null) return -1;
            device = null;
            for (UsbDevice d : manager.getDeviceList().values()) {
                if (isHackRf(d)) { device = d; break; }
            }
            if (device == null) return -1;
            if (manager.hasPermission(device)) {
                permissionState = 1;
                return doOpen() >= 0 ? 1 : -1;
            }
            ensureReceiver(ctx);
            permissionState = 0;
            int flags = Build.VERSION.SDK_INT >= 31 ? 0x02000000 /* FLAG_MUTABLE */ : 0;
            Intent permIntent = new Intent(ACTION_PERMISSION).setPackage(ctx.getPackageName());
            PendingIntent pi = PendingIntent.getBroadcast(ctx, 0, permIntent, flags);
            manager.requestPermission(device, pi);
            return 0;
        } catch (Exception e) {
            Log.e(TAG, "requestOpen", e);
            return -1;
        }
    }

    private static int doOpen() {
        try {
            if (device == null) return -1;
            connection = manager.openDevice(device);
            if (connection == null) { Log.e(TAG, "openDevice returned null"); return -1; }
            currentFd = connection.getFileDescriptor();
            Log.i(TAG, "HackRF opened, fd=" + currentFd);
            return currentFd;
        } catch (Exception e) {
            Log.e(TAG, "doOpen", e);
            return -1;
        }
    }

    /**
     * @return fd (>=0) once permission is granted and the device is open,
     *         0-or-negative sentinels otherwise: -2 pending, -1 denied/failed.
     */
    public static synchronized int pollFd(Context ctx) {
        if (currentFd >= 0) return currentFd;
        if (permissionState == 1) return doOpen();
        if (permissionState == -1) return -1;
        return -2; // still waiting for the user
    }

    public static synchronized int getFd() { return currentFd; }

    /** Close the USB connection (native side has already called hackrf_close). */
    public static synchronized void close() {
        try {
            if (connection != null) connection.close();
        } catch (Exception e) {
            Log.e(TAG, "close", e);
        }
        connection = null;
        device = null;
        currentFd = -1;
        permissionState = 0;
    }
}
