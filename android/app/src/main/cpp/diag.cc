#include "diag.h"

#include <SDL.h>
#include <SDL_system.h>

#include <android/log.h>
#include <jni.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unwind.h>
#include <dlfcn.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "simutrans-diag", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "simutrans-diag", __VA_ARGS__)

// ---------------------------------------------------------------------------
// Paths (internal files dir == Java getFilesDir())
static char g_diag_dir[512];
static char g_boot_log[512];
static char g_flag[512];
static char g_pending[512];

// JNI cache for diag::request_upload() / diag::upload_status() /
// diag::wait_for_assets()
static JavaVM* g_vm = nullptr;
static jclass g_diagClass = nullptr;
static jmethodID g_diagRequest = nullptr;
static jmethodID g_diagStatus = nullptr;
static jclass g_actClass = nullptr;
static jmethodID g_actAssetsReady = nullptr;

// async-signal-safe helpers -------------------------------------------------
static size_t sputs(char* dst, size_t n, size_t pos, const char* s) {
    while (*s && pos < n) dst[pos++] = *s++;
    return pos;
}
static size_t shex(char* dst, size_t n, size_t pos, unsigned long long v) {
    char buf[20]; int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v && i < 20) { int d = (int)(v & 0xF); buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v >>= 4; }
    pos = sputs(dst, n, pos, "0x");
    while (i > 0) { if (pos >= n) break; dst[pos++] = buf[--i]; }
    return pos;
}
static size_t sdec(char* dst, size_t n, size_t pos, long long v) {
    char buf[24]; int i = 0; bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
    if (u == 0) buf[i++] = '0';
    while (u && i < 24) { buf[i++] = (char)('0' + (int)(u % 10)); u /= 10; }
    if (neg && pos < n) dst[pos++] = '-';
    while (i > 0) { if (pos >= n) break; dst[pos++] = buf[--i]; }
    return pos;
}

// crash handling --------------------------------------------------------------
static struct sigaction g_oldact[5];
static const int g_sigs[5] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
static char g_altstack[64 * 1024] __attribute__((aligned(4096)));

static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) {
    uintptr_t* ips = (uintptr_t*)((void**)arg)[0];
    int* cnt = (int*)((void**)arg)[1];
    if (*cnt >= 32) return _URC_END_OF_STACK;
    uintptr_t ip = _Unwind_GetIP(ctx);
    if (ip) ips[(*cnt)++] = ip;
    return _URC_NO_REASON;
}

static void crash_handler(int sig, siginfo_t* info, void* /*uctx*/) {
    static char buf[8192]; // static, not on the crashing stack

    uintptr_t ips[32]; int nframes = 0;
    void* cbarg[2] = { &ips, &nframes };
    _Unwind_Backtrace(unwind_cb, cbarg);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    unsigned long long ms = (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)(ts.tv_nsec / 1000000);

    size_t pos = 0;
    const size_t n = sizeof(buf);
    pos = sputs(buf, n, pos, "# native-crash\n\n");
    pos = sputs(buf, n, pos, "FATAL SIGNAL ");
    pos = sdec(buf, n, pos, sig);
    pos = sputs(buf, n, pos, " (addr=");
    pos = shex(buf, n, pos, (unsigned long long)(info ? info->si_addr : 0));
    pos = sputs(buf, n, pos, ", pc=");
    pos = shex(buf, n, pos, (unsigned long long)(nframes > 0 ? ips[0] : 0));
    pos = sputs(buf, n, pos, ", ts=");
    pos = sdec(buf, n, pos, (long long)ms);
    pos = sputs(buf, n, pos, ")\n\nbacktrace:\n");
    for (int i = 0; i < nframes; i++) {
        pos = sputs(buf, n, pos, "  #");
        pos = sdec(buf, n, pos, i);
        pos = sputs(buf, n, pos, " pc=");
        pos = shex(buf, n, pos, (unsigned long long)ips[i]);
        Dl_info dli;
        memset(&dli, 0, sizeof(dli));
        if (dladdr((void*)ips[i], &dli) && dli.dli_fname) {
            pos = sputs(buf, n, pos, " ");
            pos = sputs(buf, n, pos, dli.dli_fname);
            pos = sputs(buf, n, pos, "+");
            pos = shex(buf, n, pos, (unsigned long long)((char*)ips[i] - (char*)dli.dli_fbase));
            if (dli.dli_sname) { pos = sputs(buf, n, pos, " "); pos = sputs(buf, n, pos, dli.dli_sname); }
        }
        pos = sputs(buf, n, pos, "\n");
    }
    if (nframes == 0) pos = sputs(buf, n, pos, "  (no frames)\n");
    pos = sputs(buf, n, pos, "\n(unwound in signal handler on alt stack)\n");

    char path[600];
    size_t l = strlen(g_pending);
    memcpy(path, g_pending, l + 1);
    path[l++] = '/';
    const char* nm = "native-crash-";
    size_t nml = strlen(nm);
    memcpy(path + l, nm, nml); l += nml;
    char digits[24];
    size_t dlen = sdec(digits, sizeof(digits), 0, (long long)ms);
    memcpy(path + l, digits, dlen); l += dlen;
    memcpy(path + l, ".md", 4);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        ssize_t r = write(fd, buf, pos);
        (void)r;
        close(fd);
    }
    // Marker line in boot.log so a plain boot.log upload also shows the crash.
    int bfd = open(g_boot_log, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if (bfd >= 0) {
        size_t p2 = 0;
        p2 = sdec(buf, n, p2, (long long)ms);
        p2 = sputs(buf, n, p2, " FATAL_SIGNAL_");
        p2 = sdec(buf, n, p2, sig);
        p2 = sputs(buf, n, p2, "\n");
        write(bfd, buf, p2);
        close(bfd);
    }

    // Restore default handling and re-raise so the OS records a real crash.
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, nullptr);
    raise(sig);
}

