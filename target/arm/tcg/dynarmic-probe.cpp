/* SPDX-License-Identifier: GPL-2.0-or-later */
/* No QEMU headers/state/longjmp paths cross this C++ boundary. */
#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <dynarmic/interface/A32/a32.h>
#include "dynarmic-probe-abi.h"

namespace {
using namespace Dynarmic;
using namespace Dynarmic::A32;

struct Callbacks final : UserCallbacks {
    Jit *jit = nullptr;
    const LTDynarmicProbeStep *step = nullptr;
    bool unsupported = false;
    unsigned reads = 0;
    uint64_t ticks = 1;

    void reject() {
        unsupported = true;
        if (jit) { jit->HaltExecution(HaltReason::MemoryAbort); }
    }
    std::optional<uint32_t> MemoryReadCode(VAddr address) override {
        return step && address == step->pc ? std::optional<uint32_t>(step->opcode)
                                   : std::nullopt;
    }
    uint8_t MemoryRead8(VAddr) override { reject(); return 0; }
    uint16_t MemoryRead16(VAddr) override { reject(); return 0; }
    uint32_t MemoryRead32(VAddr address) override {
        if (!step || !step->has_read || address != step->read_address || reads++) {
            reject();
            return 0;
        }
        return step->read_value;
    }
    uint64_t MemoryRead64(VAddr) override { reject(); return 0; }
    void MemoryWrite8(VAddr, uint8_t) override { reject(); }
    void MemoryWrite16(VAddr, uint16_t) override { reject(); }
    void MemoryWrite32(VAddr, uint32_t) override { reject(); }
    void MemoryWrite64(VAddr, uint64_t) override { reject(); }
    void InterpreterFallback(VAddr, size_t) override { reject(); }
    void CallSVC(uint32_t) override { reject(); }
    void ExceptionRaised(VAddr, Exception) override { reject(); }
    void AddTicks(uint64_t count) override {
        ticks = count < ticks ? ticks - count : 0;
    }
    uint64_t GetTicksRemaining() override { return ticks; }
};
}

