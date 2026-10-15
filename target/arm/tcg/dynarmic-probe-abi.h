/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARM_DYNARMIC_PROBE_ABI_H
#define ARM_DYNARMIC_PROBE_ABI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Snapshot-only bounded diagnostic. No QEMU helper or live memory callback
 * crosses this ABI. Historical GDB steps remain single instruction; the
 * separately enabled Run caller accepts one 2..8-instruction block. */
#define LT_DYNARMIC_PROBE_MAX_STEPS 8
#define LT_DYNARMIC_PROBE_CONTEXT_WORDS 12

typedef struct LTDynarmicProbeStep {
    uint32_t pc;
    uint32_t opcode;
    uint64_t code_identity;
    bool has_read;
    uint32_t read_address;
    uint32_t read_value;
    uint32_t expected_regs[16];
    uint32_t expected_cpsr;
} LTDynarmicProbeStep;

typedef struct LTDynarmicProbePlan {
    unsigned count;
    uint32_t initial_regs[16];
    uint32_t initial_cpsr;
    uint64_t owner;
    uint64_t context[LT_DYNARMIC_PROBE_CONTEXT_WORDS];
    LTDynarmicProbeStep steps[LT_DYNARMIC_PROBE_MAX_STEPS];
} LTDynarmicProbePlan;

bool lt_dynarmic_probe_execute(const LTDynarmicProbePlan *plan,
                              uint32_t regs[16], uint32_t *cpsr);

typedef struct LTDynarmicBlockStats {
    uint64_t jit_creations;
    uint64_t runs;
    uint64_t generation;
    unsigned code_reads;
    unsigned data_reads;
    bool cache_hit;
} LTDynarmicBlockStats;

/* Run one actual branch-terminated block, not a loop of Jit::Step calls.
 * Reusable allocation/cache are private to this CPU host thread. */
bool lt_dynarmic_probe_block(const LTDynarmicProbePlan *plan,
                            uint32_t regs[16], uint32_t *cpsr,
                            LTDynarmicBlockStats *stats);
void lt_dynarmic_probe_block_invalidate(void);

#ifdef __cplusplus
}
#endif
#endif
