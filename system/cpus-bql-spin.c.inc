/* SPDX-License-Identifier: GPL-2.0-or-later */

/* A bounded alternative to immediately sleeping on a contended vCPU0 BQL.
 * Particularly useful to investigate hosts where a microsecond-scale sleep
 * incurs a millisecond-scale wakeup. Default off; no scheduler/affinity change.
 */
#define BQL_SPIN_NS 50000
static bool bql_spin_on;
static bool bql_spin_fallback_prof;
static uint64_t bql_spin_profiled_fallbacks;
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
    /* Independent diagnostic, only on the uncommon blocking path. Conditions
     * mode keeps this BQL hook intact; reuse its bounded site/span recorder.
     * The interval excludes the preceding 50us spin. Off adds no clocks. */
    int64_t begin = bql_spin_fallback_prof && native_wait_on ? get_clock() : 0;
    qemu_mutex_lock_impl(mutex, file, line);
    if (begin) {
        bql_spin_profiled_fallbacks++;
        native_wait_end(begin, 1, mutex, mutex, file, line);
    }
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
    const char *profile = getenv("LT_BQL_FALLBACK_PROF");
    const char *wait = getenv("LT_NATIVE_WAIT_PROF");
    bool profile_requested = profile && !strcmp(profile, "on");
    bql_spin_fallback_prof = bql_spin_on && profile_requested && wait &&
                             !strcmp(wait, "conditions");
    if (profile_requested) {
        fprintf(stderr, "[bql-fallback] requested=on configured=%s; "
                "requires active conditions trace\n",
                bql_spin_fallback_prof ? "on" : "off");
    }
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
    if (bql_spin_fallback_prof) {
        fprintf(stderr, "[bql-fallback] finished calls=%" PRIu64 " trace=%s\n",
                bql_spin_profiled_fallbacks, native_wait_on ? "on" : "off");
    }
}
