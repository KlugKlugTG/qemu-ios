/*
 * Entry point for embedding this emulator in an iOS app.
 *
 * On macOS, main() runs qemu_init() on the main thread, hands the main loop to
 * a second thread and gives the main thread to CFRunLoopRun(), because the
 * Cocoa UI needs it. An iOS app already owns its main thread and its runloop,
 * so instead the app calls qemu_ios_main() on a background pthread and the
 * whole emulator -- init, main loop, cleanup -- lives there.
 *
 * QEMU is not designed to be started twice in one process: it never fully
 * releases what it allocates. One VM per app launch, as in UTM.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "exec/gdbstub.h"
#include "system/replay.h"
#include "system/system.h"
#ifdef CONFIG_TCG
#include "tcg/ir-cache-lifecycle.h"
#endif

/* This tree registers every thread it runs through the main loop; the
 * portable entry only asks before it unregisters one it created itself. */
bool rcu_thread_is_registered(void);

bool rcu_thread_is_registered(void)
{
    return true;
}

#ifdef CONFIG_TCG
/* The A6 tree has no experimental IR cache; the portable entry still calls it. */
void arm_tcg_ir_cache_finish(void)
{
}
#endif

#include "qemu-ios-ui.h"

#if defined(__linux__)
bool qemu_rcu_embedded_main(void) __attribute__((visibility("hidden")));
bool qemu_rcu_embedded_main(void)
{
    return true;
}
#endif

/* Narrow developer diagnostic: attach only after a frame capture to inspect
 * the guest's proc/task/pmap structures. No listener unless explicitly armed;
 * bind loopback so Android access can use an adb forward. */
static void guest_gdb_init(void)
{
    const char *value = getenv("LT_GUEST_GDB_PORT");
    if (!value || !*value || !strcmp(value, "off") || !strcmp(value, "0")) {
        return;
    }
    unsigned port;
    if (qemu_strtoui(value, NULL, 10, &port) || port < 1024 || port > 65535) {
        fprintf(stderr, "[guest-gdb] invalid LT_GUEST_GDB_PORT\n");
        return;
    }
    g_autofree char *endpoint = g_strdup_printf("tcp:127.0.0.1:%u", port);
    Error *err = NULL;
    if (!gdbserver_start(endpoint, &err)) {
        fprintf(stderr, "[guest-gdb] %s\n", error_get_pretty(err));
        error_free(err);
        return;
    }
    fprintf(stderr, "[guest-gdb] listening 127.0.0.1:%u; attach after capture\n", port);
}


/*
 * A crash inside the engine is otherwise invisible on a phone: the embedding host sees only the
 * process disappear, and the signal number is all it can report. Print the signal, the faulting
 * address and a short backtrace on stderr, which the host already captures, so a report says where
 * it died instead of guessing. Only the embeddings that ask for it get the handler.
 */
#include <signal.h>
#include <ucontext.h>
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <unwind.h>
#else
#include <execinfo.h>
#endif

#if defined(__ANDROID__)
typedef struct CrashFrames {
    void *pc[16];
    unsigned count;
} CrashFrames;

static _Unwind_Reason_Code crash_frame(struct _Unwind_Context *ctx, void *opaque)
{
    CrashFrames *f = opaque;

    if (f->count < ARRAY_SIZE(f->pc)) {
        f->pc[f->count++] = (void *)_Unwind_GetIP(ctx);
    }
    return f->count < ARRAY_SIZE(f->pc) ? _URC_NO_REASON : _URC_END_OF_STACK;
}

/* Bionic has no execinfo; the unwinder plus dladdr names what it can and prints the rest as addresses. */
static void crash_backtrace(void)
{
    CrashFrames f = { 0 };
    char line[256];

    _Unwind_Backtrace(crash_frame, &f);
    for (unsigned i = 0; i < f.count; i++) {
        Dl_info d = { 0 };
        int len;

        if (dladdr(f.pc[i], &d) && d.dli_sname) {
            len = snprintf(line, sizeof(line), "#%u %p %s+0x%lx\n", i, f.pc[i], d.dli_sname,
                           (unsigned long)((uintptr_t)f.pc[i] - (uintptr_t)d.dli_saddr));
        } else {
            len = snprintf(line, sizeof(line), "#%u %p\n", i, f.pc[i]);
        }
        if (len > 0) {
            ssize_t ignored = write(STDERR_FILENO, line, (size_t)len);
            (void)ignored;
        }
    }
}
#else
static void crash_backtrace(void)
{
    void *frames[16];
    int n = backtrace(frames, (int)ARRAY_SIZE(frames));

    if (n > 0) {
        backtrace_symbols_fd(frames, n, STDERR_FILENO);
    }
}
#endif

