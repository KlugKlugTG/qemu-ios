/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Unit tests compile the real codec against real QEMU target/TCG headers.
 * The allocation/CPU-name stubs below replace engine infrastructure, never an
 * instruction translator or a guest acceptance oracle. No iOS run is claimed.
 */
#include "../../target/arm/tcg/ir-cache.c"

#if defined(__linux__) && TCG_TARGET_REG_BITS == 64 && \
    !defined(CONFIG_USER_ONLY) && !defined(CONFIG_PLUGIN)

__thread TCGContext *tcg_ctx;
TCGv_env tcg_env;
uintptr_t tcg_splitwx_diff;
const TargetPageBits target_page = { true, 10, (uint64_t)-1024 };
const TCGOpDef tcg_op_defs[] = {
#define DEF(s, o, i, c, flags) { #s, o, i, c, o + i + c, flags },
#include "tcg/tcg-opc.h"
};
const size_t tcg_op_defs_max = ARRAY_SIZE(tcg_op_defs);
static GPtrArray *allocations;
static const char *cpu_name = "arm1176-arm-cpu";
static bool optional_supported = true;

Object *object_dynamic_cast_assert(Object *obj, const char *typename,
                                   const char *file, int line, const char *func)
{
    return obj;
}
const char *object_get_typename(const Object *obj) { return cpu_name; }
bool tcg_op_supported(TCGOpcode opc, TCGType type, unsigned flags)
{
    return opc != INDEX_op_ctpop || optional_supported;
}
void *tcg_malloc_internal(TCGContext *s, int size)
{
    void *p = g_malloc0(size);
    g_ptr_array_add(allocations, p);
    return p;
}
TCGOp *tcg_emit_op(TCGOpcode opc, unsigned nargs)
{
    TCGOp *o = tcg_malloc_internal(tcg_ctx,
                                  sizeof(TCGOp) + MAX(4, nargs) * sizeof(TCGArg));
    o->opc = opc; o->nargs = MAX(4, nargs);
    QTAILQ_INSERT_TAIL(&tcg_ctx->ops, o, link);
    tcg_ctx->nb_ops++;
    return o;
}
TCGLabel *gen_new_label(void)
{
    TCGLabel *l = tcg_malloc_internal(tcg_ctx, sizeof(TCGLabel));
    l->id = tcg_ctx->nb_labels++;
    QSIMPLEQ_INIT(&l->branches); QSIMPLEQ_INIT(&l->relocs);
    QSIMPLEQ_INSERT_TAIL(&tcg_ctx->labels, l, next);
    return l;
}

static TCGContext *new_context(void)
{
    TCGContext *s = g_new0(TCGContext, 1);
    tcg_ctx = s;
    s->nb_globals = s->nb_temps = 2;
    s->temps[0].kind = TEMP_FIXED;
    s->temps[0].type = s->temps[0].base_type = TCG_TYPE_I64;
    s->temps[1].kind = TEMP_GLOBAL;
    s->temps[1].type = s->temps[1].base_type = TCG_TYPE_I32;
    s->temps[1].mem_base = &s->temps[0];
    s->addr_type = TCG_TYPE_I32;
    tcg_env = temp_tcgv_ptr(&s->temps[0]);
    QTAILQ_INIT(&s->ops); QTAILQ_INIT(&s->free_ops); QSIMPLEQ_INIT(&s->labels);
    return s;
}

static IRBlock *fixture(void)
{
    IRBlock *b = g_new0(IRBlock, 1);
    b->globals = 2; b->temps = 6; b->labels = 1;
    b->size = 12; b->icount = 3; b->ops = 12;
    b->temp[2] = (IRTemp){ TEMP_CONST, 0, 0, 0, 1 };
    b->temp[3] = (IRTemp){ TEMP_CONST, 0, 0, 0, 2 };
    b->temp[4] = (IRTemp){ TEMP_EBB, 0, 0, 0, 0 };
    b->temp[5] = (IRTemp){ TEMP_CONST, 0, 0, 0, 0 };
    b->op[0] = (IROp){ INDEX_op_ld, 0, 0, 3,
                       { 4, 0, (uint64_t)-(int64_t)sizeof(CPUState) } };
    b->op[1] = (IROp){ INDEX_op_brcond, 0, 0, 4, { 4, 5, TCG_COND_LT, 0 } };
    b->op[2] = (IROp){ INDEX_op_insn_start, 0, 0, 3, { 0x1000, 0, 0 } };
    b->op[3] = (IROp){ INDEX_op_mov, 0, 0, 2, { 1, 2 } };
    b->op[4] = (IROp){ INDEX_op_insn_start, 0, 0, 3, { 0x1004, 0, 0 } };
    b->op[5] = (IROp){ INDEX_op_add, 0, 0, 3, { 1, 1, 3 } };
    b->op[6] = (IROp){ INDEX_op_insn_start, 0, 0, 3, { 0x1008, 0, 0 } };
    b->op[7] = (IROp){ INDEX_op_goto_tb, 0, 0, 1, { 0 } };
    b->op[8] = (IROp){ INDEX_op_exit_tb, 0, 0, 1, { 1 } };
    b->op[9] = (IROp){ INDEX_op_set_label, 0, 0, 1, { 0 } };
    b->op[10] = (IROp){ INDEX_op_mb, 0, 0, 1, { 1 } };
    b->op[11] = (IROp){ INDEX_op_exit_tb, 0, 0, 1, { 4 } };
    return b;
}

