/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Default-off bounded host wait before vCPU0 sleeps for guest work. Observe
 * only a host kick sequence outside the BQL; inspect all guest/CPU predicates
 * again under the BQL. No guest time advancement or interrupt suppression. */
#define IDLE_SPIN_NS 250000
static bool idle_spin_on;
static uint32_t idle_spin_kicks;
static uint64_t idle_spin_attempts, idle_spin_resumed, idle_spin_fallback;

static void idle_spin_init(void)
{
    const char *mode = getenv("LT_IDLE_SPIN");
    idle_spin_on = mode && !strcmp(mode, "on");
    fprintf(stderr, "[idle-spin] requested=%s active=%s cpu=0 budget-ns=%d\n",
            idle_spin_on ? "on" : "off", idle_spin_on ? "on" : "off",
            IDLE_SPIN_NS);
}

/* Called with the BQL held and the CPU idle. False leaves the caller free to
 * take its original condition wait, still holding the BQL for that decision. */
static bool idle_spin_wait(CPUState *cpu)
{
    if (!idle_spin_on || cpu->cpu_index != 0 || !cpu->halted || cpu->stop ||
        !runstate_is_running()) {
        return false;
    }
    idle_spin_attempts++;
    uint32_t sequence = qatomic_read(&idle_spin_kicks);
    int64_t deadline = get_clock() + IDLE_SPIN_NS;

    bql_unlock();
    while (get_clock() < deadline) {
        if (qatomic_read(&idle_spin_kicks) != sequence) {
            break;
        }
        cpu_relax();
    }
    bql_lock();
    if (!cpu_thread_is_idle(cpu)) {
        idle_spin_resumed++;
        return true;
    }
    idle_spin_fallback++;
    return false;
}

static void idle_spin_kick(CPUState *cpu)
{
    if (idle_spin_on && cpu->cpu_index == 0) {
        qatomic_fetch_inc(&idle_spin_kicks);
    }
}

void qemu_ios_idle_spin_finish(void)
{
    if (idle_spin_on) {
        fprintf(stderr, "[idle-spin] finished attempts=%" PRIu64
                " resumed=%" PRIu64 " fallback=%" PRIu64 "\n",
                idle_spin_attempts, idle_spin_resumed, idle_spin_fallback);
    }
}
