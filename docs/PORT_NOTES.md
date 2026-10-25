# Simutrans Android Port — Working Notes

This file is the running log the port spec (§2 rule 10) requires.
Language for all code/docs: English. Messages to the person: Spanish, max 6 lines.

Target: Samsung Galaxy S20 (arm64-v8a, 1440x3200, ~563dpi), pak128, SDL2 software-rendering path,
built entirely on GitHub Actions, APK delivered via GitHub Releases. No GPU-API code of our own.

---

## 0. Phase 0 recon — headline discovery

**The upstream `master` branch of simutrans/simutrans ALREADY contains Android support, and there
is a separate official Android project repo (`simutrans/simutrans-android-project`) that builds a
working APK with Gradle + CMake + NDK.** Our port is therefore NOT a from-scratch port. It is:
adapt the official Android project → pak128 + arm64-only + CI + fixed signing + diagnostics +
person-facing release flow.

Upstream repo used: https://github.com/simutrans/simutrans, default branch `master`,
base commit for this port: `460ffc2` ("Less rescaling each frame..."), version 125.0.2 Nightly
(src/simutrans/simversion.h).

### 0.1 What exists in the main repo (verified by reading files)

- `src/android/` — Android JNI glue:
  - `android.cc`: `Java_com_simutrans_Simutrans_getVersion` (native version for the asset-unpack
    check), `download_file(url, filename)` calling static Java `com.simutrans.Simutrans.downloadFile`
    via JNI (used by in-game pakset downloader when `USE_OWN_PAKINSTALL`).
  - `AndroidPreBuild.sh` (fetch languages, paksets, soundfont — pak64/pak64.japan oriented),
    `AndroidBuild.sh` (legacy ndk-build flow), `unpak.sh`, `signBundle.sh`, keystore placeholder.
- Root `CMakeLists.txt` lines 74–81: `if(ANDROID) add_library(simutrans SHARED)`, links
  `android` + `log`, compiles `src/android/android.cc`, adds `-Wl,-z,max-page-size=16384`.
  Line 222–223: on Android the sdl2 backend links plain `SDL2 SDL2main` targets.
  Line 506–509: makeobj/nettool are NOT built on Android.
- `cmake/SimutransFindDependencies.cmake` — ANDROID branch expects prebuilt shared libs in
  sibling dirs of the source root: `../zlib`, `../bzip2`, `../libpng`, `../zstd`, `../freetype`
  (as `prebuilt/<debug|release>/<abi>/*.so`), plus the FluidSynth prebuilt family and
  `find_package(OpenMP REQUIRED)` (needed only when FluidSynth is enabled).
  IMPORTANT recent fix (documented in the file itself): `find_package(Threads)` now runs for
  Android too, so `SIMUTRANS_MULTI_THREAD` works (verified by upstream against NDK r27c;
  pthread barriers available at API 24+).
- 11 files contain `__ANDROID__` blocks: `simmain.cc`, `sys/simsys.cc`, `sys/simsys_s2.cc`,
  `dataobj/environment.cc`, `gui/simwin.cc`, `utils/log.cc`, `sys/clipboard_s2.cc`,
  `sound/sdl2_sound.cc`, `dataobj/pakset_downloader.cc`, `music/fluidsynth.cc`.

### 0.2 The official Android project repo (verified by cloning)

https://github.com/simutrans/simutrans-android-project — Gradle project, module `simutrans`:

- Gradle wrapper 8.13, AGP 8.13.0, compileSdk 36, minSdk 24, targetSdk 36, Java 21.
- Toolchain pins from `tools/android-helper/setup-android.sh` (in the main repo):
  NDK **29.0.14206865**, CMake **3.31.6** (SDK cmake), build-tools **35.0.0**, cmdline-tools 13114758.