static void qemu_ios_crash_handler(int sig, siginfo_t *info, void *ctx)
{
    char line[512];
    int len = snprintf(line, sizeof(line), "\nqemu: fatal signal %d at %p (si_code %d)\n",
                       sig, info ? info->si_addr : NULL, info ? info->si_code : 0);
    if (len > 0) {
        ssize_t ignored = write(STDERR_FILENO, line, (size_t)len);
        (void)ignored;
    }
#if defined(__aarch64__) || defined(__arm__)
    if (ctx) {
        ucontext_t *uc = ctx;
        len = snprintf(line, sizeof(line), "qemu: pc %p lr %p\n",
                       (void *)uc->uc_mcontext.pc, (void *)uc->uc_mcontext.regs[14]);
        if (len > 0) {
            ssize_t ignored = write(STDERR_FILENO, line, (size_t)len);
            (void)ignored;
        }
    }
#endif
    crash_backtrace();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void qemu_ios_install_crash_handler(void)
{
    static bool installed;
    static char altstack[64 * 1024];
    stack_t ss = { .ss_sp = altstack, .ss_size = sizeof(altstack), .ss_flags = 0 };

    if (installed || !getenv("LTM_CRASH_TRACE")) {
        return;
    }
    installed = true;
    sigaltstack(&ss, NULL);
    struct sigaction sa = { 0 };
    sa.sa_sigaction = qemu_ios_crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}

int qemu_ios_main(int argc, char **argv)
{
    int status;
#if defined(__linux__)
    const bool owns_rcu_reader = !rcu_thread_is_registered();
    if (owns_rcu_reader) {
        rcu_register_thread();
    }
#endif

    qemu_init(argc, argv);
    guest_gdb_init();
    qemu_ios_frame_path_init();

    /*
     * Resume automatically when restoring a snapshot.
     *
     * -incoming clears `autostart` (system/vl.c), so a restored machine sits
     * in `paused` forever waiting for a `cont` that, in this app, no monitor is
     * ever going to send. Setting it back here means the VM starts itself the
     * moment the incoming stream is consumed -- which is what makes restore
     * look instant rather than look broken.
     */
    autostart = 1;

    qemu_ios_install_crash_handler();
    if (getenv("LTM_CRASH_TRACE")) {
        fprintf(stderr, "qemu: argc %d", argc);
        for (int i = 0; i < argc; i++) {
            fprintf(stderr, " [%d]%s", i, argv[i]);
        }
        fprintf(stderr, "\n");
    }

    /* Console and AIO context exist only now; the display attaches here. */
    qemu_ios_ui_vm_started();

    /*
     * qemu_init() returns holding the BQL and the replay mutex, and the main
     * loop expects to take them itself. Handing them straight back keeps this
     * identical to the sequence in system/main.c.
     */
    bql_unlock();
    replay_mutex_unlock();

    replay_mutex_lock();
    bql_lock();
    status = qemu_main_loop();

    /* Main loop is done: no further bottom half will ever run. Tell the
     * app side before cleanup so late input events are dropped rather than
     * queued onto a context nobody services. */
    qemu_ios_ui_vm_stopped();
    qemu_cleanup(status);
    qemu_ios_frame_path_finish();
#ifdef CONFIG_TCG
    /* No vCPU/device activity remains. Retain final experimental cache totals
     * before the embedding host disposes its worker without running atexit.
     * This observes cleanup only; it does not change guest-halt confirmation.
     */
    arm_tcg_ir_cache_finish();
#endif
    bql_unlock();
    replay_mutex_unlock();

#if defined(__linux__)
    if (owns_rcu_reader) {
        rcu_unregister_thread();
    }
#endif
    return status;
}