static TranslationBlock new_tb(void)
{
    TranslationBlock tb = { 0 };
    tb.pc = 0x1000;
    tb_set_page_addr0(&tb, 0x2000);
    tb_set_page_addr1(&tb, -1);
    return tb;
}

static const uint8_t code[12] = {
    1, 0, 0xa0, 0xe3, /* mov r0,#1 */
    2, 0, 0x80, 0xe2, /* add r0,r0,#2 */
    0, 0, 0, 0xea,    /* b */
};

/* Minimal independent oracle for the fixture's executable TCG operations.
 * It follows real reconstructed label pointers and observes TB exit pointers. */
static uintptr_t run_fixture(int interrupt_count, uint32_t *r0)
{
    uint32_t values[TCG_MAX_TEMPS] = { 0 };
    TCGOp *o;
    for (int i = 0; i < tcg_ctx->nb_temps; i++) {
        if (tcg_ctx->temps[i].kind == TEMP_CONST) {
            values[i] = tcg_ctx->temps[i].val;
        }
    }
    values[1] = *r0;
    for (o = QTAILQ_FIRST(&tcg_ctx->ops); o; o = QTAILQ_NEXT(o, link)) {
        unsigned a = (o->opc == INDEX_op_ld || o->opc == INDEX_op_brcond ||
                      o->opc == INDEX_op_mov || o->opc == INDEX_op_add) ?
                      arg_temp(o->args[0]) - tcg_ctx->temps : 0;
        switch (o->opc) {
        case INDEX_op_ld: values[a] = interrupt_count; break;
        case INDEX_op_brcond:
            if ((int32_t)values[a] < 0) {
                TCGLabel *l = arg_label(o->args[3]);
                while (o && !(o->opc == INDEX_op_set_label &&
                       arg_label(o->args[0]) == l)) {
                    o = QTAILQ_NEXT(o, link);
                }
                g_assert_nonnull(o);
            }
            break;
        case INDEX_op_mov:
            values[a] = values[arg_temp(o->args[1]) - tcg_ctx->temps]; break;
        case INDEX_op_add:
            values[a] = values[arg_temp(o->args[1]) - tcg_ctx->temps] +
                        values[arg_temp(o->args[2]) - tcg_ctx->temps]; break;
        case INDEX_op_exit_tb:
            *r0 = values[1]; return o->args[0];
        default: break;
        }
    }
    g_assert_not_reached();
}

static void roundtrip_relocation(void)
{
    TCGContext *old = new_context();
    TranslationBlock old_tb = new_tb(), new_tb_value = new_tb();
    IRBlock *original = fixture();
    replay_ir(original, &old_tb);
    IRBlock *captured = capture_ir(&old_tb, code);
    g_assert_nonnull(captured);
    GBytes *bytes = encode_ir(captured);
    TCGContext *fresh = new_context();
    g_assert_true(old != fresh);
    IRBlock *parsed = decode_ir(bytes, fresh->nb_globals, 512, 1024, 0x1000);
    g_assert_nonnull(parsed);
    replay_ir(parsed, &new_tb_value);
    for (unsigned i = 0; i < 100; i++) {
        uint32_t r0 = i;
        g_assert_cmphex(run_fixture(0, &r0), ==, (uintptr_t)&new_tb_value);
        g_assert_cmpuint(r0, ==, 3);
        r0 = i;
        g_assert_cmphex(run_fixture(-1, &r0), ==,
                        (uintptr_t)&new_tb_value + TB_EXIT_REQUESTED);
        g_assert_cmpuint(r0, ==, i);
    }
    TCGLabel *l = QSIMPLEQ_FIRST(&fresh->labels);
    g_assert_nonnull(l); g_assert_true(l->present);
    g_assert_false(QSIMPLEQ_EMPTY(&l->branches));
    g_assert_true(QSIMPLEQ_FIRST(&l->branches)->op !=
                   QSIMPLEQ_FIRST(&QSIMPLEQ_FIRST(&old->labels)->branches)->op);
    g_free(parsed); g_bytes_unref(bytes); g_free(captured); g_free(original);
    g_free(fresh); g_free(old);
}

