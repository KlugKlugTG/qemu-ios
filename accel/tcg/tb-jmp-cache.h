/*
 * The per-CPU TranslationBlock jump cache.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_TB_JMP_CACHE_H
#define ACCEL_TCG_TB_JMP_CACHE_H

#include "qemu/rcu.h"
#include "exec/cpu-common.h"

#ifdef QEMU_EXPERIMENTAL_TB_JMP_CACHE_BITS
#if QEMU_EXPERIMENTAL_TB_JMP_CACHE_BITS != 12 && \
    QEMU_EXPERIMENTAL_TB_JMP_CACHE_BITS != 14
#error "Experimental TB jump cache supports only 12 or 14 bits"
#endif
#define TB_JMP_CACHE_BITS QEMU_EXPERIMENTAL_TB_JMP_CACHE_BITS
#else
#define TB_JMP_CACHE_BITS 12
#endif
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)

/*
 * Invalidated in parallel; all accesses to 'tb' must be atomic.
 * A valid entry is read/written by a single CPU, therefore there is
 * no need for qatomic_rcu_read() and pc is always consistent with a
 * non-NULL value of 'tb'.  Strictly speaking pc is only needed for
 * CF_PCREL, but it's used always for simplicity.
 */
typedef struct CPUJumpCache {
    struct rcu_head rcu;
#ifdef QEMU_EXPERIMENTAL_TB_CACHE_STATS
    /* Owner-vCPU diagnostics; not migrated, cleared, or used for correctness. */
    uint64_t fast_hits, empty_misses, pc_misses, state_misses;
    uint64_t slow_hits, slow_absent;
#endif
    struct {
        TranslationBlock *tb;
        vaddr pc;
    } array[TB_JMP_CACHE_SIZE];
} CPUJumpCache;

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */
