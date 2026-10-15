/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARM_DYNARMIC_PROBE_ABI_H
#define ARM_DYNARMIC_PROBE_ABI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Only synthetic MOV/ADD r0 immediate instructions; no memory or CP15 ABI. */
bool lt_dynarmic_probe_integer(uint32_t regs[16], uint32_t *cpsr,
                               uint32_t opcode);

#ifdef __cplusplus
}
#endif
#endif
