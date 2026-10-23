/* SPDX-License-Identifier: GPL-2.0-or-later
 * LT_GLWAIT_PROF=1: where the vCPU waits for the host GPU. Every 5 s, per
 * phase: calls a second, mean and worst microseconds and milliseconds a
 * presented frame; then which guest contexts issued the finishes, flushes,
 * write-backs and presents. Off costs one branch per phase.
 */
enum {
    GW_CALL, GW_FINISH, GW_FLUSH, GW_WB_READ, GW_WB_COPY, GW_PRESENT,
    GW_PRESENT_WAIT, GW_PRESENT_READ, GW_PRESENT_COPY, GW_PRESENT_PAGES, GW_PRESENT_QUEUE, GW_FENCE,
    GW_N
};
static const char *const gw_names[GW_N] = {
    "all-calls", "finish", "flush", "wb-read", "wb-copy", "present",
    "present-wait", "present-read", "present-copy", "present-pages", "present-queue", "fence",
};
static int gw_on = -1;
static struct {
    uint64_t n[GW_N], ns[GW_N], max[GW_N];
    uint64_t frames, t0;
    struct { const char *why; uint64_t n; } site[12];   /* bridge-side completions, by trigger */
    /* finish: since the last present, the gap and the calls in it */
    uint64_t present_end, since_calls, gap_ns, gap_calls, gap_n;
    struct { uint32_t ctx; uint64_t calls, finish, flush, wb, present; uint32_t slot[6], slot_n[6]; } c[8];
} gw;

static inline bool gw_enabled(void)
{
    if (gw_on < 0) {
        const char *v = getenv("LT_GLWAIT_PROF");
        gw_on = v && strcmp(v, "off") && strcmp(v, "0");
    }
    return gw_on;
}

static inline uint64_t gw_t(void)
{
    return gw_enabled() ? get_clock() : 0;
}

static inline void gw_add(unsigned what, uint64_t t0)
{
    if (t0) {
        uint64_t d = get_clock() - t0;
        gw.n[what]++;
        gw.ns[what] += d;
        gw.max[what] = MAX(gw.max[what], d);
    }
}

/* One of the per-context counters below, for @ctx. */
static void gw_slot(uint32_t ctx, uint32_t slot);
#define GW_CTX(ctx, field) do { if (gw_on > 0) gw_ctx(ctx)->field++; } while (0)
static typeof(gw.c[0]) *gw_ctx(uint32_t ctx)
{
    unsigned i, free = ARRAY_SIZE(gw.c);
    for (i = 0; i < ARRAY_SIZE(gw.c); i++) {
        if (gw.c[i].ctx == ctx) return &gw.c[i];
        if (!gw.c[i].ctx && free == ARRAY_SIZE(gw.c)) free = i;
    }
    i = free < ARRAY_SIZE(gw.c) ? free : ARRAY_SIZE(gw.c) - 1;
    gw.c[i] = (typeof(gw.c[0])){ .ctx = ctx };
    return &gw.c[i];
}

/* The first six slots a context calls, with counts: what a quiet context does. */
static void gw_slot(uint32_t ctx, uint32_t slot)
{
    typeof(gw.c[0]) *c = gw_ctx(ctx);
    c->calls++;
    gw.since_calls++;
    for (unsigned i = 0; i < ARRAY_SIZE(c->slot); i++) {
        if (!c->slot_n[i]) c->slot[i] = slot;
        if (c->slot[i] == slot) {
            c->slot_n[i]++;
            return;
        }
    }
}

static void gw_report_async(double secs, double frames);

static void gw_report(void)
{
    uint64_t now = get_clock();
    if (!gw.t0) gw.t0 = now;
    if (now - gw.t0 < 5000000000ULL) return;
    double secs = (now - gw.t0) / 1e9, frames = gw.frames ? gw.frames : 1;
    fprintf(stderr, "[glwait] %.1fs frames %.1f/s\n", secs, gw.frames / secs);
    for (unsigned i = 0; i < GW_N; i++) {
        if (!gw.n[i]) continue;
        fprintf(stderr, "[glwait]   %-14s %8.1f/s mean %7.1f us max %7.1f us  %6.3f ms/frame\n",
                gw_names[i], gw.n[i] / secs, gw.ns[i] / 1e3 / gw.n[i], gw.max[i] / 1e3,
                gw.ns[i] / 1e6 / frames);
    }
    gw_report_async(secs, frames);
    if (gw.gap_n) {
        fprintf(stderr, "[glwait]   finish after present: %.1f us, %.1f calls\n",
                gw.gap_ns / 1e3 / gw.gap_n, (double)gw.gap_calls / gw.gap_n);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(gw.c); i++) {
        if (!gw.c[i].ctx || !(gw.c[i].finish | gw.c[i].flush | gw.c[i].wb | gw.c[i].present)) continue;
        fprintf(stderr, "[glwait]   ctx %08x calls %.0f/s finish %.1f/s flush %.1f/s wb %.1f/s present %.1f/s\n",
                gw.c[i].ctx, gw.c[i].calls / secs, gw.c[i].finish / secs, gw.c[i].flush / secs,
                gw.c[i].wb / secs, gw.c[i].present / secs);
        for (unsigned k = 0; k < ARRAY_SIZE(gw.c[i].slot) && gw.c[i].slot_n[k]; k++) {
            fprintf(stderr, "[glwait]     slot %u %.1f/s\n", gw.c[i].slot[k], gw.c[i].slot_n[k] / secs);
        }
    }
    memset(&gw, 0, sizeof(gw));
    gw.t0 = now;
}
