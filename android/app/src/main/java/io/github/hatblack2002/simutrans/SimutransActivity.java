package io.github.hatblack2002.simutrans;

import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.io.StringWriter;
import java.io.PrintWriter;

/**
 * Launcher activity (manifest: android:name=".SimutransActivity").
 *
 * Phase-1 incident note ("the app closes immediately"): the first APKs shipped
 * with this class missing — Android could not instantiate the launchable
 * activity. CI now guards against that class of bug (verify step asserts the
 * launchable activity exists in the dex).
 *
 * Phase 2: real game. On top of SDLActivity this adds
 *  - first-run asset installation (base data + pak128) on a background thread
 *    with an on-screen progress overlay in Spanish; the native side waits on
 *    {@link #assetsReady()} before touching any file (diag::wait_for_assets);
 *  - the game's command line via {@link #getArguments()}.
 */
public class SimutransActivity extends SDLActivity {

    /** Set by the installer thread; polled by native diag::wait_for_assets(). */
    private static volatile boolean sAssetsReady = false;

    private TextView mOverlay;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // BEFORE super: install the Java crash handler and package a
        // boot-failure report if the previous launch died before its first
        // frame. Nothing of ours may run before this line.
        Diag.init(this);
        super.onCreate(savedInstanceState);
        // Ship reports pending from previous crashed boots IMMEDIATELY.
        // Never wait for the first frame: an immediate-close failure mode
        // kills the process early, and the previous run's report must
        // still get out during this launch's first seconds.
        Diag.startUploader(this);

        // First-run / upgrade asset install (base data + pak128), with a
        // progress overlay. Runs on a background thread; the native main()
        // blocks in diag::wait_for_assets() until we are done.
        sAssetsReady = false;
        showOverlay();
        final SimutransActivity self = this;
        new Thread(() -> {
            try {
                AssetInstaller.install(this, self::setOverlayProgress);
                sAssetsReady = true;
                runOnUiThread(() -> hideOverlay());
            } catch (Exception e) {
                // Package a report; the overlay stays with the error so the
                // person sees something happened. Native proceeds after its
                // timeout, fails to find the pakset, and reports too.
                java.io.StringWriter sw = new StringWriter();
                e.printStackTrace(new PrintWriter(sw));
                Diag.packageReport("assets-failure",
                        "error: " + e + "\n" + sw);
                runOnUiThread(() -> setOverlayText(
                        "ERROR AL PREPARAR EL JUEGO\n\nReinicia la app.\n(" + e + ")"));
            }
        }, "asset-install").start();
    }

    /** Game arguments (SDLActivity passes these to SDL_main). */
    @Override
    protected String[] getArguments() {
        return new String[]{
                "-autodpi",       // UI scaling for the 563dpi screen
                "-fullscreen",
                "-log",
                "-debug", "2",
                "-objects", "pak128/",  // bundled pakset
        };
    }

    /**
     * Same as the official app: the game's C++ side keeps global state, so an
     * activity restart inside the same process would re-enter a dirty simutrans.
     * Exit the process; Android restarts it cleanly next time.
     */
    @Override
    protected void onDestroy() {
        super.onDestroy();
        System.exit(0);
    }

    /** Polled from native (diag::wait_for_assets) once per second. */
    public static boolean assetsReady() {
        return sAssetsReady;
    }

    // --- overlay -----------------------------------------------------------

    private void showOverlay() {
        mOverlay = new TextView(this);
        mOverlay.setText("PREPARANDO SIMUTRANS...\n(la primera vez tarda unos minutos)");
        mOverlay.setTextSize(22);
        mOverlay.setGravity(Gravity.CENTER);
        mOverlay.setBackgroundColor(0xFF101820);
        mOverlay.setTextColor(0xFFF0F0F0);
        mOverlay.setPadding(48, 48, 48, 48);
        addContentView(mOverlay, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private void hideOverlay() {
        if (mOverlay != null) mOverlay.setVisibility(View.GONE);
    }

    private void setOverlayText(final String text) {
        final TextView v = mOverlay;
        if (v == null) return;
        v.setText(text);
    }

    /** Called from the installer thread (throttled). */
    private void setOverlayProgress(final String text) {
        final TextView v = mOverlay;
        if (v == null) return;
        runOnUiThread(() -> {
            if (v.isShown()) v.setText(text);
        });
    }
}
