#ifndef SIMUTRANS_ANDROID_DIAG_H
#define SIMUTRANS_ANDROID_DIAG_H

// Diagnostics core (native side), spec §5.
// Markers + crash files live in the SAME diag/ dir the Java side uses:
//   <internal files dir>/diag/{boot.log, boot-in-progress, pending/}
namespace diag {

// Call first thing inside SDL_main (SDL JNI layer is up at that point).
void boot_init();

// Append "<unix_ms> <marker>\n" to diag/boot.log + logcat.
void log(const char* marker);

// Marker "first_frame_drawn" + delete diag/boot-in-progress (boot succeeded).
void boot_first_frame();

// Trigger Java-side upload now (2-finger gesture). Best effort.
void request_upload();

// Block until the Java side finished the first-run asset unpack
// (base data + pak128), or a ~5 minute timeout. Call from SDL_main
// BEFORE the game touches any file.
void wait_for_assets();

// Current on-screen upload status from Java ("ENVIO: ..."). Never null;
// returns a font-safe ASCII string. Poll at most ~1/s.
const char* upload_status();

} // namespace diag

#endif // SIMUTRANS_ANDROID_DIAG_H
