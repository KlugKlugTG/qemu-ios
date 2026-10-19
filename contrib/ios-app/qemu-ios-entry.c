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
#include "system/replay.h"
#include "system/system.h"
#ifdef CONFIG_TCG
#include "tcg/ir-cache-lifecycle.h"
#endif

#include "qemu-ios-ui.h"

#if defined(__linux__)
bool qemu_rcu_embedded_main(void) __attribute__((visibility("hidden")));
bool qemu_rcu_embedded_main(void)
{
    return true;
}
#endif

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