extern "C" bool lt_dynarmic_probe_execute(const LTDynarmicProbePlan *plan,
                                         uint32_t regs[16], uint32_t *cpsr)
{
    if (!plan || !plan->count || plan->count > LT_DYNARMIC_PROBE_MAX_STEPS) {
        return false;
    }
    try {
        Callbacks callbacks;
        UserConfig config{};
        config.callbacks = &callbacks;
        config.arch_version = ArchVersion::v6K;
        config.code_cache_size = 8 * 1024 * 1024;
        config.check_halt_on_memory_access = true;
        /* A fresh JIT per capsule avoids persistent invalidation claims.
         * The live QEMU caller currently always supplies count=1. */
        Jit jit(config);
        callbacks.jit = &jit;
        std::copy_n(plan->initial_regs, 16, jit.Regs().begin());
        jit.SetCpsr(plan->initial_cpsr);
        for (unsigned index = 0; index < plan->count; index++) {
            const LTDynarmicProbeStep &step = plan->steps[index];
            if (jit.Regs()[15] != step.pc || (step.pc & 3)) {
                return false;
            }
            callbacks.step = &step;
            callbacks.reads = 0;
            callbacks.ticks = 1;
            const auto halt = jit.Step();
            if (callbacks.unsupported || halt != HaltReason::Step ||
                callbacks.reads != (step.has_read ? 1u : 0u) ||
                jit.Cpsr() != step.expected_cpsr ||
                !std::equal(step.expected_regs, step.expected_regs + 16,
                            jit.Regs().begin())) {
                return false;
            }
        }
        /* Rejection never modifies outputs or live QEMU state. */
        std::copy_n(jit.Regs().begin(), 16, regs);
        *cpsr = jit.Cpsr();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

namespace {
/* Deliberately separate from the already tested single-step callbacks. No
 * live QEMU state/helper or callback enters this reusable context. Scalar
 * owner/code identities are compared only, never dereferenced here. */
struct BlockCallbacks final : UserCallbacks {
    Jit *jit = nullptr;
    const LTDynarmicProbePlan *plan = nullptr;
    bool unsupported = false;
    unsigned code_reads = 0;
    unsigned data_reads = 0;
    unsigned read_cursor = 0;
    uint64_t consumed = 0;
    uint64_t ticks = 0;

    void reject() {
        unsupported = true;
        if (jit) { jit->HaltExecution(HaltReason::MemoryAbort); }
    }
    std::optional<uint32_t> MemoryReadCode(VAddr address) override {
        code_reads++;
        if (plan) {
            for (unsigned i = 0; i < plan->count; i++) {
                if (plan->steps[i].pc == address) {
                    return plan->steps[i].opcode;
                }
            }
        }
        reject();
        return std::nullopt;
    }
    bool IsReadOnlyMemory(VAddr) override { return false; }
    uint8_t MemoryRead8(VAddr) override { reject(); return 0; }
    uint16_t MemoryRead16(VAddr) override { reject(); return 0; }
    uint32_t MemoryRead32(VAddr address) override {
        if (!plan) { reject(); return 0; }
        while (read_cursor < plan->count &&
               !plan->steps[read_cursor].has_read) {
            read_cursor++;
        }
        if (read_cursor == plan->count ||
            plan->steps[read_cursor].read_address != address) {
            reject();
            return 0;
        }
        data_reads++;
        return plan->steps[read_cursor++].read_value;
    }
    uint64_t MemoryRead64(VAddr) override { reject(); return 0; }
    void MemoryWrite8(VAddr, uint8_t) override { reject(); }
    void MemoryWrite16(VAddr, uint16_t) override { reject(); }
    void MemoryWrite32(VAddr, uint32_t) override { reject(); }
    void MemoryWrite64(VAddr, uint64_t) override { reject(); }
    void InterpreterFallback(VAddr, size_t) override { reject(); }
    void CallSVC(uint32_t) override { reject(); }
    void ExceptionRaised(VAddr, Exception) override { reject(); }
    void AddTicks(uint64_t count) override {
        if (!count || count > ticks) {
            reject();
            return;
        }
        consumed += count;
        ticks -= count;
        if (!ticks) {
            /* Request the host return at this real compiled-block boundary.
             * No chaining to another block is authorized by this capsule. */
            jit->HaltExecution(HaltReason::UserDefined1);
        }
    }
    uint64_t GetTicksRemaining() override { return ticks; }
};

struct BlockEngine {
    BlockCallbacks callbacks;
    std::unique_ptr<Jit> jit;
    LTDynarmicProbePlan previous{};
    bool valid = false;
    uint64_t runs = 0;
    uint64_t generation = 0;

    BlockEngine() {
        UserConfig config{};
        config.callbacks = &callbacks;
        config.arch_version = ArchVersion::v6K;
        config.code_cache_size = 8 * 1024 * 1024;
        config.check_halt_on_memory_access = true;
        config.always_little_endian = true;
        config.enable_cycle_counting = true;
        /* Keep safe IR optimization, but require dispatcher/cycle/halt
         * checks rather than a linked block or return-stack escape. */
        config.optimizations = all_safe_optimizations &
            ~(OptimizationFlag::BlockLinking |
              OptimizationFlag::ReturnStackBuffer |
              OptimizationFlag::FastDispatch);
        jit = std::make_unique<Jit>(config);
        callbacks.jit = jit.get();
    }
    bool matches(const LTDynarmicProbePlan &plan) const {
        if (!valid || previous.owner != plan.owner ||
            previous.count != plan.count ||
            previous.throughput != plan.throughput ||
            !std::equal(previous.context,
                        previous.context + LT_DYNARMIC_PROBE_CONTEXT_WORDS,
                        plan.context)) {
            return false;
        }
        for (unsigned i = 0; i < plan.count; i++) {
            if (previous.steps[i].pc != plan.steps[i].pc ||
                previous.steps[i].opcode != plan.steps[i].opcode ||
                previous.steps[i].code_identity != plan.steps[i].code_identity) {
                return false;
            }
        }
        return true;
    }
    void invalidate() {
        if (valid) {
            jit->ClearCache();
        }
        valid = false;
    }
};

/* Single vCPU/single host thread only. Allocation survives repeated eligible
 * blocks; every TCG handoff clears compiled code, not the backing allocation.
 * Thread-local destruction retires the allocation with the CPU host thread. */
thread_local std::unique_ptr<BlockEngine> block_engine;
}

extern "C" bool lt_dynarmic_probe_block(const LTDynarmicProbePlan *plan,
                                       uint32_t regs[16], uint32_t *cpsr,
                                       LTDynarmicBlockStats *stats)
{
    if (!plan || plan->count < 2 ||
        plan->count > LT_DYNARMIC_PROBE_MAX_STEPS) {
        return false;
    }
    try {
        if (!block_engine) {
            block_engine = std::make_unique<BlockEngine>();
        }
        BlockEngine &engine = *block_engine;
        const bool matching = engine.matches(*plan);
        if (!matching) {
            engine.jit->ClearCache();
            engine.generation++;
        }
        engine.previous = *plan;
        engine.valid = true;
        BlockCallbacks &callbacks = engine.callbacks;
        callbacks.plan = plan;
        callbacks.unsupported = false;
        callbacks.code_reads = callbacks.data_reads = callbacks.read_cursor = 0;
        callbacks.consumed = 0;
        callbacks.ticks = plan->count;
        /* Only this host thread manipulates this private JIT's halt flags;
         * no asynchronous external HaltExecution/ClearHalt race is possible. */
        engine.jit->ClearHalt(HaltReason::UserDefined1 |
                              HaltReason::MemoryAbort |
                              HaltReason::CacheInvalidation);
        std::copy_n(plan->initial_regs, 16, engine.jit->Regs().begin());
        engine.jit->SetCpsr(plan->initial_cpsr);
        const auto halt = engine.jit->Run();
        callbacks.plan = nullptr;
        const LTDynarmicProbeStep &last = plan->steps[plan->count - 1];
        unsigned expected_reads = 0;
        for (unsigned i = 0; i < plan->count; i++) {
            expected_reads += plan->steps[i].has_read;
        }
        bool state_valid;
        if (plan->throughput) {
            /* No C instruction interpreter in this trial. Validate structural
             * architectural boundaries; an external TCG differential harness
             * must check the exact changed register/NZCV/branch result. */
            const auto &actual = engine.jit->Regs();
            uint32_t allowed_flags = plan->writes_nzcv ? 0xf0000000u : 0;
            state_valid = !callbacks.data_reads &&
                !((engine.jit->Cpsr() ^ plan->initial_cpsr) & ~allowed_flags) &&
                (actual[15] == plan->terminal_taken ||
                 actual[15] == plan->terminal_fallthrough);
            for (unsigned i = 0; i < 15; i++) {
                if (!(plan->written_regs & (1u << i)) &&
                    actual[i] != plan->initial_regs[i]) {
                    state_valid = false;
                }
            }
        } else {
            state_valid = engine.jit->Cpsr() == last.expected_cpsr &&
                std::equal(last.expected_regs, last.expected_regs + 16,
                           engine.jit->Regs().begin());
        }
        if (callbacks.unsupported || halt != HaltReason::UserDefined1 ||
            callbacks.consumed != plan->count ||
            callbacks.data_reads != expected_reads || !state_valid) {
            engine.invalidate();
            return false;
        }
        std::copy_n(engine.jit->Regs().begin(), 16, regs);
        *cpsr = engine.jit->Cpsr();
        engine.runs++;
        *stats = {1, engine.runs, engine.generation, callbacks.code_reads,
                  callbacks.data_reads, matching && !callbacks.code_reads};
        return true;
    } catch (...) {
        /* Reject the snapshot with no architectural commit. Retire the JIT
         * instead of calling a potentially throwing cache method while an
         * exception is already in flight. No exception crosses the C ABI. */
        block_engine.reset();
        return false;
    }
}

extern "C" void lt_dynarmic_probe_block_invalidate(void)
{
    try {
        if (block_engine) {
            block_engine->invalidate();
        }
    } catch (...) {
        block_engine.reset();
    }
}
