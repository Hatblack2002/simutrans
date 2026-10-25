package io.github.hatblack2002.simutrans;

import android.os.Bundle;

import org.libsdl.app.SDLActivity;

/**
 * Launcher activity (manifest: android:name=".SimutransActivity").
 *
 * Phase-1 incident ("the app closes immediately"): the first APKs shipped with
 * this class missing — Android could not instantiate the launchable activity,
 * so the app died the instant it was opened, and no diagnostic could fire
 * because the failure happens before any of our code runs. CI now guards
 * against this class of bug (verify step asserts the launchable activity
 * exists in the dex).
 */
public class SimutransActivity extends SDLActivity {

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
    }
}
