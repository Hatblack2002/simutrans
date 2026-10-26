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

- **2026-10-26 — Phase 0 published + Phase 1 (pipeline proof) implemented**
  - The person delivered a GitHub token (classic PAT, full scopes incl. repo/workflow) — the
    one-time setup is DONE, done by the agent, not the person:
    - Fork created: **Hatblack2002/simutrans** (branch `android-port` pushed; Phase 0 notes live).
    - Pre-existing stub repo `Hatblack2002/Simutrans` (a prior attempt: green-CI workflow but
      NEVER a Release upload — that is why the person never received an APK) renamed to
      `simutrans-attempt-1`, preserved for reference. Its workflow file was studied:
      `android-actions/setup-android@v3` + explicit `sdkmanager` pins proven green on
      ubuntu-24.04; its last commits are real lessons (SIGPIPE under pipefail, obsolete SDK
      packages, abiFilters rewrite, debug-variant prebuilt path).
    - Actions secret `DIAG_TOKEN` set via API (libsodium sealed box) from the same token.
      SECURITY NOTE: the token is a full-scope classic PAT — fine for this private-ish fork
      during bring-up; before any wider APK distribution, replace with a fine-grained PAT
      scoped to Issues:RW on this repo only (spec §5.2's original plan).
  - Phase 1 artifacts (all under `android/` + `.github/workflows/android.yml`):
    - Gradle project mirroring the official android-project pins: AGP 8.13.0, wrapper 8.13,
      NDK 29.0.14206865, SDK CMake 3.31.6, compileSdk 36, minSdk 24, targetSdk 33,
      Java 21 (Temurin), `abiFilters 'arm64-v8a'`, `ANDROID_STL=c++_shared`.
    - SDL2 vendored by exact pin `6ad5a966` (FetchContent tarball + SHA256) — same commit
      as the official project's submodule; SDL Java glue (9 files, org.libsdl.app) vendored
      from that same commit; NO AndroidX anywhere.
    - `libmain.so` trivial app: SDL2 SOFTWARE path only (window surface, no renderer GL),
      color screen; 1 finger = color change; 2 fingers = upload diagnostics now;
      3 fingers = deliberate SIGSEGV (tests crash pipeline); BACK = clean quit.
      On-screen Spanish hints via built-in 5x7 font (no accent glyphs needed).
    - Diagnostics per spec §5: `diag/boot.log` markers (`app_start`…`first_frame_drawn`),
      `diag/boot-in-progress` flag (survives a dead boot → `boot-failure` report on next
      launch), native crash handler (sigaction on alt stack; SEGV/ABRT/BUS/FPE/ILL;
      async-signal-safe manual formatting; `_Unwind_Backtrace` + `dladdr`; format
      `FATAL SIGNAL <n> (addr=…, pc=…)`; re-raises after writing
      `diag/pending/native-crash-<ts>.md`), Java uncaught-exception handler
      (`java-crash-*.md`), one-per-versionCode `boot-ok` report, `DiagUploader`
      (HttpsURLConnection → POST /repos/Hatblack2002/simutrans/issues, label `device-log`,
      body ≤ 60 000 chars truncated-in-middle, HTTP 201 → delete file, ≤ 5 per run,
      empty token ⇒ disabled but files kept). Token + repo injected via `BuildConfig`
      from `-PdiagToken=` in CI.
    - Fixed debug keystore committed `android/keystore/debug.keystore` (alias
      `androiddebugkey`, pass `android`) → every build installs over the previous one;
      `versionCode = GITHUB_RUN_NUMBER`; `versionName = 0.1.<run>+<sha>`.
    - CI `.github/workflows/android.yml`: push android-port + dispatch;
      `contents: write`; pinned action SHAs (checkout v4.4.0, setup-java v4.9.1,
      setup-android v3.2.2, upload-artifact v4.6.2); sdkmanager installs exact NDK/CMake;
      build does NOT require DIAG_TOKEN; verifies APK (`libmain.so`/`libSDL2.so`/
      `libc++_shared.so` present, aapt badging, apksigner cert); publishes GitHub
      Release tag `latest-debug` (prerelease) with STABLE filename
      `simutrans-android-debug.apk`; artifact + logs always uploaded.
  - Discovery affecting Phase 2: upstream `.gitignore` ignores `/simutrans/text` (all
    translations) and `*.bdf` (fonts) — they are fetched at build time
    (`tools/get_lang_files.sh`), and `*.zip` ignores any committed pak128 → pak128 and
    text/font data must be fetched in CI into build-time assets, never committed.

- **2026-10-26 — Phase 1 CI: GREEN on first attempt (run 40229634704)**
  - All 13 steps green. Log evidence (run1 logs zip):
    `BUILD SUCCESSFUL in 1m 9s`; APK contains `lib/arm64-v8a/libmain.so` (47 216 B),
    `lib/arm64-v8a/libSDL2.so` (1 931 080 B), `libc++_shared.so` (1 374 336 B);
    aapt: `package: name='io.github.hatblack2002.simutrans' versionCode='1'
    versionName='0.1.1+fe67b10'`, `native-code: 'arm64-v8a'`;
    apksigner: `Signer #1 certificate DN: CN=Android Debug, O=Simutrans Android Port, C=US`
    (our fixed keystore). Local binary checks of the downloaded release APK:
    `SDL_main` and `JNI_OnLoad` GLOBAL in libmain.so dynsym; every DT_NEEDED of
    libmain.so/libSDL2.so resolved (APK or system); APK sha256
    `a5b3f195d87f397394c349d242c8aaa5e174fa85c762ce1f5ce1a80ebd1430e8` (3 550 506 B).
  - Incident + fix: the inherited upstream workflows (10 files: Nightly win/mac/ubuntu/
    makeobj/tests + update-nightly-release) fired on our push and one failed
    (winGDI-32). All were DISABLED via API (`state: disabled_manually`, zero diff).
  - Incident (mine) + recovery: while deleting the stale upstream "Nightly" release in
    the fork I deleted our own fresh `latest-debug` release instead (asset gone; tag
    still exists). Recovery: the next CI run's Publish step re-creates the release on
    the existing tag and re-uploads the APK (workflow is idempotent that way — by
    design). Stale "Nightly" release+tag then deleted properly. Lesson: verify release
    IDs from full JSON, not from interleaved rg output.
  - Upstream also ships `.github/workflows/nightly-android.yml` (their own Android
    nightly, now disabled) — read it in Phase 2 for their asset/pak64 recipe.

- **2026-10-26 — Incident: "the app closes immediately on open" (person's report) — root cause found & fixed**
  - Symptom: installed release APK closes instantly on launch; ZERO `device-log`
    issues arrived — consistent with a crash happening BEFORE `Diag.init()` runs.
  - Root cause: `AndroidManifest.xml` declares launcher activity `.SimutransActivity`,
    but that Java class was never written. Dex proof on the downloaded release APK:
    the bytes `SimutransActivity` appear nowhere in classes*.dex. Android cannot
    instantiate the launchable activity → instant close; our diag entry point never
    executes → diagnostic blindness. Green CI could not catch it: aapt/apksigner
    verify the manifest and signing, not that manifest-referenced classes exist
    in the dex.
  - Fix (3 parts):
    1) Added `SimutransActivity.java` (extends `org.libsdl.app.SDLActivity`):
       calls `Diag.init(this)` BEFORE `super.onCreate` (packages previous
       boot-failure + installs Java crash handler before anything else can throw),
       then `Diag.startUploader(this)` right after.
    2) `Diag.startUploader` now uploads anything pending IMMEDIATELY (reports from
       previous crashed boots), then waits for first frame as before — removes the
       "starved uploader" blind spot for immediate-close failure modes.
    3) CI Verify step now extracts the launchable activity from `aapt dump badging`,
       builds its JVM descriptor (`Lio/github/.../SimutransActivity;`) and greps the
       APK's classes*.dex — a missing launchable activity fails the build forever.
  - Expected next: person reinstalls from the SAME link (same fixed keystore +
    higher versionCode = GITHUB_RUN_NUMBER → in-place upgrade, no uninstall
    needed); the app should show the color test screen with Spanish hints.