- `jni/` contains submodules: SDL (libsdl-org/SDL, branch SDL2, pinned
  `6ad5a966ea95918a4534ce22a7b31387db70675a` — a Nov 25 2025 SDL2-branch commit), bzip2 `66c46b8c…`,
  libpng `32fc0af2…`, zlib `51b7f2ab…`, zstd `f8745da6…`, freetype `cf53e54c…`.
  Their setup additionally does `git revert 53dbe18 --no-commit` inside SDL (restores the
  16k-page linker flag). We will pass the flag from our own CMake instead.
- `jni/build_libraries.sh`: builds zlib/bzip2/libpng/zstd/freetype as shared libs per ABI
  (android-24, `c++_static` for those C libs) with output to `<lib>/prebuilt/<type>/<abi>/`.
  SDL is NOT prebuilt — it is a CMake subproject of the app build.
- `jni/CMakeLists.txt` (entire file): `add_subdirectory(SDL)` + `add_subdirectory(simutrans)`.
- Module `build.gradle` CMake args: `-DANDROID_APP_PLATFORM=android-24 -DANDROID_STL=c++_shared
  -DSIMUTRANS_BACKEND=sdl2 -DSIMUTRANS_USE_ZSTD=ON -DSIMUTRANS_USE_FLUIDSYNTH_MIDI=ON
  -DSIMUTRANS_MULTI_THREAD=OFF -DSIMUTRANS_USE_OWN_PAKINSTALL=ON`; 4 ABIs.
- Java `com.simutrans.Simutrans extends SDLActivity`: loads libs `SDL2`, `simutrans`;
  `getArguments()` = `{ "-autodpi", "-fullscreen", "-log", "-debug 3" }`; unpacks ALL assets
  recursively to `getFilesDir()` when `config/version` != native version; `onDestroy` →
  `System.exit(0)`; static `downloadFile(url, filename)` for the pakset downloader.
- Their CI draft exists but is DISABLED: `.github/workflows/android.ym_` (underscore suffix).
  It used NDK 25/Java 17 (older than the current setup scripts) and cloned simutrans from a
  developer fork branch `android-cmake` (that work is now merged into master — see 0.1).
- Signing there = env vars `SIGNING_KEYSTORE` etc. (a locally generated debug keystore).
  No fixed committed keystore, no diagnostics, pak64/pak64.japan only (no pak128).

### 0.3 SDL2 backend facts (src/simutrans/sys/simsys_s2.cc — read in full where relevant)

- Entry chain: `int main()` (end of simsys_s2.cc, ~line 1275) → `sysmain()` (simsys.cc:1601) →
  `simu_main()` (simmain.cc:556). On Android `#include <SDL.h>` (lines 6–10) pulls in
  `SDL_main.h`, which renames `main` to `SDL_main` — exported from the shared lib; SDLActivity
  finds it in any loaded library. **No source change needed for the entry point.**
- Window (`dr_os_open`, lines 455–506): `SDL_CreateWindow(... SDL_WINDOW_FULLSCREEN_DESKTOP |
  SDL_WINDOW_ALLOW_HIGHDPI [+ SDL_WINDOW_RESIZABLE on __ANDROID__])`.
- Renderer (`internal_create_surfaces`, lines 376–451): `SDL_CreateRenderer(ACCELERATED
  [±PRESENTVSYNC])` with **fallback to `SDL_RENDERER_SOFTWARE`** if accelerated fails;
  texture = `SDL_PIXELFORMAT_RGB565`, `SDL_TEXTUREACCESS_STREAMING`, width rounded up to
  multiple of 16 (pitch alignment). `simgraph16.cc` (COLOUR_DEPTH=16) is the software renderer
  and draws into an RGB565 surface; `dr_flush` = `SDL_UpdateTexture` + `RenderCopy` +
  `RenderPresent`; dirty-rect updates via `dr_textur`. **We add no renderer code.**
  NOTE: SDL2's "accelerated" renderer may use GLES internally — that is SDL's own business,
  exactly as shipped by upstream and the official Android project. We write no GL code.
  If it misbehaves on the S20 (Adreno), there is the software fallback and/or we pass flags
  to force it; boot markers will tell.
