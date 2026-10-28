package com.hacktvstudio;

import android.os.Bundle;
import org.qtproject.qt.android.bindings.QtActivity;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.io.StringWriter;

/**
 * QtActivity subclass used only to diagnose launch crashes on head units that
 * cannot be reached over adb. Before Qt loads its native libraries we start a
 * logcat capture into the app's own external files dir (no runtime permission
 * needed) and install an uncaught-exception handler, so after a crash the user
 * can read the reason from:
 *     Android/data/com.hacktvstudio/files/hacktv_log.txt     (logcat)
 *     Android/data/com.hacktvstudio/files/hacktv_crash.txt   (Java stack trace)
 */
public class HackTvActivity extends QtActivity {

    @Override
    public void onCreate(Bundle savedInstanceState) {
        // Prefer a plugged-in USB stick (its app dir is writable with no runtime
        // permission and the user can read it on a PC), fall back to internal.
        final File dir = pickLogDir();

        // Capture this process's logcat to a file that survives the crash.
        try {
            if (dir != null) {
                dir.mkdirs();
                new ProcessBuilder()
                        .command("logcat", "-v", "threadtime")
                        .redirectErrorStream(true)
                        .redirectOutput(new File(dir, "hacktv_log.txt"))
                        .start();
            }
        } catch (Throwable ignored) {}

        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override public void uncaughtException(Thread t, Throwable e) {
                writeCrash(dir, e);
                android.os.Process.killProcess(android.os.Process.myPid());
                System.exit(2);
            }
        });

        try {
            super.onCreate(savedInstanceState);
        } catch (Throwable e) {
            writeCrash(dir, e);
            throw e;
        }
    }

    /** The USB/SD app dir (last getExternalFilesDirs entry) if one is plugged in,
     *  otherwise the internal app dir. */
    private File pickLogDir() {
        try {
            File[] dirs = getExternalFilesDirs(null);
            if (dirs != null) {
                for (int i = dirs.length - 1; i >= 1; i--)
                    if (dirs[i] != null) return dirs[i];
                if (dirs.length > 0 && dirs[0] != null) return dirs[0];
            }
        } catch (Throwable ignored) {}
        return getExternalFilesDir(null);
    }

    private void writeCrash(File dir, Throwable e) {
        try {
            if (dir == null) return;
            dir.mkdirs();
            StringWriter sw = new StringWriter();
            e.printStackTrace(new PrintWriter(sw));
            FileWriter w = new FileWriter(new File(dir, "hacktv_crash.txt"));
            w.write("HackTv Studio crash\n\n" + sw);
            w.close();
        } catch (Throwable ignored) {}
    }
}
