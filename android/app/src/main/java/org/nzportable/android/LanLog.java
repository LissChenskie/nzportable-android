package org.nzportable.android;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Intent;
import android.os.Build;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.RandomAccessFile;
import java.io.StringWriter;
import java.util.ArrayDeque;
import java.util.List;

/**
 * Crash/LAN log for players without a PC. Native code appends the NZP_LAN log and native crashes to
 * nzp_lan_log.txt; Java crashes are appended here. If the previous run crashed, the next launch offers
 * the report through the system share sheet (Telegram, mail, notes...).
 */
final class LanLog {
    private static final String TAG = "NZP_LAN";
    private static final int MAX_FILE = 512 * 1024, REPORT_TAIL = 40 * 1024;

    static native void nativeInit(String logPath, String markerPath);

    private LanLog() { }

    static void init(final Activity activity) {
        final File dir = activity.getExternalFilesDir(null) != null ? activity.getExternalFilesDir(null) : activity.getFilesDir();
        final File log = new File(dir, "nzp_lan_log.txt"), marker = new File(dir, "nzp_crash_pending");
        try {
            if (log.length() > MAX_FILE) { File old = new File(dir, "nzp_lan_log.old.txt"); old.delete(); log.renameTo(old); }
            final String exitInfo = previousExit(activity, new File(dir, "nzp_last_exit_ts"));
            final boolean crashed = marker.exists() || exitInfo != null;
            // Previous-run state is captured before this run writes anything new.
            final String previous = crashed ? (exitInfo != null ? "SYSTEM EXIT REASON: " + exitInfo + "\n\n" : "")
                + tail(log, REPORT_TAIL) : null;
            appendJava(log, "==== app start " + new java.util.Date() + " | " + Build.MANUFACTURER + " " + Build.MODEL
                + " | Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ") ====");
            try { nativeInit(log.getAbsolutePath(), marker.getAbsolutePath()); }
            catch (UnsatisfiedLinkError e) { Log.w(TAG, "crash capture unavailable: " + e.getMessage()); }
            final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
            Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
                try {
                    StringWriter sw = new StringWriter();
                    e.printStackTrace(new PrintWriter(sw));
                    appendJava(log, "=== JAVA CRASH in thread " + t.getName() + " ===\n" + sw + "=== END JAVA CRASH ===");
                    new FileOutputStream(marker).close();
                } catch (Throwable ignored) { }
                if (prev != null) prev.uncaughtException(t, e);
            });
            if (crashed) {
                marker.delete();
                new Thread(() -> offerReport(activity, previous)).start();
            }
        } catch (Throwable t) { Log.w(TAG, "LanLog init failed: " + t); }
    }

    /** Why Android ended the previous process (API 30+). Null when it exited normally or was already reported. */
    private static String previousExit(Activity activity, File stamp) {
        if (Build.VERSION.SDK_INT < 30) return null;
        try {
            ActivityManager am = (ActivityManager) activity.getSystemService(Activity.ACTIVITY_SERVICE);
            List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(null, 0, 1);
            if (list == null || list.isEmpty()) return null;
            ApplicationExitInfo info = list.get(0);
            long seen = 0;
            try { seen = Long.parseLong(tail(stamp, 64).trim()); } catch (NumberFormatException ignored) { }
            if (info.getTimestamp() <= seen) return null;
            try (FileOutputStream out = new FileOutputStream(stamp)) { out.write(Long.toString(info.getTimestamp()).getBytes()); }
            int r = info.getReason();
            boolean abnormal = r == ApplicationExitInfo.REASON_CRASH || r == ApplicationExitInfo.REASON_CRASH_NATIVE
                || r == ApplicationExitInfo.REASON_ANR || r == ApplicationExitInfo.REASON_LOW_MEMORY
                || r == ApplicationExitInfo.REASON_SIGNALED || r == ApplicationExitInfo.REASON_INITIALIZATION_FAILURE
                || r == ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE || r == ApplicationExitInfo.REASON_OTHER;
            if (!abnormal) return null;
            return "reason=" + r + " " + reasonName(r) + " status=" + info.getStatus() + " importance=" + info.getImportance()
                + " pss_kb=" + info.getPss() + " rss_kb=" + info.getRss() + " desc=" + info.getDescription();
        } catch (Throwable t) { return null; }
    }

    private static String reasonName(int r) {
        switch (r) {
            case ApplicationExitInfo.REASON_CRASH: return "JAVA_CRASH";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "NATIVE_CRASH";
            case ApplicationExitInfo.REASON_ANR: return "ANR";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "LOW_MEMORY_KILL";
            case ApplicationExitInfo.REASON_SIGNALED: return "SIGNALED(killed)";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "INIT_FAILURE";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "EXCESSIVE_RESOURCES";
            default: return "OTHER";
        }
    }

    private static void offerReport(final Activity activity, String fileTail) {
        final StringBuilder report = new StringBuilder("NZPortable crash report (previous run)\n");
        report.append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(", Android ")
              .append(Build.VERSION.RELEASE).append("\n\n---- app log (tail) ----\n").append(fileTail == null ? "" : fileTail);
        // Best effort: own-app logcat of the previous process (tags NZP_LAN, SDL, crash lines).
        try {
            Process p = Runtime.getRuntime().exec(new String[]{"logcat", "-d", "-v", "threadtime"});
            ArrayDeque<String> lines = new ArrayDeque<>();
            try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
                String l;
                while ((l = r.readLine()) != null) {
                    if (l.contains("NZP_LAN") || l.contains("SDL") || l.contains("AndroidRuntime") || l.contains("Fatal")
                            || l.contains("libc") || l.contains("NZP")) {
                        lines.addLast(l);
                        if (lines.size() > 150) lines.removeFirst();
                    }
                }
            }
            report.append("\n---- logcat (own app, last lines) ----\n");
            for (String l : lines) report.append(l).append('\n');
        } catch (Throwable ignored) { }
        String text = report.length() > 90 * 1024 ? report.substring(report.length() - 90 * 1024) : report.toString();
        final String payload = text;
        activity.runOnUiThread(() -> {
            try {
                Intent send = new Intent(Intent.ACTION_SEND).setType("text/plain")
                    .putExtra(Intent.EXTRA_SUBJECT, "NZPortable crash report").putExtra(Intent.EXTRA_TEXT, payload);
                activity.startActivity(Intent.createChooser(send, "The game crashed last time - send the report"));
            } catch (Throwable t) { Log.w(TAG, "cannot offer crash report: " + t); }
        });
    }

    private static synchronized void appendJava(File f, String text) {
        try (FileOutputStream out = new FileOutputStream(f, true)) { out.write((text + "\n").getBytes("UTF-8")); }
        catch (Throwable ignored) { }
    }

    private static String tail(File f, int max) {
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            long len = r.length(), start = Math.max(0, len - max);
            byte[] b = new byte[(int) (len - start)];
            r.seek(start); r.readFully(b);
            return new String(b, "UTF-8");
        } catch (Throwable t) { return ""; }
    }
}
