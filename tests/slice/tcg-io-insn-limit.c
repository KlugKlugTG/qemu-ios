/* Regression: alternate TB modes at one physical PC must not lengthen a known
 * MMIO boundary, including concurrent updates and physical address zero.
 * SLICE include/exec/translation-block.h define CF_COUNT_MASK
 * SLICE accel/tcg/translate-all.c fn tb_remember_io_insn_limit tb_apply_io_insn_limit
 * PKG glib-2.0
 * CFLAGS -Wall -Wextra -Werror
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <glib.h>
typedef uint64_t tb_page_addr_t;
static struct { GHashTable *io_insn_limits; GMutex io_insn_lock; } tb_ctx;
#define qemu_mutex_lock g_mutex_lock
#define qemu_mutex_unlock g_mutex_unlock
#include "slice.h"

static int lookup(tb_page_addr_t pc)
{
    return GPOINTER_TO_INT(g_hash_table_lookup(tb_ctx.io_insn_limits,
                                             (gpointer)(uintptr_t)pc));
}

static gpointer update(gpointer opaque)
{
    int limit = GPOINTER_TO_INT(opaque);
    for (int i = 0; i < 10000; i++) {
        tb_remember_io_insn_limit(0x404, limit);
    }
    return NULL;
}

int main(void)
{
    tb_ctx.io_insn_limits = g_hash_table_new(NULL, NULL);
    g_mutex_init(&tb_ctx.io_insn_lock);
    tb_remember_io_insn_limit(0, 12);
    tb_remember_io_insn_limit(0, 3);
    tb_remember_io_insn_limit(0, 19);
    assert(lookup(0) == 3);
    tb_remember_io_insn_limit(0x404, 7);
    GThread *threads[4];
    for (int i = 0; i < 4; i++) {
        threads[i] = g_thread_new("io-limit", update, GINT_TO_POINTER(i + 1));
    }
    for (int i = 0; i < 4; i++) {
        g_thread_join(threads[i]);
    }
    assert(lookup(0x404) == 1);
    assert(lookup(0) == 3);
    assert(tb_apply_io_insn_limit(0x404, 0, 64) == 1);
    assert(tb_apply_io_insn_limit(0x404, 2, 2) == 2);
    assert(tb_apply_io_insn_limit(0x404, 1, 1) == 1);
    assert(tb_apply_io_insn_limit((tb_page_addr_t)-1, 0, 64) == 64);
    assert(tb_apply_io_insn_limit(0x408, 0, 64) == 64);
    assert(tb_apply_io_insn_limit(0, 0, 2) == 2);
    g_hash_table_destroy(tb_ctx.io_insn_limits);
    g_mutex_clear(&tb_ctx.io_insn_lock);
    puts("PASS: MMIO limits only shorten; explicit-count replay stays intact");
    return 0;
}