static void malformed_ir(void)
{
    TCGContext *s = new_context();
    IRBlock *b = fixture();
    for (unsigned mutation = 0; mutation < 18; mutation++) {
        IRBlock *bad = g_memdup(b, sizeof(*b));
        switch (mutation) {
        case 0: bad->globals++; break;
        case 1: bad->temps = TCG_MAX_TEMPS + 1; break;
        case 2: bad->labels = IR_MAX_LABELS + 1; break;
        case 3: bad->size = 1028; break;
        case 4: bad->icount = 512; break;
        case 5: bad->temp[2].type = TCG_TYPE_I64; break;
        case 6: bad->temp[2].kind = TEMP_FIXED; break;
        case 7: bad->temp[2].value = UINT64_C(0x123456789abcdef0); break;
        case 8: bad->op[5].opc = INDEX_op_call; break;
        case 9: bad->op[5].p1 = TCG_TYPE_I64; break;
        case 10: bad->op[5].args[0] = 500; break;
        case 11: bad->op[1].args[3] = 1; break;
        case 12: bad->op[1].args[2] = 1234; break;
        case 13: bad->op[8].args[0] = 3; break;
        case 14: bad->op[0].args[1] = 1; break;
        case 15: bad->op[0].args[2] = UINT64_C(0x7fffffffffffffff); break;
        case 16: bad->op[2].args[0]++; break;
        case 17: bad->op[5].args[0] = 2; break;
        }
        /* Encoder bounds intentionally rely on a locally captured valid
         * object; malformed count fields are patched after valid encoding. */
        if (mutation == 1 || mutation == 2) {
            GBytes *good = encode_ir(b);
            size_t len; const uint8_t *p = g_bytes_get_data(good, &len);
            uint8_t *copy = g_memdup(p, len);
            uint64_t v = GUINT64_TO_LE(mutation == 1 ? bad->temps : bad->labels);
            memcpy(copy + (mutation == 1 ? 16 : 24), &v, 8);
            GBytes *bytes = g_bytes_new_take(copy, len);
            g_assert_null(decode_ir(bytes, 2, 512, 1024, 0x1000));
            g_bytes_unref(bytes); g_bytes_unref(good);
        } else {
            GBytes *bytes = encode_ir(bad);
            g_assert_null(decode_ir(bytes, 2, 512, 1024, 0x1000));
            g_bytes_unref(bytes);
        }
        g_free(bad);
        g_assert_cmpint(s->nb_temps, ==, 2);
        g_assert_true(QTAILQ_EMPTY(&s->ops));
    }
    GBytes *bytes = encode_ir(b);
    size_t len; const uint8_t *p = g_bytes_get_data(bytes, &len);
    for (size_t cut = 0; cut < len; cut++) {
        GBytes *shortened = g_bytes_new(p, cut);
        g_assert_null(decode_ir(shortened, 2, 512, 1024, 0x1000));
        g_bytes_unref(shortened);
    }
    g_bytes_unref(bytes); g_free(b); g_free(s);
}

static void current_context_key(void)
{
    TCGContext *s = new_context();
    ARMCPU *cpu = g_new0(ARMCPU, 1);
    TranslationBlock tb = new_tb();
    uint8_t page[1024] = { 0 }, a[32], b[32];
    memcpy(page, code, sizeof(code));
    g_assert_true(make_key(CPU(cpu), &tb, 512, tb.pc, page, a));
#define DIFFERENT(statement, restore) do { \
    statement; \
    g_assert_true(make_key(CPU(cpu), &tb, 512, tb.pc, page, b)); \
    g_assert_cmpint(memcmp(a, b, 32), !=, 0); \
    restore; \
} while (0)
    DIFFERENT(page[0] ^= 1, page[0] ^= 1);
    DIFFERENT(page[1023] ^= 1, page[1023] ^= 1);
    DIFFERENT(tb.flags ^= 2, tb.flags ^= 2);
    DIFFERENT(tb.cs_base ^= 1, tb.cs_base ^= 1);
    DIFFERENT(tb.cflags ^= CF_NOIRQ, tb.cflags ^= CF_NOIRQ);
    DIFFERENT(cpu->env.features ^= 1, cpu->env.features ^= 1);
    DIFFERENT(cpu->isar.idregs[0] ^= 1, cpu->isar.idregs[0] ^= 1);
    DIFFERENT(cpu->idle_loop_pc ^= 4, cpu->idle_loop_pc ^= 4);
    DIFFERENT(s->temps[1].mem_offset++, s->temps[1].mem_offset--);
    DIFFERENT(engine_hash[0] ^= 1, engine_hash[0] ^= 1);
    DIFFERENT(optional_supported = false, optional_supported = true);
