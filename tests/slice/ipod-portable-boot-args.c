/* Actual timer callback: slow handoff readiness, bounded scan frequency,
 * successful writes and cessation after match/budget. Legacy Mac policy too.
 * SLICE hw/arm/ipod_touch_2g.c define BOOT_ARGS_CMDLINE_OFF|BOOT_ARGS_SCAN_LEN|BOOT_ARGS_CMDLINE_LEN
 * SLICE hw/arm/ipod_touch_2g.c fn boot_args_signature ipod_touch_boot_args_discovery_interval_ms ipod_touch_boot_args_search_budget_ms ipod_touch_requested_boot_args ipod_touch_set_boot_args_now ipod_touch_stage_boot_args
 * VARIANT
 * VARIANT -D__APPLE__=1
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
#define QEMU_CLOCK_VIRTUAL 0
#define MEMTXATTRS_UNSPECIFIED 0
typedef struct {
    void *nsas, *boot_args_timer;
    unsigned boot_args_writes;
    uint64_t boot_args_scan_deadline, boot_args_scan_started_ms, boot_args_scan_attempts;
    uint32_t boot_args_delay_ms, boot_args_repeat, boot_args_interval_ms, boot_args_addr;
    bool boot_args_scan_failed;
    char boot_args[512];
} IPodTouchMachineState;
static uint64_t now, ready_at, next;
static unsigned schedules, reads, writes;
static int timer;
static uint8_t command_line[256];
static uint64_t qemu_clock_get_ms(int clock) { return now; }
static void *timer_new_ms(int clock, void (*callback)(void *), void *opaque) { return &timer; }
static void timer_mod(void *value, uint64_t deadline) { assert(value == &timer); next = deadline; schedules++; }
static uint32_t ldl_le_p(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void stl_le_p(uint8_t *p, uint32_t value) { for(int i=0;i<4;i++)p[i]=value>>(i*8); }
static void signature(uint8_t *p) { stl_le_p(p,1);stl_le_p(p+4,0xc0000000);stl_le_p(p+8,0x08000000); }
static int address_space_rw(void *space, uint32_t addr, int attrs, uint8_t *data, size_t size, int write) {
    const uint32_t handoff = 0x08000400;
    if (write) {
        assert(addr == handoff + 0x38 && size == sizeof(command_line));
        memcpy(command_line, data, size); writes++; return 0;
    }
    reads++; memset(data,0,size);
    if (now >= ready_at && addr <= handoff && (uint64_t)addr + size >= handoff + 12)
        signature(data + handoff - addr);
    return 0;
}
#include "slice.h"

int main(void) {
    IPodTouchMachineState s={.boot_args_repeat=200,.boot_args_interval_ms=250};
    strcpy(s.boot_args,"-v amfi_allow_any_signature=1");
    ready_at=120000;
    ipod_touch_stage_boot_args(&s);
#if defined(__APPLE__)
    assert(s.boot_args_scan_deadline==50000);
    ipod_touch_set_boot_args_now(&s); assert(next==1 && reads>200);
    now=50000; unsigned old_schedules=schedules,old_reads=reads;
    ipod_touch_set_boot_args_now(&s);
    assert(schedules==old_schedules && reads==old_reads && !writes);
    puts("PASS: legacy macOS discovery cadence/deadline unchanged");
#else
    assert(s.boot_args_scan_deadline==600000);
    /* Dispatch the production timer at its real requested cadence. */
    while (next<=1000) {
        now=next; ipod_touch_set_boot_args_now(&s);
        assert(next==now+250 && !writes);
    }
    assert(s.boot_args_scan_attempts==5 && reads<1500);
    /* The old 50-second window must not terminate a still-booting guest. */
    now=50000; ipod_touch_set_boot_args_now(&s); assert(next==50250 && !writes);
    now=120000; ipod_touch_set_boot_args_now(&s);
    assert(s.boot_args_addr==0x08000400 && writes==1);
    assert(!strcmp((char*)command_line,s.boot_args));
    uint64_t found_scans=s.boot_args_scan_attempts;
    unsigned found_reads=reads;
    for(unsigned i=1;i<200;i++) { now=next;ipod_touch_set_boot_args_now(&s); }
    assert(writes==200 && s.boot_args_scan_attempts==found_scans);
    assert(reads-found_reads==199); /* Cached signature, never another full scan. */
    /* Unknown firmware stops at the exact budget without another scan/rearm. */
    s.boot_args_addr=0; ready_at=UINT64_MAX; now=0;
    ipod_touch_stage_boot_args(&s);
    now=600000; unsigned old_schedules=schedules,old_reads=reads;
    ipod_touch_set_boot_args_now(&s);
    assert(schedules==old_schedules && reads==old_reads && !s.boot_args_scan_attempts);
    /* A caller's 1ms interval cannot cause >40 full scans/second. */
    s.boot_args_interval_ms=1;now=0;ipod_touch_stage_boot_args(&s);
    ipod_touch_set_boot_args_now(&s);assert(next==25);
    /* Explicit empty startup args create no timer and no discovery work. */
    s.boot_args[0]=0;old_schedules=schedules;
    ipod_touch_stage_boot_args(&s);assert(schedules==old_schedules);
    puts("PASS: pre-handoff readiness at120s, configured250ms cadence, >=25ms bound, cached-match refresh and exact600s stop");
#endif
}
