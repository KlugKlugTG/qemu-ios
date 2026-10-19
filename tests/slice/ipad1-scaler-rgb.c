/* The scaler's 32-bit RGB path (scaler_rgb) behind a fake DART: a 1x iPhone app turned onto the iPad's landscape
 * panel, and its 2x destination mapped in 64 KiB windows of packed rows (issue 47); Zoom's source, linear at the
 * stride 4.x programs (issue 54) and, on the iPad 3.2, in the same windows.
 *
 * Mutations (must fail this test): read the source as rows packed at ALIGN(w, 64) (the gather before issue 54);
 * turn every quarter turn counterclockwise, whatever +0x58 asks for (Smash Hit upside down, issue 47); band the rows
 * 64 KiB / stride at a time instead of 16 (5.1.1's 2x refused, issue 96).
 *
 * SLICE hw/arm/ipod_touch_scaler.c range typedef struct { | static const MemoryRegionOps
 * PKG glib-2.0
 */
#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef int SysBusDevice;
typedef int DeviceState;
#define ROUND_UP(n,d) (((n)+(d)-1)/(d)*(d))
typedef int MemoryRegion;
typedef uint64_t hwaddr;
typedef int *qemu_irq;
static uint8_t ram[0x200000];
static void cpu_physical_memory_read(uint64_t a,void *p,size_t n)
{ assert(a+n<=sizeof(ram));memcpy(p,ram+a,n); }
static void cpu_physical_memory_write(uint64_t a,const void *p,size_t n)
{ assert(a+n<=sizeof(ram));memcpy(ram+a,p,n); }
static void stw_le_p(void *p,unsigned v) { uint8_t *b=p;b[0]=v;b[1]=v>>8; }
static void qemu_set_irq(qemu_irq irq,int level) { *irq=level; }
#define error_report(...) ((void)0)
typedef int QEMUTimer;   /* scaler_write's end of a transfer; scaler_rgb is called directly here */
enum { QEMU_CLOCK_VIRTUAL };
static int64_t qemu_clock_get_ns(int c) { (void)c; return 0; }
static void timer_mod(QEMUTimer *t, int64_t at) { (void)t; (void)at; }
static void timer_del(QEMUTimer *t) { (void)t; }
static bool timer_pending(QEMUTimer *t) { (void)t; return false; }
#include "slice.h"

/* The source at IOVA 0x100000 maps straight to RAM 0, at 0x300000 in bands of `src_band` bytes of which the first
 * `src_pages` pages are mapped, onto packed RAM from 0; the destination at IOVA 0x200000 maps, in each band of
 * `dst_band` bytes, only the first `dst_pages` pages, onto packed RAM from DST. */
#define DST 0x100000
static unsigned dst_pages = 16, src_pages = 16, src_band = 0x10000, dst_band = 0x10000;
static hwaddr dart(void *opaque, uint32_t va, unsigned sid)
{
    if (va >= 0x100000 && va < 0x180000) return va - 0x100000;
    if (va >= 0x300000 && va < 0x400000 && (va - 0x300000) % src_band >> 12 < src_pages)
        return (va - 0x300000) / src_band * src_pages * 0x1000 + (va - 0x300000) % src_band;
    if (va >= 0x200000 && va < 0x300000 && (va - 0x200000) % dst_band >> 12 < dst_pages)
        return DST + (va - 0x200000) / dst_band * dst_pages * 0x1000 + (va - 0x200000) % dst_band;
    return (hwaddr)-1;
}
static uint32_t src_px(unsigned u, unsigned v) { return 0xff000000u | v << 8 | u; }
static uint32_t at(uint64_t a) { uint32_t x; memcpy(&x, ram + a, 4); return x; }

/* The source packed in RAM at `pitch` pixels a row and programmed at `sstride` pixels from IOVA `sbase`. */
static void go_src(IPodScalerState *s, unsigned sw, unsigned sh, unsigned dw, unsigned dh, unsigned stride,
                   unsigned step, uint32_t sbase, unsigned pitch, unsigned sstride)
{
    uint32_t *r = s->regs;
    memset(ram, 0, sizeof(ram));
    for (unsigned v = 0; v < sh; v++)
        for (unsigned u = 0; u < sw; u++) memcpy(ram + (v * pitch + u) * 4, &(uint32_t){src_px(u, v)}, 4);
    r[0x10/4] = 6; r[0x30/4] = 0x406; r[0x14/4] = sbase; r[0x34/4] = 0x200000;
    r[0x1c/4] = sstride << 16 | sstride;
    r[0x24/4] = sw << 16 | sh; r[0x40/4] = dw << 16 | dh; r[0x3c/4] = stride / 4 << 16 | stride / 4;
    r[0x50/4] = r[0x54/4] = step;
    assert(scaler_rgb(s));
}