- **Touch gestures are ALREADY IMPLEMENTED upstream** (this de-risks Phase 4 massively):
  - `SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0")` (line 496) — synthesis disabled.
  - FINGER events enabled explicitly (lines 325–327); mouse events synthesized by touch are
    filtered via `SDL_TOUCH_MOUSEID` (lines 784, 801).
  - Tap (down+up without movement) → `SIM_MOUSE_LEFTBUTTON` + queued `SIM_MOUSE_LEFTUP`
    (lines 906–936). Tap position = where the finger LANDED (anchor), not where it drifted.
  - Single-finger drag → `SIM_MOUSE_LEFTBUTTON` + `SIM_MOUSE_MOVED` (lines 864–904) — this is
    Simutrans' left-drag = map drag / window drag on title bars.
  - Long press → `SIM_MOUSE_LONGPRESS` (LONGPRESS_MS=500, SLOP=16 px, repeats every 250 ms up
    to 1.5 s — lines 47–65, 655–683). Consumed as right-click/info by the GUI.
  - Two-finger pinch → `SIM_MOUSE_WHEELUP/WHEELDOWN` (Δdistance threshold 0.033 — lines
    951–978) = zoom.
  - Three-finger drag → right-button drag (map scroll — lines 979–996).
  - Soft keyboard: `SDL_HasScreenKeyboardSupport()`, `env_t::hide_keyboard=true` default on
    Android, `SDL_StartTextInput` managed in `dr_os_init` (lines 338–357).
- Android BACK button: `SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1")` is COMMENTED OUT
  (line 333) → today the system handles BACK (activity finishes). **Phase 4 TODO**: enable the
  trap and map `SDL_AC_BACK` to open/close the in-game menu instead of killing the app.
- Termination: `my_event_filter` on `SDL_APP_TERMINATING` saves the world + settings.xml then
  exits (lines 280–298) — a base for §Phase-5 autosave behavior.
- DPI/UI scaling (the option the spec told us to find): `-autodpi` (simmain.cc:905 →
  `dr_set_screen_scale(-1)`, simsys_s2.cc:176+) and `-screen_scale N` (percent). Screen↔texture
  coordinate macros (`SCREEN_TO_TEX_*`, `TEX_TO_SCREEN_*`) everywhere. Android defaults
  (environment.cc): `menupos = MENU_BOTTOM`, `single_toolbar_mode = true`, `fontsize = 17`.

### 0.4 Paths, data, fonts, language (verified)

- On Android: base_dir = `SDL_AndroidGetInternalStoragePath()` (= `getFilesDir()`), checked for
  `config/simuconf.tab` (simsys.cc:511–512). User dir = `SDL_AndroidGetExternalStoragePath()`
  if writable, else `SDL_GetPrefPath(...)` (simsys.cc:612–626). Install/pakset dir = external
  storage path (simsys.cc:677–678). Explicit overrides exist too: `-set_basedir`,
  `-set_installdir`, `-set_userdir`, `-singleuser` (simmain.cc help text, lines 428–431).
- Pakset choice: `-objects DIR/` (simmain.cc:746) → `set_pakdir()` (simmain.cc:512) resolves
  the dir relative to base_dir, then install_dir, then user_dir (`dr_chdir(env_t::base_dir)`
  first — CWD does not matter). Default comes from `pak_file_path` in simuconf.tab
  (`simutrans/config/simuconf.tab` has commented examples incl. `pak128/`).
- Base data folder = repo root `simutrans/` (12 MB): `config/` (simuconf.tab), `font/`
  (Prop-Latin1.bdf, Prop-Latin2.bdf, cyr.bdf), `text/` (ALL translations incl. `es.tab`),
  `themes/`, `script/`, `ai/`, `music/`, license files. `tools/get_lang_files.sh` only refreshes
  translations from the translator server — the committed ones are usable as-is.
