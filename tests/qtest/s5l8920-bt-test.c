/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The kboot boards' Bluetooth controller answers its HCI on the UART the board's DT hangs it from: the N18's
 * BCM4329 on uart1 (bluetooth,n18), the N88's BCM4325 on uart3. With nothing on the N18's port, 3.1.1's BlueTool
 * never got its HCI_Reset answered, BTServer could not come up, and SpringBoard's status bar waited out two 5 s
 * BTSessionAttach timeouts on every turn of the interface (LightTouchMac `sessions rotation` on n18ap 7C146).
 */
#include "qemu/osdep.h"
#include "libqtest.h"

#define UART(n) (0x82500000ULL + (n) * 0x100000)
#define MSEC 1000000LL

static char *kboot;

typedef struct Board {
    const char *machine;
    int uart;
} Board;

static void hci_reset(const void *data)
{
    const Board *b = data;
    const uint64_t u = UART(b->uart);
    const uint8_t cmd[] = { 1, 0x03, 0x0c, 0 };                         /* H4 command: HCI_Reset */
    const uint8_t evt[] = { 4, 0x0e, 4, 1, 0x03, 0x0c, 0 };             /* Command Complete, status 0 */
    QTestState *q = qtest_initf("-machine %s,kboot=%s,wifi=off -display none -audio driver=none -nic none",
                                b->machine, kboot);

    qtest_writel(q, u + 4, 1);                                          /* UCON: polled receive, no DMA */
    qtest_writel(q, u + 8, 7);                                          /* UFCON: FIFOs on, both cleared */
    for (size_t i = 0; i < sizeof(cmd); i++) {
        qtest_writeb(q, u + 0x20, cmd[i]);                              /* UTXH */
    }
    qtest_clock_step(q, 10 * MSEC);                                     /* past the controller's latency */
    g_assert_cmpuint(qtest_readl(q, u + 0x18) & 255, ==, sizeof(evt));  /* UFSTAT: received bytes */
    for (size_t i = 0; i < sizeof(evt); i++) {
        g_assert_cmphex(qtest_readb(q, u + 0x24), ==, evt[i]);          /* URXH */
    }
    qtest_quit(q);
}

static const Board boards[] = {
    { "n18", 1 },
    { "n88", 3 },
};

int main(int argc, char **argv)
{
    /* A K48KBOOT bundle with an empty image: the trailer only. */
    static const uint8_t trailer[24] = {
        'K', '4', '8', 'K', 'B', 'O', 'O', 'T',
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40,
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
    };
    int fd, ret;

    g_test_init(&argc, &argv, NULL);
    fd = g_file_open_tmp("s5l8920-kboot-XXXXXX", &kboot, NULL);
    g_assert(fd >= 0);
    g_assert(write(fd, trailer, sizeof(trailer)) == sizeof(trailer));
    close(fd);
    for (size_t i = 0; i < ARRAY_SIZE(boards); i++) {
        g_autofree char *path = g_strdup_printf("/s5l8920/bt/%s-hci-reset", boards[i].machine);
        qtest_add_data_func(path, &boards[i], hci_reset);
    }
    ret = g_test_run();
    unlink(kboot);
    g_free(kboot);
    return ret;
}
