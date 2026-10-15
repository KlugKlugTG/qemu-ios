/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TCG_IR_CACHE_STORE_H
#define TCG_IR_CACHE_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

/* These are hard limits, including stale records from a different build. */
#define TCG_IR_CACHE_MAX_BYTES (32 * 1024 * 1024)
#define TCG_IR_CACHE_MAX_RECORDS 4096
#define TCG_IR_CACHE_MAX_RECORD (128 * 1024)

typedef struct TCGIRCacheStore TCGIRCacheStore;

/* Existing absolute owner-only directory; never follows the final symlink.
 * A second simultaneous owner gets NULL. Errors always mean normal translation.
 * The caller serializes access, and owns the returned store and lookup GBytes.
 */
TCGIRCacheStore *tcg_ir_store_open(const char *directory,
                                 const uint8_t build[32]);
GBytes *tcg_ir_store_lookup(TCGIRCacheStore *store, const uint8_t key[32]);
bool tcg_ir_store_put(TCGIRCacheStore *store, const uint8_t key[32], GBytes *data);
void tcg_ir_store_close(TCGIRCacheStore *store);

#endif
