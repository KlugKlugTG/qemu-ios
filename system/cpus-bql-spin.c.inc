/* SPDX-License-Identifier: GPL-2.0-or-later */

/* A bounded alternative to immediately sleeping on a contended vCPU0 BQL.
 * Particularly useful to investigate hosts where a microsecond-scale sleep
 * incurs a millisecond-scale wakeup. Default off; no scheduler/affinity change.
 */
#define BQL_SPIN_NS 50000
static bool bql_spin_on;
static uint64_t bql_spin_contended, bql_spin_acquired, bql_spin_fallback;

static void bql_spin_lock(QemuMutex *mutex, const char *file, int line)
{
    CPUState *cpu = current_cpu;

    if (!cpu || cpu->cpu_index != 0) {
        qemu_mutex_lock_impl(mutex, file, line);
        return;
    }
    if (!qemu_mutex_trylock_impl(mutex, file, line)) {
        return;
    }
    bql_spin_contended++;
    int64_t deadline = get_clock() + BQL_SPIN_NS;
    while (get_clock() < deadline) {
        cpu_relax();
        if (!qemu_mutex_trylock_impl(mutex, file, line)) {
            bql_spin_acquired++;
            return;
        }
    }
    bql_spin_fallback++;
    qemu_mutex_lock_impl(mutex, file, line);
}

static void bql_spin_init(void)
{
    const char *mode = getenv("LT_BQL_SPIN");
    bool requested = mode && !strcmp(mode, "on");

    if (requested && qatomic_read(&bql_mutex_lock_func) == qemu_mutex_lock_impl) {
        bql_spin_on = true;
        qatomic_set(&bql_mutex_lock_func, bql_spin_lock);
    }
    fprintf(stderr, "[bql-spin] requested=%s active=%s cpu=0 budget-ns=%d\n",
            requested ? "on" : "off", bql_spin_on ? "on" : "off", BQL_SPIN_NS);
}

void qemu_ios_bql_spin_finish(void)
{
    if (bql_spin_on) {
        fprintf(stderr, "[bql-spin] finished contended=%" PRIu64
                " acquired=%" PRIu64 " fallback=%" PRIu64 " hook=%s\n",
                bql_spin_contended, bql_spin_acquired, bql_spin_fallback,
                qatomic_read(&bql_mutex_lock_func) == bql_spin_lock ?
                "unchanged" : "replaced");
    }
}
