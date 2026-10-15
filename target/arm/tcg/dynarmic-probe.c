/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Asset-free diagnostic only. This is not an alternative iOS CPU backend. */
#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/lt-dynarmic-probe.h"
#include "hw/core/boards.h"
#include "system/memory.h"
#include "dynarmic-probe-abi.h"

bool lt_dynarmic_probe_try(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    CPUTLBEntryFull translation;
    uint32_t regs[16], cpsr, opcode;
    AddressSpace *as;
    MemoryRegion *region;
    hwaddr offset, length = 4;
    static unsigned executions;
    const char *enabled = g_getenv("LT_DYNARMIC_PROBE");

    /* Restrict the prototype to the one-CPU, asset-free diagnostic machine.
     * Kernel, Thumb, big-endian, memory/CP15 operations and all real boards
     * always continue through the existing TCG implementation. */
    if (!enabled || strcmp(enabled, "1") || executions >= 8 ||
        !object_dynamic_cast(OBJECT(qdev_get_machine()), MACHINE_TYPE_NAME("none")) ||
        cpu->midr != 0x410fb767 || env->aarch64 || env->thumb ||
        (env->cp15.sctlr_el[1] & SCTLR_B) ||
        (env->regs[15] & 3)) {
        return false;
    }
    cpsr = cpsr_read(env);
    if ((cpsr & CPSR_M) != ARM_CPU_MODE_USR ||
        (cpsr & (CPSR_E | CPSR_IT_0_1 | CPSR_IT_2_7))) {
        return false;
    }

    /* Genuine instruction-fetch permission check, in C before any C++ JIT
     * frame exists. A failed probe leaves fault delivery to ordinary TCG. */
    if (!arm_cpu_tlb_fill_align(cs, &translation, env->regs[15],
                               MMU_INST_FETCH,
                               arm_to_core_mmu_idx(arm_mmu_idx(env)),
                               MO_32, 4, true, 0)) {
        return false;
    }
    as = arm_addressspace(cs, translation.attrs);
    region = address_space_translate(as, translation.phys_addr, &offset,
                                     &length, false, translation.attrs);
    if (length < 4 || !memory_region_is_ram(region) ||
        memory_region_is_ram_device(region)) {
        return false;
    }
    opcode = ldl_le_p((uint8_t *)memory_region_get_ram_ptr(region) + offset);
    if ((opcode & 0xffffff00) != 0xe3a00000 &&
        (opcode & 0xffffff00) != 0xe2800000) {
        return false;
    }
    memcpy(regs, env->regs, sizeof(regs));
    if (!lt_dynarmic_probe_integer(regs, &cpsr, opcode)) {
        /* No QEMU architectural state was changed by the C++ side. */
        return false;
    }
    memcpy(env->regs, regs, sizeof(regs));
    cpsr_write(env, cpsr, CPSR_NZCV | CPSR_Q | CPSR_GE, CPSRWriteRaw);
    executions++;
    fprintf(stderr, "LT_DYNARMIC_PROBE executed=%u pc=0x%08x opcode=0x%08x r0=%u\n",
            executions, regs[15] - 4, opcode, regs[0]);
    return true;
}