static void install_crash_handlers() {
    static stack_t st;
    st.ss_sp = g_altstack;
    st.ss_size = sizeof(g_altstack);
    st.ss_flags = 0;
    if (sigaltstack(&st, nullptr) != 0) LOGW("sigaltstack failed: %s", strerror(errno));
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < 5; i++) {
        if (sigaction(g_sigs[i], &sa, &g_oldact[i]) != 0) LOGW("sigaction %d failed", g_sigs[i]);
    }
}

// JNI -----------------------------------------------------------------------
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass c = env->FindClass("io/github/hatblack2002/simutrans/Diag");
    if (c == nullptr) {
        env->ExceptionClear();
        LOGW("JNI_OnLoad: Diag class not found; request_upload will retry at runtime");
    } else {
        g_diagClass = (jclass)env->NewGlobalRef(c);
        g_diagRequest = env->GetStaticMethodID(g_diagClass, "requestUpload", "()V");
        if (env->ExceptionCheck()) { env->ExceptionClear(); g_diagRequest = nullptr; }
        g_diagStatus = env->GetStaticMethodID(g_diagClass, "uploadStatus", "()Ljava/lang/String;");
        if (env->ExceptionCheck()) { env->ExceptionClear(); g_diagStatus = nullptr; }
    }
    jclass a = env->FindClass("io/github/hatblack2002/simutrans/SimutransActivity");
    if (a == nullptr) {
        env->ExceptionClear();
        LOGW("JNI_OnLoad: SimutransActivity not found; wait_for_assets will no-op");
    } else {
        g_actClass = (jclass)env->NewGlobalRef(a);
        g_actAssetsReady = env->GetStaticMethodID(g_actClass, "assetsReady", "()Z");
        if (env->ExceptionCheck()) { env->ExceptionClear(); g_actAssetsReady = nullptr; }
    }
    return JNI_VERSION_1_6;
}

// public API ----------------------------------------------------------------
static void setpath(char* dst, size_t n, const char* base, const char* leaf) {
    size_t l = strlen(base);
    if (l >= n) return;
    memcpy(dst, base, l);
    size_t m = strlen(leaf);
    if (l + 1 + m >= n) return;
    dst[l] = '/'; memcpy(dst + l + 1, leaf, m + 1);
}

void diag::boot_init() {
    const char* base = SDL_AndroidGetInternalStoragePath();
    setpath(g_diag_dir, sizeof(g_diag_dir), base, "diag");
    setpath(g_boot_log, sizeof(g_boot_log), g_diag_dir, "boot.log");
    setpath(g_flag, sizeof(g_flag), g_diag_dir, "boot-in-progress");
    setpath(g_pending, sizeof(g_pending), g_diag_dir, "pending");
    mkdir(g_diag_dir, 0700);
    mkdir(g_pending, 0700);
    install_crash_handlers();
}

void diag::log(const char* marker) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    unsigned long long ms = (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)(ts.tv_nsec / 1000000);
    char line[512];
    size_t pos = sdec(line, sizeof(line), 0, (long long)ms);
    line[pos++] = ' ';
    pos = sputs(line, sizeof(line), pos, marker);
    line[pos++] = '\n';
    int fd = open(g_boot_log, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if (fd >= 0) { write(fd, line, pos); close(fd); }
    LOGI("%s", marker);
}

void diag::boot_first_frame() {
    log("first_frame_drawn");
    unlink(g_flag);
}

void diag::request_upload() {
    if (g_diagClass && g_diagRequest) {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        if (env) {
            env->CallStaticVoidMethod(g_diagClass, g_diagRequest);
            if (env->ExceptionCheck()) { env->ExceptionClear(); LOGW("requestUpload threw"); }
            return;
        }
    }
    LOGW("request_upload: no cached JNI; skipping");
}

const char* diag::upload_status() {
    static char status[96] = "ENVIO: ESPERA";
    if (!g_diagClass || !g_diagStatus) return status;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) return status;
    jstring js = (jstring)env->CallStaticObjectMethod(g_diagClass, g_diagStatus);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return status; }
    if (js == nullptr) return status;
    const char* s = env->GetStringUTFChars(js, nullptr);
    if (s == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return status;
    }
    size_t i = 0;
    for (; i < sizeof(status) - 1 && s[i]; i++) status[i] = s[i];
    status[i] = '\0';
    env->ReleaseStringUTFChars(js, s);
    return status;
}

void diag::wait_for_assets() {
    if (!g_actClass || !g_actAssetsReady) {
        LOGW("wait_for_assets: no cached JNI; proceeding");
        return;
    }
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (!env) {
        LOGW("wait_for_assets: no JNIEnv; proceeding");
        return;
    }
    log("wait_assets_start");
    for (int waited = 0; waited < 300; ++waited) {  // up to 5 minutes
        jboolean ok = env->CallStaticBooleanMethod(g_actClass, g_actAssetsReady);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (ok) {
            log("assets_ready");
            return;
        }
        if (waited > 0 && waited % 10 == 0) {
            LOGI("still waiting for assets (%d s)", waited);
        }
        SDL_Delay(1000);
    }
    log("assets_wait_timeout");
}