- Fonts: `dr_get_system_font()` on Android uses `AFontMatcher` (NDK font API, **API 29+**;
  target S20 = Android 10+ = API 29+, so OK; note minSdk 24 ⇒ API 24–28 devices would lack it,
  irrelevant for our single target device). `font_t` loads BDF or FreeType TTF. The official
  AndroidPreBuild even deletes the base `font/` dir and relies on system fonts. We KEEP the
  base font dir (Latin-1 BDF is a safe fallback for Spanish).
- Language: `-lang CODE` exists; Spanish translation present (`simutrans/text/es.tab`).

### 0.5 pak128 (the required pakset)

- Authoritative URL — embedded in the game's own generated `src/paksetinfo.h` (from
  `tools/get_pak.sh`), NOT a random mirror:
  `http://downloads.sourceforge.net/project/simutrans/pak128/pak128%20for%20ST%20125.0%20up%20%282.10.3%29/simupak128-2-1023-for-125-0up.zip`
- Verified live on 2026-10-26: HTTP 302 → SourceForge mirror, HTTP 200,
  Content-Length **97,565,449 bytes ≈ 93 MB**. Unpacked ≈ 408 MB (paksetinfo.h: 418031 KB).
  Version string "pak128 2.10.3 for 125.0 up" — compatible with master 125.0.2 Nightly.
- Decision: bundle as `assets/pak128.zip` (APK ≈ 93 MB zip + 12 MB base + libs ≈ ~110–125 MB,
  under the 250 MB fallback threshold). Unpack on first run. The spec's fallback (Release-asset
  download on first run) stays in reserve.

### 0.6 Dependencies to cross-compile (verified in CMakeLists / SimutransFindDependencies)

Mandatory: **zlib, bzip2, libpng, freetype, SDL2(+SDL2main), Threads**.
We disable: zstd (`SIMUTRANS_USE_ZSTD=OFF`), FluidSynth MIDI (`=OFF` — the official Android
build drags in a 9-library prebuilt family; we skip music initially per spec scope rules),
miniupnpc (default off), SDL3 backend, fontconfig, Steam.
C++ standard: 14. `MULTI_THREAD`: official Android gradle keeps it OFF — we start OFF too.

### 0.7 Toolchain version pins (nothing floating)