---

## 2. Phase 1 acceptance — COMPLETE (2026-10-26)

Device reports received end-to-end on the fork repo: boot-ok (build 3 & 4),
boot-failure capture, three native-crash reports with full arm64 backtraces
(SIGSEGV addr=0x0 from the deliberate 3-finger test, frames resolving into
libmain.so SDL_main → exactly the planted null write). On-screen upload status
(ENVIO:) shipped in v4 (9701030) after the issues-disabled incident.

## 3. Phase 2 — the real game (2026-10-26)

Person's directive (verbatim intent): stop shipping test screens; build the
complete game; error capture stays (it is automatic — every crash ships itself
to the repo on next launch).

### 3.1 Architecture (all decisions re-verified against the tree this session)

- libmain.so = the ENTIRE simutrans tree via the repo root CMakeLists.txt
  (its `if(ANDROID) add_library(simutrans SHARED)` branch), OUTPUT_NAME=main.
  SDL_main comes from simsys_s2.cc's `main()` (SDL_main.h renames it) — the
  phase-1 main.cc test screen is deleted.
- Deps, all but SDL2 linked STATICALLY into libmain.so:
  - zlib → NDK system lib (sysroot ships zlib.h + libz.so at android-24);
    provided as an INTERFACE target named ZLIB::ZLIB.
  - bzip2 1.0.8 → vendored at android/app/src/main/cpp/bzip2 (7 .c + bzlib.h,
    no cmake upstream; tarball sha256 ab5a03176e…c4a2269, verified locally).
  - libpng 1.6.44 → FetchContent, its own CMakeLists, PNG_STATIC=ON
    (sha256 60c4da1d…8b8ce8e, verified locally).
  - freetype 2.13.3 → FetchContent, static, all FT_DISABLE_* deps on
    (sha256 05503506…063289, verified locally).
  - SDL2 → unchanged pin 6ad5a966… (shared, as in phase 1).