#undef DIFFERENT
    g_assert_true(make_key(CPU(cpu), &tb, 1, tb.pc, page, b));
    g_assert_cmpint(memcmp(a, b, 32), !=, 0);
    cpu_name = "cortex-a8-arm-cpu";
    g_assert_false(make_key(CPU(cpu), &tb, 512, tb.pc, page, b));
    cpu_name = "arm1176-arm-cpu";
    tb.cflags |= CF_BP_PAGE;
    g_assert_false(make_key(CPU(cpu), &tb, 512, tb.pc, page, b));
    tb.cflags = CF_PARALLEL;
    g_assert_false(make_key(CPU(cpu), &tb, 512, tb.pc, page, b));
    tb.cflags = 0;
    tb_set_page_addr0(&tb, -1);
    g_assert_false(make_key(CPU(cpu), &tb, 512, tb.pc, page, b));
    g_free(cpu); g_free(s);
}

static void reject_pointer_and_helper(void)
{
    TCGContext *s = new_context();
    TranslationBlock tb = new_tb();
    IRBlock *b = fixture();
    replay_ir(b, &tb);
    s->temps[2].type = s->temps[2].base_type = TCG_TYPE_I64;
    s->temps[2].val = (intptr_t)&tb;
    g_assert_null(capture_ir(&tb, code));
    s->temps[2].type = s->temps[2].base_type = TCG_TYPE_I32;
    s->temps[2].val = 1;
    tcg_emit_op(INDEX_op_call, 4);
    g_assert_null(capture_ir(&tb, code));
    g_free(b); g_free(s);
    uint8_t cpreg[] = { 0x10, 0x0f, 0x11, 0xee };
    g_assert_false(supported_instructions(cpreg, sizeof(cpreg)));
    g_assert_false(supported_instructions(code, sizeof(code) - 1));
}

int main(int argc, char **argv)
{
    allocations = g_ptr_array_new_with_free_func(g_free);
    if (argc == 3 && (!strcmp(argv[1], "--produce-ir") ||
                     !strcmp(argv[1], "--consume-ir"))) {
        g_setenv("LT_TCG_IR_CACHE_ENABLE", "experimental-v1", true);
        g_setenv("LT_TCG_IR_CACHE", argv[2], true);
        TCGContext *s = new_context();
        ARMCPU *cpu = g_new0(ARMCPU, 1);
        TranslationBlock tb = new_tb();
        uint8_t page[1024] = { 0 };
        ARMTcgIRCacheKey key;
        memcpy(page, code, sizeof(code));
        bool hit = arm_tcg_ir_cache_load(CPU(cpu), &tb, 512, tb.pc, page, &key);
        if (!strcmp(argv[1], "--produce-ir")) {
            g_assert_false(hit);
            g_assert_true(key.eligible);
            IRBlock *b = fixture();
            replay_ir(b, &tb);
            arm_tcg_ir_cache_save(CPU(cpu), &tb, tb.pc, page, &key);
            g_assert_cmpuint(stored, ==, 1);
            g_free(b);
        } else {
            g_assert_true(hit);
            g_assert_cmpuint(hits, ==, 1);
            uint32_t r0 = 99;
            g_assert_cmphex(run_fixture(0, &r0), ==, (uintptr_t)&tb);
            g_assert_cmpuint(r0, ==, 3);
        }
        g_free(cpu); g_free(s); g_ptr_array_unref(allocations);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--disabled")) {
        g_unsetenv("LT_TCG_IR_CACHE_ENABLE");
        g_setenv("LT_TCG_IR_CACHE", argv[2], true);
        ARMTcgIRCacheKey key;
        g_assert_false(arm_tcg_ir_cache_load(NULL, NULL, 0, 0, NULL, &key));
        g_assert_false(key.eligible);
        g_assert_null(store);
        g_ptr_array_unref(allocations);
        return 0;
    }
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/arm-ir/relocation-and-interrupts", roundtrip_relocation);
    g_test_add_func("/arm-ir/adversarial-and-all-truncations", malformed_ir);
    g_test_add_func("/arm-ir/current-bytes-build-context", current_context_key);
    g_test_add_func("/arm-ir/reject-pointers-helpers", reject_pointer_and_helper);
    int result = g_test_run();
    g_ptr_array_unref(allocations);
    return result;
}
#else
#error This unit harness requires Linux x86_64/ARM64 system-mode without plugins.
#endif
