package io.github.hatblack2002.simutrans;

import android.content.Context;

import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Uploads diag/pending/*.md reports as GitHub Issues in the fork repo
 * (POST /repos/<DIAG_REPO>/issues, label "device-log").
 * - Body capped at 60,000 chars (truncate middle, never the crash trace head/tail).
 * - HTTP 201 -> delete the local file (it was delivered).
 * - Any other status -> keep the file, retry on next launch. Max 5 per run.
 * - Empty DIAG_TOKEN -> uploader disabled (files still kept).
 */
public final class DiagUploader {

    private static final int MAX_BODY = 60000;
    private static final int MAX_PER_RUN = 5;

    private DiagUploader() {}

    public static synchronized int uploadPending(Context c) {
        String token = BuildConfig.DIAG_TOKEN;
        if (token == null || token.isEmpty()) {
            Diag.uploaderLog("upload skipped: no DIAG_TOKEN (build-time secret missing)");
            Diag.setUploadStatus("ENVIO: SIN TOKEN");
            return 0;
        }
        File pending = Diag.pendingDir(c);
        File[] files = pending.listFiles((dir, name) -> name.endsWith(".md"));
        if (files == null || files.length == 0) {
            Diag.uploaderLog("nothing pending");
            // LISTO = nothing to send; sticky once something was delivered this boot.
            if (!everUploaded) Diag.setUploadStatus("ENVIO: LISTO");
            return 0;
        }
        Arrays.sort(files);
        int uploaded = 0;
        int tried = 0;
        int realTotal = 0;
        String lastErr = null;
        for (File f : files) {
            if (tried++ >= MAX_PER_RUN) break;
            String kind = f.getName().replaceFirst("\\.md$", "");
            String body = Diag.readFileText(f);
            if (body.isEmpty()) { // junk file — remove
                f.delete();
                continue;
            }
            realTotal++;
            body = cap(body);
            String title = "[diag] " + kind
                    + " (build " + BuildConfig.VERSION_CODE + ", "
                    + android.os.Build.MODEL + ")";
            int status = postIssue(token, title, body);
            if (status == 201) {
                f.delete();
                uploaded++;
                Diag.uploaderLog("uploaded " + f.getName() + " -> HTTP 201");
            } else {
                lastErr = (status == -1) ? "RED" : ("HTTP " + status);
                Diag.uploaderLog("upload FAILED " + f.getName() + " -> HTTP " + status);
            }
        }
        Diag.uploaderLog("upload run done: " + uploaded + " uploaded, "
                + Math.max(0, files.length - uploaded) + " kept");
        if (uploaded > 0) everUploaded = true;
        if (realTotal == 0) {
            if (!everUploaded) Diag.setUploadStatus("ENVIO: LISTO");
        } else if (uploaded == realTotal) {
            Diag.setUploadStatus("ENVIO: OK " + uploaded + "/" + realTotal);
        } else {
            Diag.setUploadStatus("ENVIO: FALLO " + uploaded + "/" + realTotal
                    + (lastErr != null ? " " + lastErr : ""));
        }
        return uploaded;
    }

    private static volatile boolean everUploaded = false;

    /** Keep head + tail; truncate the middle. Crash traces are at the end of files. */
    private static String cap(String s) {
        if (s.length() <= MAX_BODY) return s;
        int head = 8000;
        int tail = MAX_BODY - head - 64;
        return s.substring(0, head)
                + "\n\n...[TRUNCATED " + (s.length() - head - tail) + " chars]...\n\n"
                + s.substring(s.length() - tail);
    }

    private static int postIssue(String token, String title, String body) {
        HttpURLConnection conn = null;
        try {
            URL url = new URL("https://api.github.com/repos/" + BuildConfig.DIAG_REPO + "/issues");
            conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(15000);
            conn.setReadTimeout(30000);
            conn.setRequestMethod("POST");
            conn.setDoOutput(true);
            conn.setRequestProperty("Authorization", "token " + token);
            conn.setRequestProperty("Accept", "application/vnd.github+json");
            conn.setRequestProperty("User-Agent", "simutrans-android-diag");
            JSONObject json = new JSONObject();
            json.put("title", title);
            json.put("body", body);
            json.put("labels", new org.json.JSONArray().put("device-log"));
            byte[] payload = json.toString().getBytes(StandardCharsets.UTF_8);
            conn.setFixedLengthStreamingMode(payload.length);
            try (OutputStream out = conn.getOutputStream()) {
                out.write(payload);
            }
            return conn.getResponseCode();
        } catch (IOException | org.json.JSONException e) {
            Diag.uploaderLog("upload exception: " + e);
            return -1;
        } finally {
            if (conn != null) conn.disconnect();
        }
    }
}