| Component | Pin |
|---|---|
| simutrans | master `460ffc2` (fork base) |
| Java | Temurin 21 (CI) |
| Gradle wrapper | 8.13 |
| AGP | 8.13.0 |
| NDK | 29.0.14206865 |
| CMake (SDK) | 3.31.6 |
| build-tools | 35.0.0 |
| compileSdk / targetSdk / minSdk | 36 / 33 / 24 |
| ABI | arm64-v8a only |
| SDL2 | `6ad5a966ea95918a4534ce22a7b31387db70675a` (official project's pin) |
| zlib / bzip2 / libpng / freetype / zstd | official project's submodule commits (§0.2) — vendored at Phase 2 if used |
| pak128 | URL above (verified) |

---

## 1. Our port design (spec layout, adapted)

- Fork of simutrans/simutrans, branch `android-port`. New files only in `android/`,
  `.github/`, `docs/` (plus, if unavoidable, tiny Android-only patches — each one logged here).
- `android/` Gradle project adapted from simutrans-android-project:
  - app module `app` (Java, no Kotlin), `applicationId io.github.<user>.simutrans`
    (prevents any collision with the official `com.simutrans` app),
    `namespace` may stay `com.simutrans`-free: use `io.github.<user>.simutrans.app`.
  - `android/app/src/main/cpp/CMakeLists.txt` builds SDL2 + deps + (Phase 2) simutrans as
    **`libmain.so`** (upstream target is `simutrans` → we set `OUTPUT_NAME main`; zero upstream diff).
  - libs loaded by our Activity: `SDL2`, `main`.
- Args to SDL_main (Phase 2 initial): `-autodpi -fullscreen -log -debug 2 -objects pak128/`.
  (-autodpi = official UI scaling for the 563dpi screen; -fps 30 cap considered for Phase 3.)
- Signing: ONE fixed debug keystore committed at `android/keystore/debug.keystore`
  (storepass/keypass `android`, alias `androiddebugkey`); used for the Debug variant we ship.
  `versionCode = GITHUB_RUN_NUMBER` (env), `versionName = "<SIM_VERSION>+<short-sha>"`.
- CI `.github/workflows/android.yml`: push to `android-port` + `workflow_dispatch`;
  Java 21 → `sdkmanager` installs pinned NDK/CMake if missing → `./gradlew assembleDebug`
  → assert APK contents (native libs present) → upload artifact → update GitHub Release
  (pre-release tag `latest-debug`, stable asset name `simutrans-android-debug.apk`,
  `permissions: contents: write`, pinned action SHAs).
- Diagnostics (built FIRST, spec §5):
  - `diag/boot.log` markers: `app_start`, `assets_ok`, `sdl_init_ok`, `window_ok`,
    `pak_loading`, `pak_loaded`, `menu_shown`, `first_frame_drawn`, `new_game_started`.
    `boot-in-progress` marker deleted after `first_frame_drawn`; its survival on next launch
    means "died before first frame" and is reported.
  - Native crash handler (sigaction on alt stack, SEGV/ABRT/BUS/FPE/ILL, async-signal-safe
    writes, `_Unwind_Backtrace` raw addresses + `dladdr` names, line format
    `FATAL SIGNAL <n> (addr=..., pc=...)`, then re-raise) → `diag/crash-<ts>.log`.
  - Java `Thread.setDefaultUncaughtExceptionHandler` → same dir.
  - stdout/stderr + simu.log routed to `diag/app.log` and to logcat.
  - `DiagUploader` (Java, `HttpsURLConnection`, background thread): on next launch and via a
    "Send diagnostics" button → POST GitHub issue (title `[diag] ... (build N)`,
    label `device-log`, body ≤ 60,000 chars, truncate middle — never the crash trace;
    HTTP status always logged). 201 → delete uploaded files; else keep (max 5).
  - `DIAG_TOKEN` injected from Actions secret into `BuildConfig`; empty token ⇒ uploader
    disabled but files kept (and CI must not fail on missing secret).
- First-run unpack: Java progress screen; unpack base assets + `pak128.zip` to `getFilesDir()`;
  marker `extracted.ok` containing versionName; re-extract on version change.
- Person flow: Release page → download `simutrans-android-debug.apk` → install (upgrade-safe
  thanks to fixed key + increasing versionCode) → open.

## 2. Risks (spec §8 checklist, each with our mitigation)

1. Not installed → fixed committed keystore; versionCode = run number; arm64-only; minSdk 24.
2. Starts-then-dies → lib names `SDL2`/`main`; SDL_main export mechanism verified (§0.3);
   CI step lists `libmain.so` symbols (readelf/llvm-readelf) and asserts `SDL_main` +
   asserts all `DT_NEEDED` .so are inside the APK.
3. Black screen → renderer fallback upstream; boot markers pinpoint the stage; RGB565 path
   is upstream's own.
4. Files not found → unpack to `getFilesDir()` (base_dir detection verified, §0.4);
   `-objects pak128/`; zip layout verified in CI (`unzip -l`, expect `simutrans/pak128/` or
   `pak128/` — both handled); case: pak file names are lowercase.
5. Imported-prebuilt .so packaging question (how libz.so etc. get into the APK in the official
   project) is NOT documented in their scripts → resolve empirically in Phase 2: CI step
   `unzip -l app-debug.apk` asserting `lib/arm64-v8a/libz.so` etc.; if missing, add explicit
   copy into `app/src/main/jniLibs/arm64-v8a/` (or build deps in-tree as real targets —
   then AGP packages them automatically).
6. Duplicate touch events → upstream already handles (hint + `SDL_TOUCH_MOUSEID` filters).
7. Text input/IME → upstream `hide_keyboard` + StartTextInput; acceptance test = typing a
   save-game name (Phase 4 checklist).
8. CI runs once then breaks → caches keyed on content hashes; secrets checked (workflow does
   not require DIAG_TOKEN to exist); permissions `contents: write` for the Release step.
9. Issue API 422/403 → body capped 60k; HTTP status logged; token scope verified.
10. "Works on my side" → forbidden; every claim cites a CI log line, artifact, or a device
    report (spec §2 rule 1).

## 3. Phase plan (updated with findings)

- **Phase 0 (this)**: recon + this file. Acceptance: notes committed. ✅ (local commit;
  push pending the person's one-time fork+token setup)
- **Phase 1**: pipeline proof. `android/` + CI + fixed keystore + trivial SDL2 color-screen
  app with touch color change, boot markers, both crash handlers, DiagUploader, test-issue
  button, and a hidden crash trigger. Acceptance: spec §6 Phase 1 items (1)–(5).
- **Phase 2**: real Simutrans in `libmain.so`; deps; assets = base data + pak128; first-run
  unpack; args incl. `-objects pak128/`; boot markers in startup; APK content assertions.
  Acceptance: menu visible + tappable, markers show `menu_shown`, no crash issues.
- **Phase 3**: new game on a small map (128×128 or smaller), `-fps 30` cap; acceptance:
  `new_game_started`, pak128 sprites visible, time advances, 5 min stable.
- **Phase 4**: verify the upstream gesture set on device; enable Android back-button trap →
  in-game menu mapping; UI scale default via `-autodpi` (+ `-screen_scale` from a small
  settings screen if needed); virtual toolbar (optional, toggleable) if needed.
  Acceptance: spec §6 Phase 4 checklist via the person's one-sentence answers.
- **Phase 5**: lifecycle (pause/resume, autosave — termination filter already exists),
  memory/thermals over 15 min, optional music (FluidSynth family or skip), es default lang,
  icon/app name, README.

## 4. Open items (verify with evidence, do not guess)

- AGP packaging of imported prebuilt shared libs (§2.5) — resolve in Phase 2 via APK listing.
- Debug variant compiles native at -O0 → slow for real gameplay. Plan: keep Gradle Debug
  variant but force `-DCMAKE_BUILD_TYPE=RelWithDebInfo` in CMake args (allowed; upstream's
  AndroidPreBuild did OPTIMIZE=1 with DEBUG=1 similarly) — decide at Phase 2 with frame-time
  evidence.
- Exact git tags for vendored deps if we vendor them (else use official submodule commits).
- Runner disk headroom with NDK r29 (~ fine on ubuntu-latest; cleanup step if needed).
- Whether `-log` (simu.log) lands in cwd or user dir on Android — observe in Phase 2 and
  route into `diag/`.

## 5. Changelog

- **2026-10-26 — Phase 0 (recon)** — agent environment: git 2.47.3, network OK, NO `gh` CLI,
  NO GitHub credentials in sandbox → the person must do a one-time fork + fine-grained PAT
  (Contents/Issues/Workflows/Secrets RW on that repo only) before Phase 1 pushes.
  - Cloned simutrans/simutrans @ master 460ffc2 (shallow for recon).
  - Discovered existing Android support in master + official simutrans-android-project
    (§0.1–0.2). Verified entry point, RGB565 software-render path, complete touch gesture
    layer, `-autodpi`/`-screen_scale` UI scaling, Android path handling, CLI options
    (`-objects`, `-singleuser`, `-set_*dir`, `-lang`, `-fps`, `-screensize`, `-fullscreen`,
    `-autodpi`, `-screen_scale`, `-debug`, `-log`, `-mute`, `-nomidi`, `-nosound`, `-threads`).
  - Verified official pak128 URL + size (93 MB zip / ~408 MB unpacked) and compatibility
    (125.0up vs master 125.0.2 Nightly). Decision: bundle in APK.
  - Wrote this file. No source changes made.