static void go(IPodScalerState *s, unsigned sw, unsigned sh, unsigned dw, unsigned dh, unsigned stride, unsigned step)
{
    go_src(s, sw, sh, dw, dh, stride, step, 0x100000, ROUND_UP(sw, 64), ROUND_UP(sw, 64));
}

int main(void)
{
    int irq = 0;
    IPodScalerState s = {.irq = &irq};
    ipod_scaler_set_iommu((DeviceState *)&s, dart, NULL, 2);
    /* 1:1 with the size turned (320x480 -> 480x320 as measured, here 4x6 -> 6x4): destination (x, y) is source
     * column sw - 1 - y, row x, as the panel turns the portrait UI. */
    go(&s, 4, 6, 6, 4, 256, 0x10000);
    for (unsigned y = 0; y < 4; y++)
        for (unsigned x = 0; x < 6; x++) assert(at(DST + y * 256 + x * 4) == src_px(3 - y, x));
    /* +0x58 = 3 turns it the other way, clockwise: a landscape-right app's 960x640 layer onto the iPhone 4's
     * portrait panel (here 6x4 -> 4x6), destination (x, y) is source column y, row sh - 1 - x. */
    s.regs[0x58 / 4] = 3;
    go(&s, 6, 4, 4, 6, 256, 0x10000);
    for (unsigned y = 0; y < 6; y++)
        for (unsigned x = 0; x < 4; x++) assert(at(DST + y * 256 + x * 4) == src_px(y, 3 - x));
    s.regs[0x58 / 4] = 0;
    /* 2x into rows of 128 pixels at a programmed stride of 1024 bytes: bands of 16 rows, 16 strides (16 KiB) long,
     * packed at 512 bytes, 2 pages of each mapped (960x640 at 4096: 64 KiB bands, 15 pages). Every row lands; none
     * is dropped at the first unmapped page. */
    dst_pages = 2; dst_band = 0x4000;
    go(&s, 64, 64, 128, 128, 1024, 0x8000);
    for (unsigned y = 0; y < 128; y++)
        for (unsigned x = 0; x < 128; x += 37) assert(at(DST + y * 512 + x * 4) == src_px(x / 2, y / 2));
    /* A stride that is the packed pitch stays linear (the 1x destination: 480 wide at 2048). */
    dst_pages = 16; dst_band = 0x10000;
    go(&s, 64, 32, 64, 32, 256, 0x10000);
    for (unsigned y = 0; y < 32; y++) assert(at(DST + y * 256 + 4) == src_px(1, y));
    /* Zoom on 4.x: a 27-wide crop at the 32-pixel stride it is programmed with (427 at 432 on the iPhone 4), not
     * packed at ALIGN(27, 64) = 64, blown up 2x. */
    go_src(&s, 27, 20, 54, 40, 256, 0x8000, 0x100000, 32, 32);
    for (unsigned y = 0; y < 40; y++)
        for (unsigned x = 0; x < 54; x += 5) assert(at(DST + y * 256 + x * 4) == src_px(x / 2, y / 2));
    /* Zoom on the iPad 3.2: the source programmed at a stride of 256 pixels and mapped in bands of 16 rows (16 KiB)
     * packed at ALIGN(w, 64), 1 of 4 pages each (11 of 16 at 683 wide and 4096 on the device), here across 5 bands. */
    src_pages = 1; src_band = 0x4000;
    go_src(&s, 27, 80, 27, 40, 256, 0x20000, 0x300000, 64, 256);
    for (unsigned y = 0; y < 40; y++) assert(at(DST + y * 256 + 26 * 4) == src_px(26, y * 2));
    /* 5.1.1's 2x of a 1x app (issue 96): the app's own 320x480 layer is the source, programmed at a stride of 512
     * pixels and mapped in 32 KiB bands of 16 rows packed at 1280 bytes, 5 of 8 pages each, as the device maps it;
     * here turned 1:1 onto the 480x320 destination. With 64 KiB / stride rows a band, row 16 was read from an
     * unmapped page and every frame was refused, leaving the destination's old pixels on screen. */
    src_pages = 5; src_band = 0x8000;
    go_src(&s, 320, 480, 480, 320, 1920, 0x10000, 0x300000, 320, 512);
    for (unsigned y = 0; y < 320; y += 7)
        for (unsigned x = 0; x < 480; x += 13) assert(at(DST + y * 1920 + x * 4) == src_px(319 - y, x));
    puts("PASS: turned 1x destination, a clockwise turn by +0x58, 2x destination in packed 16-row bands, "
         "linear destination, "
         "Zoom's source linear at its stride and in 16-row bands, 5.1.1's 2x source in 32 KiB bands");
}
