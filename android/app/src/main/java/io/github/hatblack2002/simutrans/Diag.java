package io.github.hatblack2002.simutrans;

import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Diagnostics core (spec §5): boot markers, boot-in-progress flag, Java crash handler,
 * report packaging. Native code writes its own markers into the same files (line-append).
 *
 * Layout under getFilesDir() (the same dir the native side sees via
 * SDL_AndroidGetInternalStoragePath()):
 *   diag/boot.log            — stage markers, one per line: "<unix_ms> <marker>"
 *   diag/boot-in-progress    — flag; deleted by native after first_frame_drawn
 *   diag/pending/*.md        — reports waiting for upload (DiagUploader)
 *   diag/upload.log          — uploader status lines
 */
public final class Diag {

    private static final String TAG = "simutrans-diag";
    private static Context sApp = null;
    private static Thread sUploaderThread = null;

    private Diag() {}

    public static File dir(Context c)  { return new File(c.getFilesDir(), "diag"); }
    public static File pendingDir(Context c) { return new File(dir(c), "pending"); }
    public static File bootLog(Context c) { return new File(dir(c), "boot.log"); }
    public static File bootFlag(Context c) { return new File(dir(c), "boot-in-progress"); }

    /** Called from SimutransActivity.onCreate BEFORE super.onCreate. */
    public static synchronized void init(Context c) {
        sApp = c.getApplicationContext();
        try {
            pendingDir(sApp).mkdirs();
        } catch (Exception e) {
            Log.e(TAG, "mkdir failed: " + e);
        }

        // If the flag survived, the previous boot died before the first frame.
        File flag = bootFlag(sApp);
        if (flag.exists()) {
            packageReport("boot-failure", readFileText(bootLog(sApp)));
        }
        try (FileWriter w = new FileWriter(bootLog(sApp), false)) {
            // truncate for this boot
        } catch (IOException e) {
            Log.e(TAG, "boot.log truncate failed: " + e);
        }
        log("java_on_create");
        try {
            new FileWriter(flag, false).close();
        } catch (IOException e) {
            Log.e(TAG, "boot flag create failed: " + e);
        }
        installJavaCrashHandler();
    }

    /** Append one stage marker to boot.log (+logcat). Safe from any thread. */
    public static void log(String marker) {
        if (sApp == null) return;
        try (FileWriter w = new FileWriter(bootLog(sApp), true)) {
            w.write(System.currentTimeMillis() + " " + marker + "\n");
        } catch (IOException e) {
            Log.e(TAG, "boot.log append failed: " + e);
        }
        Log.i(TAG, marker);
    }

    /** Package a report file into diag/pending/ (uploaded on this or next launch). */
    public static synchronized void packageReport(String kind, String content) {
        if (sApp == null) return;
        String ts = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
        File f = new File(pendingDir(sApp), kind + "-" + ts + ".md");
        try (FileWriter w = new FileWriter(f, false)) {
            w.write("# " + kind + " — " + BuildConfig.VERSION_NAME
                    + " (build " + BuildConfig.VERSION_CODE + ")\n\n");
            w.write(deviceBlock());
            if (content != null && !content.isEmpty()) {
                w.write("\n## log\n\n```\n");
                w.write(content);
                w.write("\n```\n");
            }
            w.close();
        } catch (IOException e) {
            Log.e(TAG, "packageReport failed: " + e);
            return;
        }
        uploaderLog("packaged " + f.getName());
    }

    /** Called by native code (JNI) and by the 2-finger gesture trigger. */
    public static void requestUpload() {
        if (sApp == null) return;
        final Context c = sApp;
        new Thread(() -> DiagUploader.uploadPending(c), "diag-upload-now").start();
    }

    /** Background uploader: FIRST ships anything already pending (reports from
     *  previous crashed boots — must not wait on this launch's progress, see the
     *  phase-1 immediate-close incident), THEN waits for a complete boot, writes
     *  the one-per-version boot-ok report, and uploads it. */
    public static synchronized void startUploader(Context c) {
        if (sUploaderThread != null && sUploaderThread.isAlive()) return;
        sUploaderThread = new Thread(() -> {
            final Context app = c.getApplicationContext();
            // 1) Upload pending reports from previous crashed boots right now.
            //    In the immediate-close failure mode every launch dies early;
            //    waiting for the first frame would starve the uploader forever.
            DiagUploader.uploadPending(app);
            // 2) Wait for native to remove the boot flag (= first frame drawn) — max 120 s.
            for (int i = 0; i < 240; i++) {
                if (!bootFlag(app).exists()) break;
                try { Thread.sleep(500); } catch (InterruptedException e) { return; }
            }
            boolean firstFrame = false;
            String boot = readFileText(bootLog(app));
            if (boot != null) firstFrame = boot.contains("first_frame_drawn");
            if (firstFrame) {
                File sentMarker = new File(dir(app), "boot-ok-" + BuildConfig.VERSION_CODE + ".sent");
                if (!sentMarker.exists()) {
                    packageReport("boot-ok", boot);
                    try { new FileWriter(sentMarker, false).close(); } catch (IOException ignored) {}
                }
            }
            DiagUploader.uploadPending(app);
        }, "diag-uploader");
        sUploaderThread.start();
    }

    public static String deviceBlock() {
        String density = "n/a";
        try {
            density = String.valueOf(sApp.getResources().getDisplayMetrics().densityDpi);
        } catch (Exception ignored) {}
        String abi = "unknown";
        try {
            abi = Build.SUPPORTED_ABIS != null && Build.SUPPORTED_ABIS.length > 0
                    ? Build.SUPPORTED_ABIS[0] : "unknown";
        } catch (Exception ignored) {}
        return "device: " + Build.MANUFACTURER + " " + Build.MODEL
                + "\nandroid: " + Build.VERSION.RELEASE + " (SDK " + Build.VERSION.SDK_INT + ")"
                + "\nabi: " + abi
                + "\nscreen dpi: " + density
                + "\napp: " + BuildConfig.VERSION_NAME
                + " (build " + BuildConfig.VERSION_CODE + ")"
                + "\nutc: " + new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date())
                + "\n";
    }

    public static void uploaderLog(String line) {
        if (sApp == null) return;
        try (FileWriter w = new FileWriter(new File(dir(sApp), "upload.log"), true)) {
            w.write(System.currentTimeMillis() + " " + line + "\n");
        } catch (IOException ignored) {}
        Log.i(TAG, line);
    }

    public static String readFileText(File f) {
        if (f == null || !f.exists()) return "";
        try (InputStream in = new FileInputStream(f)) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
    }

    private static void installJavaCrashHandler() {
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            StringBuilder sb = new StringBuilder();
            sb.append("thread: ").append(t.getName()).append('\n');
            sb.append("exception: ").append(e.getClass().getName())
              .append(": ").append(e.getMessage()).append('\n');
            for (StackTraceElement el : e.getStackTrace()) sb.append("  at ").append(el).append('\n');
            Throwable cause = e.getCause();
            int depth = 0;
            while (cause != null && depth++ < 8) {
                sb.append("caused by: ").append(cause.getClass().getName())
                  .append(": ").append(cause.getMessage()).append('\n');
                for (StackTraceElement el : cause.getStackTrace()) sb.append("  at ").append(el).append('\n');
                cause = cause.getCause();
            }
            packageReport("java-crash", sb.toString());
            if (prev != null) prev.uncaughtException(t, e);
        });
    }
}