- Options: SIMUTRANS_BACKEND=sdl2, MULTI_THREAD=OFF, ZSTD=OFF, FONTCONFIG=OFF,
  UPNP=OFF, WARNINGS_AS_ERRORS=OFF, USE_REVISION=1 (deterministic; avoids git
  probing at build time; revision is a label for offline play).
- Debug-variant performance: AGP forces CMAKE_BUILD_TYPE=Debug and neither
  CMake nor SimutransCompileOptions pass any -O level (=> clang -O0, unplayable
  software renderer at 1440x3200). Forced CMAKE_C_FLAGS_DEBUG="-g -O2" and
  CMAKE_CXX_FLAGS_DEBUG="-g -O2 -fno-limit-debug-info" globally in our
  cpp/CMakeLists — the "debug" APK is only debug-SIGNED, not debug-SLOW.

### 3.2 Upstream patches (2 files, both logged here per §2 rule 10)

1. cmake/SimutransFindDependencies.cmake — the ANDROID branch now starts with
   `if (NOT TARGET ZLIB::ZLIB)` and skips its whole prebuilt-sibling-dir import
   block (../zlib, ../bzip2, …, fluidsynth family, OpenMP REQUIRED) when our
   build already provided real targets with those names. Balance checked
   (if/endif depth 0). Desktop branch untouched.
2. src/simutrans/sys/simsys_s2.cc — three `#ifdef __ANDROID__` hunks:
   a. `#include "diag.h"` after the SDL include block;
   b. dr_flush(): first presented frame → diag::boot_first_frame() (clears the
      boot flag; static bool guard);
   c. main(): diag::boot_init() + log(native_main) + diag::wait_for_assets()
      before sysmain() — the game touches NO file before the Java-side
      first-run unpack finishes.

### 3.3 First-run asset pipeline

- CI fetches pak128 (URL from src/paksetinfo.h — "pak128 2.10.3 for 125.0 up",
  97,565,449 bytes) into assets/pak128.zip (noCompress) and copies the git-
  tracked base data dir simutrans/ (config, font BDFs, text incl. es.tab,
  themes, script, ai, music) into assets/simutrans/.
- AssetInstaller.java (background thread, Spanish progress overlay added via
  addContentView): copies assets/simutrans/** → filesDir/**, unzips
  pak128.zip → filesDir/pak128/** with %-progress; zip layout normalized
  (optional "simutrans/" wrapper stripped, everything forced under pak128/ —
  mirrors upstream unpak.sh logic). Markers: .assets-base-<versionCode> and
  .assets-pak128-<versionCode>-<zipSize> → upgrades re-copy base (seconds) and
  re-unpack the pak only when the bundled zip actually changes.
- Activity: getArguments() = -autodpi -fullscreen -log -debug 2 -objects pak128/;
  onDestroy → System.exit(0) (same as official app: C++ globals cannot re-init);
  assetsReady() polled by native.
- Failure path: installer exception → overlay shows the error + diag report
  "assets-failure" → native 5-min timeout → game's own pakset-missing fatal
  window → boot-failure report on next launch. Every failure self-reports.

### 3.4 Diagnostics in the game build

diag.cc/h unchanged except: +wait_for_assets() (JNI-cached
SimutransActivity.assetsReady, 1 s poll, 5 min cap, markers wait_assets_start /
assets_ready / assets_wait_timeout in boot.log); JNI_OnLoad also caches the
activity class. The 2/3-finger test gestures are gone with main.cc — touch is
now the game's own (upstream: tap=click, drag=pan, long-press=right-click,
pinch=zoom, 3-finger drag=map scroll).

### 3.5 Build/size expectations

~950 translation units of the game + SDL2 + 3 static deps on 4 vCPU: expect
15–25 min CI (was ~2 min). APK ≈ 12 MB base (compressed) + 93 MB pak zip
(stored) + libs ≈ ~110 MB — GitHub Release, fine. First launch unpacks ≈ 400 MB
to internal storage (S20: plenty), expect 1–3 min with the progress overlay.
