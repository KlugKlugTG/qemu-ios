/* SPDX-License-Identifier: GPL-2.0-or-later */
/* No QEMU headers/state/longjmp paths cross this C++ boundary. */
#include <algorithm>
#include <array>
#include <exception>
#include <optional>
#include <dynarmic/interface/A32/a32.h>
#include "dynarmic-probe-abi.h"

namespace {
using namespace Dynarmic;
using namespace Dynarmic::A32;

struct Callbacks final : UserCallbacks {
    Jit *jit = nullptr;
    uint32_t pc, opcode;
    bool unsupported = false;
    uint64_t ticks = 1;

    Callbacks(uint32_t p, uint32_t op) : pc(p), opcode(op) {}
    void reject() { unsupported = true; jit->HaltExecution(HaltReason::MemoryAbort); }
    std::optional<uint32_t> MemoryReadCode(VAddr address) override {
        return address == pc ? std::optional<uint32_t>(opcode) : std::nullopt;
    }
    uint8_t MemoryRead8(VAddr) override { reject(); return 0; }
    uint16_t MemoryRead16(VAddr) override { reject(); return 0; }
    uint32_t MemoryRead32(VAddr) override { reject(); return 0; }
    uint64_t MemoryRead64(VAddr) override { reject(); return 0; }
    void MemoryWrite8(VAddr, uint8_t) override { reject(); }
    void MemoryWrite16(VAddr, uint16_t) override { reject(); }
    void MemoryWrite32(VAddr, uint32_t) override { reject(); }
    void MemoryWrite64(VAddr, uint64_t) override { reject(); }
    void InterpreterFallback(VAddr, size_t) override { reject(); }
    void CallSVC(uint32_t) override { reject(); }
    void ExceptionRaised(VAddr, Exception) override { reject(); }
    void AddTicks(uint64_t count) override { ticks = count < ticks ? ticks - count : 0; }
    uint64_t GetTicksRemaining() override { return ticks; }
};
}

extern "C" bool lt_dynarmic_probe_integer(uint32_t regs[16], uint32_t *cpsr,
                                         uint32_t opcode)
{
    const bool move = (opcode & 0xffffff00) == 0xe3a00000;
    if (!move && (opcode & 0xffffff00) != 0xe2800000) {
        return false;
    }
    try {
        Callbacks callbacks(regs[15], opcode);
        UserConfig config{};
        config.callbacks = &callbacks;
        config.arch_version = ArchVersion::v6K;
        config.code_cache_size = 8 * 1024 * 1024;
        config.check_halt_on_memory_access = true;
        /* Fresh context per instruction: no persistent alternate code cache
         * or unimplemented cross-engine invalidation contract in this proof. */
        Jit jit(config);
        callbacks.jit = &jit;
        std::copy_n(regs, 16, jit.Regs().begin());
        jit.SetCpsr(*cpsr);
        jit.Step();
        std::array<uint32_t, 16> expected;
        std::copy_n(regs, 16, expected.begin());
        expected[0] = move ? opcode & 255 : regs[0] + (opcode & 255);
        expected[15] += 4;
        if (callbacks.unsupported || jit.Cpsr() != *cpsr ||
            !std::equal(expected.begin(), expected.end(), jit.Regs().begin())) {
            return false;
        }
        std::copy_n(jit.Regs().begin(), 16, regs);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
