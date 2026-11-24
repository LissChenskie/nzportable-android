// SPDX-License-Identifier: GPL-2.0-or-later
// On-device diagnostics for players without a PC: appends the NZP_LAN log to a file and records
// native crashes (signal, fault address, backtrace) so the app can offer them for sharing after a restart.
#ifdef __ANDROID__
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <unwind.h>

static int log_fd = -1;
static char marker_path[512];
static struct sigaction old_actions[32];
static const int crash_signals[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };

void NZP_FileLog(const char *line) {
    char buf[640];
    struct timespec ts;
    int n;
    if (log_fd < 0) return;
    clock_gettime(CLOCK_REALTIME, &ts);
    n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d %s\n", (int)(ts.tv_sec / 3600 % 24), (int)(ts.tv_sec / 60 % 60),
                 (int)(ts.tv_sec % 60), (int)(ts.tv_nsec / 1000000), line);
    if (n > 0) { if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1; (void)!write(log_fd, buf, (size_t)n); }
}
static void put(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void put(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    NZP_FileLog(line);
}
struct bt_state { void *frames[40]; int count; };
static _Unwind_Reason_Code bt_cb(struct _Unwind_Context *ctx, void *arg) {
    struct bt_state *st = (struct bt_state *)arg;
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && st->count < 40) st->frames[st->count++] = (void *)pc;
    return st->count >= 40 ? _URC_END_OF_STACK : _URC_NO_REASON;
}
static void crash_handler(int sig, siginfo_t *info, void *uctx) {
    struct bt_state st = { {0}, 0 };
    int fd;
    put("=== NATIVE CRASH: signal %d (%s) code=%d fault_addr=%p ===", sig, strsignal(sig), info ? info->si_code : -1,
        info ? info->si_addr : NULL);
    if (marker_path[0] && (fd = open(marker_path, O_CREAT | O_WRONLY, 0644)) >= 0) close(fd);
    _Unwind_Backtrace(bt_cb, &st);
    for (int i = 0; i < st.count; i++) {
        Dl_info di;
        if (dladdr(st.frames[i], &di) && di.dli_fname)
            put("  #%02d pc %p  %s+0x%lx  %s", i, st.frames[i], di.dli_fname,
                (unsigned long)((char *)st.frames[i] - (char *)di.dli_fbase), di.dli_sname ? di.dli_sname : "?");
        else put("  #%02d pc %p", i, st.frames[i]);
    }
    put("=== END NATIVE CRASH ===");
    // Chain to the previous handler so the system still produces its tombstone.
    if (sig > 0 && sig < 32 && (old_actions[sig].sa_flags & SA_SIGINFO) && old_actions[sig].sa_sigaction) {
        old_actions[sig].sa_sigaction(sig, info, uctx);
        /* Returned: the runtime (ART) handled the fault, e.g. an implicit null check. Not a crash. */
        if (marker_path[0]) unlink(marker_path);
        put("(signal %d was handled by the runtime, continuing)", sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}
JNIEXPORT void JNICALL Java_org_nzportable_android_LanLog_nativeInit(JNIEnv *env, jclass cls, jstring jlog, jstring jmarker) {
    const char *log = (*env)->GetStringUTFChars(env, jlog, NULL);
    const char *marker = (*env)->GetStringUTFChars(env, jmarker, NULL);
    struct sigaction sa;
    (void)cls;
    if (log) log_fd = open(log, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (marker) snprintf(marker_path, sizeof(marker_path), "%s", marker);
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (unsigned i = 0; i < sizeof(crash_signals) / sizeof(crash_signals[0]); i++)
        sigaction(crash_signals[i], &sa, &old_actions[crash_signals[i]]);
    if (log) (*env)->ReleaseStringUTFChars(env, jlog, log);
    if (marker) (*env)->ReleaseStringUTFChars(env, jmarker, marker);
    put("crash/log capture armed");
}
#endif
