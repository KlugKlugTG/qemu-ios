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

typedef struct CrashFrames {
    uintptr_t pc[12];
    unsigned count;
} CrashFrames;

#if defined(__ANDROID__)
static _Unwind_Reason_Code crash_frame(struct _Unwind_Context *ctx, void *opaque)
{
    CrashFrames *f = opaque;

    if (f->count < ARRAY_SIZE(f->pc)) {
        f->pc[f->count++] = (uintptr_t)_Unwind_GetIP(ctx);
    }
    return f->count < ARRAY_SIZE(f->pc) ? _URC_NO_REASON : _URC_END_OF_STACK;
}
#endif

/* Hex without snprintf: a signal handler must not take the library's locks, and a crash inside a crash
 * report is worse than no report. */
static void crash_hex(const char *label, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char line[64];
    size_t at = 0;

    while (*label) {
        line[at++] = *label++;
    }
    line[at++] = ' ';
    line[at++] = '0';
    line[at++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) {
        line[at++] = digits[(value >> shift) & 0xf];
    }
    line[at++] = '\n';
    ssize_t ignored = write(STDERR_FILENO, line, at);
    (void)ignored;
}

#if defined(__ANDROID__)
#include <link.h>

static int crash_find_phdr(struct dl_phdr_info *info, size_t size, void *data)
{
    struct { uintptr_t addr; uintptr_t *out; } *arg = data;
    uintptr_t base = info->dlpi_addr;

    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];

        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (arg->addr >= base + ph->p_vaddr && arg->addr < base + ph->p_vaddr + ph->p_memsz) {
            *arg->out = base;
            return 1;
        }
    }
    return 0;
}

/* The library this code lives in, found through the program headers: dladdr needs a dynamic symbol. */
static uintptr_t crash_library_base(void *addr)
{
    uintptr_t out = 0;
    struct { uintptr_t addr; uintptr_t *out; } arg = { (uintptr_t)addr, &out };

    dl_iterate_phdr(crash_find_phdr, &arg);
    return out;
}
#else
static uintptr_t crash_library_base(void *addr)
{
    Dl_info d = { 0 };

    return dladdr(addr, &d) && d.dli_fbase ? (uintptr_t)d.dli_fbase : 0;
}
#endif

/* Where this engine's own code starts: a crash address minus it resolves against the same library
 * built by CI, which is the only way to name a frame without symbols on the device. */
static uintptr_t crash_base;

static void crash_rel(const char *label, uintptr_t pc)
{
    crash_hex(label, crash_base && pc >= crash_base ? pc - crash_base : pc);
}

static void crash_backtrace(void)
{
#if defined(__ANDROID__)
    CrashFrames f = { 0 };

    _Unwind_Backtrace(crash_frame, &f);
    for (unsigned i = 0; i < f.count; i++) {
        crash_rel("#", f.pc[i]);
    }
#else
    void *frames[12];
    int n = backtrace(frames, (int)ARRAY_SIZE(frames));

    for (int i = 0; i < n; i++) {
        crash_rel("#", (uintptr_t)frames[i]);
    }
#endif
}

static void qemu_ios_crash_handler(int sig, siginfo_t *info, void *ctx)
{
    crash_hex("qemu: fatal signal", (uint64_t)sig);
    crash_hex("qemu: fault address", (uintptr_t)(info ? info->si_addr : NULL));
    crash_hex("qemu: si_code", (uint64_t)(info ? info->si_code : 0));
    crash_hex("qemu: base", crash_base);
    if (ctx) {
        ucontext_t *uc = ctx;
#if defined(__aarch64__)
        const mcontext_t *mc = &uc->uc_mcontext;

        crash_rel("qemu: pc", (uintptr_t)mc->pc);
        crash_rel("qemu: lr", (uintptr_t)mc->regs[30]);
        crash_hex("qemu: sp", (uintptr_t)mc->sp);
        crash_hex("qemu: x0", (uintptr_t)mc->regs[0]);
        crash_hex("qemu: x1", (uintptr_t)mc->regs[1]);
        crash_hex("qemu: x2", (uintptr_t)mc->regs[2]);
        crash_hex("qemu: x3", (uintptr_t)mc->regs[3]);
#elif defined(__arm__)
        crash_hex("qemu: pc", (uintptr_t)uc->uc_mcontext.arm_pc);
        crash_hex("qemu: sp", (uintptr_t)uc->uc_mcontext.arm_sp);
        crash_hex("qemu: lr", (uintptr_t)uc->uc_mcontext.arm_lr);
#else
        (void)uc;
#endif
    }
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
    crash_base = crash_library_base((void *)&qemu_ios_install_crash_handler);
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
