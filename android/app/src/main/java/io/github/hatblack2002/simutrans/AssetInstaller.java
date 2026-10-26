package io.github.hatblack2002.simutrans;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * First-run / upgrade asset installer.
 *
 * APK assets -> internal files dir (what the game uses as its base dir via
 * SDL_AndroidGetInternalStoragePath()):
 *
 *   assets/simutrans/**  ->  files/**            (config, font, text, themes, script, ai, music)
 *   assets/pak128.zip    ->  files/pak128/**     (the bundled pakset, ~400 MB unpacked)
 *
 * Marker files (dot-prefixed, in the files dir) record what is installed:
 *   .assets-base-<versionCode>        base data, reinstalled on every app upgrade
 *   .assets-pak128-<fingerprint>      pak128, reinstalled only when the bundled zip changes
 *
 * The pak zip layout is normalized: an optional leading "simutrans/" wrapper
 * is stripped, and entries not already under pak128/ go there (some pakset
 * zips wrap everything in simutrans/, some have the pak dir at the root).
 */
final class AssetInstaller {

    private static final int BUF = 64 * 1024;

    private AssetInstaller() {}

    interface Progress {
        void onProgress(String text);
    }

    static void install(Context c, Progress p) throws IOException {
        File files = c.getFilesDir();
        if (files == null) throw new IOException("no internal storage");

        installBase(c, files, p);
        installPak128(c, files, p);
    }

    // --- base data ------------------------------------------------------------

    private static void installBase(Context c, File files, Progress p) throws IOException {
        File marker = new File(files, ".assets-base-" + BuildConfig.VERSION_CODE);
        if (marker.exists()) return;  // this version's base data already installed

        p.onProgress("PREPARANDO SIMUTRANS...\n(la primera vez tarda unos minutos)\n\narchivos base...");
        copyAssetTree(c.getAssets(), "simutrans", files);

        touch(marker);
    }

    private static void copyAssetTree(AssetManager am, String path, File dstRoot) throws IOException {
        String[] children = am.list(path);
        if (children == null || children.length == 0) {
            // path is a file -> copy it (strip the leading "simutrans/")
            String rel = path.startsWith("simutrans/") ? path.substring("simutrans/".length()) : path;
            if (rel.isEmpty() || rel.endsWith("/")) return;
            File out = new File(dstRoot, rel);
            File parent = out.getParentFile();
            if (parent != null) parent.mkdirs();
            try (InputStream in = am.open(path); OutputStream os = new FileOutputStream(out)) {
                copy(in, os);
            }
            return;
        }
        for (String ch : children) {
            copyAssetTree(am, path + "/" + ch, dstRoot);
        }
    }

    // --- pak128 ----------------------------------------------------------------

    private static void installPak128(Context c, File files, Progress p) throws IOException {
        String fingerprint = BuildConfig.VERSION_CODE + "-" + pakZipSize(c);
        File marker = new File(files, ".assets-pak128-" + fingerprint);
        if (marker.exists()) return;  // same bundled zip already installed

        p.onProgress("PREPARANDO SIMUTRANS...\n\ninstalando pak128 (solo la primera vez)...");

        // wipe any previous install of the pakset
        deleteRecursive(new File(files, "pak128"));

        long total = pakZipSize(c);
        long[] done = {0};
        int[] lastPct = {-1};

        try (ZipInputStream zis = new ZipInputStream(c.getAssets().open("pak128.zip"))) {
            ZipEntry e;
            byte[] buf = new byte[BUF];
            while ((e = zis.getNextEntry()) != null) {
                String name = e.getName();
                if (name.endsWith("/")) continue;  // directory entry
                // normalize: strip optional "simutrans/" wrapper, force under pak128/
                if (name.startsWith("simutrans/")) name = name.substring("simutrans/".length());
                if (!name.startsWith("pak128/")) name = "pak128/" + name;

                File out = new File(files, name);
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                try (OutputStream os = new FileOutputStream(out)) {
                    int n;
                    while ((n = zis.read(buf)) > 0) {
                        os.write(buf, 0, n);
                        done[0] += n;
                    }
                }

                if (total > 0) {
                    int pct = (int) (done[0] * 100 / total);
                    if (pct != lastPct[0] && pct % 5 == 0) {
                        lastPct[0] = pct;
                        p.onProgress(String.format(java.util.Locale.US,
                                "PREPARANDO SIMUTRANS...\n\ninstalando pak128: %d%%", pct));
                    }
                }
            }
        }

        // sanity: the pak dir must exist and have content
        File pakDir = new File(files, "pak128");
        String[] listing = pakDir.list();
        if (listing == null || listing.length == 0) {
            throw new IOException("pak128.zip extracted nothing");
        }

        touch(marker);
    }

    /** Size of the bundled pak zip, or -1 if unknown (compressed asset). */
    private static long pakZipSize(Context c) {
        try (AssetFileDescriptor afd = c.getAssets().openFd("pak128.zip")) {
            return afd.getLength();
        } catch (IOException ex) {
            return -1;
        }
    }

    // --- helpers ---------------------------------------------------------------

    private static void copy(InputStream in, OutputStream os) throws IOException {
        byte[] buf = new byte[BUF];
        int n;
        while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
    }

    private static void touch(File f) throws IOException {
        try (OutputStream os = new FileOutputStream(f)) {
            os.write('o');
        }
    }

    private static void deleteRecursive(File f) {
        if (f == null || !f.exists()) return;
        File[] children = f.listFiles();
        if (children != null) {
            for (File ch : children) deleteRecursive(ch);
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}
