/* The iPod touch 4's i2s0 plays at the rate its codec is set to, without a guest.
 *
 * The CS42L59 masters i2s0's frame clock and AppleCS42L59Audio picks the rate in the codec's register 0a (0x0b
 * 44.1 kHz; 0x13 22.05 kHz, which iOS 6 switches to for VoiceOver's speech). The port's frame rate, which the
 * output voice and the CDMA's pacing both use, must follow it while TX runs, and go back to the NCO's when no
 * codec drives the clock. The CS42L58's rate stays in register 05.
 *
 * SLICE hw/arm/ipod_touch_cs42l58.c fn cs42l58_sample_rate cs42l58_send
 * SLICE hw/arm/s5l8930_i2s.c define I2S_(CTRL|TXCOM|CMD_RUN|MCLK_DIV|FRAME_BITS|FRAME_MASTER)[[:space:]]
 * SLICE hw/arm/s5l8930_i2s.c range static S5L8930I2SState *i2s_ports | static void i2s_in_cb
 * SLICE hw/arm/s5l8930_i2s.c fn i2s_set_rate i2s_tx_running i2s_lrclk_update
 * CFLAGS -Wall -Werror
 */
#include <assert.h>
#define trace_event_get_state_backends(id) 0
#define TRACE_PRINTF(fn, ...) do { if (0) printf(__VA_ARGS__); } while (0)
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef unsigned Clock;
typedef int ClockEvent;
static unsigned clock_get_hz(Clock *clock) { return clock ? *clock : 0; }

/* The codec's LRCLK out is i2s0's lrclk in, as hw/arm/ipad1.c connects them. */
typedef struct S5L8930I2SState {
    uint32_t regs[0x400];
    bool ctrl_run;
    uint8_t port;
    Clock *lrclk;
    uint64_t nco_base;
    uint32_t nco_ref_hz;   /* 0: the A4's PMGR NCO */
    unsigned rate;
    void *voice, *audio_be;
} S5L8930I2SState;
static S5L8930I2SState i2s0;
static void i2s_lrclk_update(void *, ClockEvent);
static void clock_update_hz(Clock *clock, unsigned rate)
{
    *clock = rate;
    i2s_lrclk_update(&i2s0, 0);
}

typedef struct {
    unsigned cmd;
    bool have_cmd, autoinc;
    uint8_t regs[128];
    bool pdn_status;
    uint8_t rate_reg;
    Clock *lrclk;
} CS42L58State;
typedef CS42L58State I2CSlave;
#define CS42L58(s) (s)
#define CS42L58_MAP_INCR 0x80
#define CS42L58_REG_CHIPID 1
static bool codec_trace(void) { return false; }

/* PMGR NCO 0's bit-clock register, which nothing programs on the iPod touch 4. */
static uint32_t nco_bclk;
typedef uint64_t hwaddr;
static int address_space_memory;
#define MEMTXATTRS_UNSPECIFIED 0
#define S5L8930_NCO_BCLK(n) (0xbf100104 + 0x10 * (n))
static uint32_t address_space_ldl_le(int *as, hwaddr addr, int attrs, void *result)
{
    return addr == S5L8930_NCO_BCLK(0) ? nco_bclk : 0;
}

struct audsettings { int freq, nchannels, fmt; bool big_endian; };
#define AUDIO_FORMAT_S16 1
static int opened, voice_rate;
static void i2s_out_cb(void *opaque, int free_bytes) {}
static void *audio_be_open_out(void *be, void *old, const char *name, void *opaque,
                               void (*cb)(void *, int), struct audsettings *as)
{
    assert(opaque == &i2s0 && cb == i2s_out_cb && as->nchannels == 2);
    opened++;
    voice_rate = as->freq;
    return opaque;
}
static void audio_be_set_volume_out_lr(void *be, void *voice, int mute, int l, int r) {}
#include "slice.h"

static void codec_write(CS42L58State *codec, uint8_t reg, uint8_t value)
{
    codec->have_cmd = false;
    cs42l58_send(codec, reg);
    cs42l58_send(codec, value);
}

int main(void)
{
    Clock lrclk = 0;
    CS42L58State cs42l59 = { .rate_reg = 0x0a, .lrclk = &lrclk };

    i2s_ports[0] = &i2s0;
    i2s0 = (S5L8930I2SState) { .lrclk = &lrclk, .rate = 44100, .voice = &i2s0 };
    i2s0.regs[I2S_TXCOM >> 2] = I2S_CMD_RUN;
    i2s0.regs[I2S_FRAME_BITS >> 2] = 0x28000300;   /* 10B500's config: the codec's slave */

    /* Before the kernel has set either clock: 44.1 kHz. */
    assert(s5l8930_i2s_rate(0) == 44100);
    nco_bclk = 64 * 48000;
    assert(s5l8930_i2s_rate(0) == 48000);
    nco_bclk = 0;

    /* Boot sets the codec to 44.1 kHz; VoiceOver's speech takes it to 22.05 kHz and back. */
    codec_write(&cs42l59, 0x0a, 0x0b);
    assert(lrclk == 44100 && s5l8930_i2s_rate(0) == 44100 && opened == 0);
    codec_write(&cs42l59, 0x0a, 0x13);
    assert(lrclk == 22050 && s5l8930_i2s_rate(0) == 22050);
    assert(opened == 1 && voice_rate == 22050 && i2s0.rate == 22050);
    codec_write(&cs42l59, 0x0a, 0x0b);
    assert(s5l8930_i2s_rate(0) == 44100 && opened == 2 && voice_rate == 44100);

    /* The CS42L59's register 05 is not its rate; the CS42L58's is. */
    codec_write(&cs42l59, 0x05, 0x13);
    assert(lrclk == 44100 && opened == 2);
    CS42L58State cs42l58 = { .rate_reg = 0x05, .lrclk = &lrclk };
    codec_write(&cs42l58, 0x05, 0x13);
    assert(lrclk == 22050 && voice_rate == 22050);

    /* Stopped, a rate change waits for the next start (which reads the port's rate). */
    i2s0.regs[I2S_TXCOM >> 2] = 0;
    codec_write(&cs42l59, 0x0a, 0x09);
    assert(s5l8930_i2s_rate(0) == 48000 && voice_rate == 22050);

    /* A port no codec drives runs at its NCO. */
    assert(s5l8930_i2s_rate(1) == 44100);
    puts("PASS: i2s0 runs at the CS42L59's LRCLK (register 0a), else at its NCO");
    return 0;
}
