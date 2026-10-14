#!/usr/bin/env python3
"""Exercise extracted production TB cache lookup/invalidation; no guest claim.

Only run the compiler after an allocated host slice-test slot. Exact production
functions/header hashes and compiler commands are retained in --output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def function(path, name):
    text = (ROOT / path).read_text()
    match = re.search(r'(?m)^(?:static[^\n]*\n)?(?:static )?(?:inline )?[^\n;{}]+\b' + re.escape(name) + r'\([^;]*?\)\s*\{', text)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cc', default='/usr/bin/cc')
p.add_argument('--prepare-only', action='store_true')
a = p.parse_args()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
a.output.mkdir(parents=True, exist_ok=False)
h = (ROOT / 'accel/tcg/tb-jmp-cache.h').read_text()
geometry = h[h.index('#ifdef QEMU_EXPERIMENTAL_TB_JMP_CACHE_BITS'):h.index('/*\n * Invalidated')]
cache = h[h.index('typedef struct CPUJumpCache'):h.index('} CPUJumpCache;') + len('} CPUJumpCache;')]
hash_h = (ROOT / 'accel/tcg/tb-hash.h').read_text()
hashing = hash_h[hash_h.index('#define TB_JMP_PAGE_BITS'):hash_h.index('\n#else')]
parts = [('accel/tcg/cpu-exec.c', 'tb_lookup_cmp'), ('accel/tcg/cpu-exec.c', 'tb_htable_lookup'),
         ('accel/tcg/cpu-exec.c', 'tb_lookup'), ('accel/tcg/cputlb.c', 'tb_jmp_cache_clear_page'),
         ('accel/tcg/translate-all.c', 'tcg_flush_jmp_cache'), ('accel/tcg/tb-maint.c', 'tb_jmp_cache_inval_tb')]
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t vaddr;
typedef uint64_t tb_page_addr_t;
struct rcu_head { void *unused; };
#define TARGET_PAGE_BITS 12
#define TARGET_PAGE_SIZE (1U << TARGET_PAGE_BITS)
#define TARGET_PAGE_ALIGN(x) (((x) + TARGET_PAGE_SIZE - 1) & ~(vaddr)(TARGET_PAGE_SIZE - 1))
#define CF_PCREL 1U
#define CF_INVALID 2U
#define unlikely(x) (x)
#define likely(x) (x)
#define tcg_debug_assert assert
#define qatomic_read(p) (*(p))
#define qatomic_set(p,v) (*(p)=(v))
typedef struct TranslationBlock { vaddr pc, cs_base; uint32_t flags,cflags;
    tb_page_addr_t phys0,phys1; } TranslationBlock;
#define tb_cflags(t) ((t)->cflags)
#define tb_page_addr0(t) ((t)->phys0)
#define tb_page_addr1(t) ((t)->phys1)
'''
state = r'''
typedef struct CPUState { CPUJumpCache *tb_jmp_cache; } CPUState;
typedef struct CPUArchState { int unused; } CPUArchState;
typedef struct { vaddr pc,cs_base; uint32_t flags,cflags; } TCGTBCPUState;
struct tb_desc { TCGTBCPUState s; CPUArchState *env; tb_page_addr_t page_addr0; };
static bool it_acct_on;
static CPUState *one_cpu;
#define CPU_FOREACH(cpu) for ((cpu)=one_cpu; (cpu); (cpu)=NULL)
static CPUArchState env;
static CPUArchState *cpu_env(CPUState *cpu) { (void)cpu; return &env; }
static TranslationBlock *candidates[8];
static tb_page_addr_t second_page = 0x900000;
static tb_page_addr_t get_page_addr_code(CPUArchState *e,vaddr pc) {
    (void)e; return pc==0x2000 ? second_page : 0x800000+pc;
}
static unsigned tb_hash_func(tb_page_addr_t p,vaddr pc,uint32_t f,vaddr base,uint32_t c) {
    (void)p;(void)pc;(void)f;(void)base;(void)c;return 0;
}
static struct { int htable; } tb_ctx;
static TranslationBlock *qht_lookup_custom(int *table,void *desc,unsigned hash,
                                           bool (*cmp)(const void *,const void *)) {
    (void)table;(void)hash;
    for (unsigned i=0;i<8;i++) if (candidates[i] && cmp(candidates[i],desc)) return candidates[i];
    return NULL;
}
'''
main = r'''
static TranslationBlock make_tb(vaddr pc,uint32_t flags) {
    return (TranslationBlock){.pc=pc,.flags=flags,.phys0=0x800000+pc,.phys1=(tb_page_addr_t)-1};
}
static TCGTBCPUState state_of(TranslationBlock *t) {
    return (TCGTBCPUState){.pc=t->pc,.cs_base=t->cs_base,.flags=t->flags,.cflags=t->cflags};
}
int main(void) {
    CPUJumpCache *jc=calloc(1,sizeof(*jc)); assert(jc);
    CPUState cpu={.tb_jmp_cache=jc}; one_cpu=&cpu;
    for (vaddr page=0;page<0x100000;page+=TARGET_PAGE_SIZE) {
        unsigned base=tb_jmp_cache_hash_page(page);
        assert(base+TB_JMP_PAGE_SIZE<=TB_JMP_CACHE_SIZE);
        for(unsigned off=0;off<TARGET_PAGE_SIZE;off+=2) {
            unsigned hash=tb_jmp_cache_hash_func(page+off);
            assert(hash>=base && hash<base+TB_JMP_PAGE_SIZE);
        }
    }
    TranslationBlock t=make_tb(0x1100,0), collision=make_tb(0,0);
    for (vaddr pc=0x3000; pc<0x4000000; pc+=4) {
        if (tb_jmp_cache_hash_func(pc)==tb_jmp_cache_hash_func(t.pc)) {collision=make_tb(pc,0);break;}
    }
    assert(collision.pc);
    candidates[0]=&t; candidates[1]=&collision;it_acct_on=true;
    assert(tb_lookup(&cpu,state_of(&t))==&t);
    assert(tb_lookup(&cpu,state_of(&t))==&t);
    assert(tb_lookup(&cpu,state_of(&collision))==&collision);
    assert(tb_lookup(&cpu,state_of(&t))==&t);
#ifdef QEMU_EXPERIMENTAL_TB_CACHE_STATS
    assert(jc->empty_misses==1 && jc->fast_hits==1 && jc->pc_misses==2 && jc->slow_hits==3);
#endif
    /* Same virtual PC, different execution flags must revalidate in QHT. */
    TranslationBlock other=t; other.flags=3; candidates[2]=&other;
    assert(tb_lookup(&cpu,state_of(&other))==&other);
    assert(tb_lookup(&cpu,state_of(&t))==&t);
    TCGTBCPUState wrong=state_of(&t); wrong.cs_base=1;
    assert(tb_lookup(&cpu,wrong)==NULL);
    wrong=state_of(&t);wrong.cflags=4;assert(tb_lookup(&cpu,wrong)==NULL);
#ifdef QEMU_EXPERIMENTAL_TB_CACHE_STATS
    assert(jc->state_misses==4 && jc->slow_absent==2);
#endif
    /* Non-current colliding TB invalidation must leave current entry valid. */
    assert(tb_lookup(&cpu,state_of(&collision))==&collision);
    tb_jmp_cache_inval_tb(&t);
    assert(tb_lookup(&cpu,state_of(&collision))==&collision);
    tb_jmp_cache_inval_tb(&collision);
    assert(jc->array[tb_jmp_cache_hash_func(collision.pc)].tb==NULL);
    /* Physical remap becomes visible after actual page invalidation. */
    assert(tb_lookup(&cpu,state_of(&t))==&t);
    TranslationBlock remap=t;remap.phys0+=0x100000;candidates[0]=&remap;
    tb_jmp_cache_clear_page(&cpu,t.pc);
    assert(tb_lookup(&cpu,state_of(&t))==NULL); /* old physical mapping cannot match */
    candidates[0]=&t;
    assert(tb_lookup(&cpu,state_of(&t))==&t);
    /* Cross-page lookup must check the second physical page after flush. */
    TranslationBlock cross=make_tb(0x1ffc,0);cross.phys1=second_page;candidates[3]=&cross;
    assert(tb_lookup(&cpu,state_of(&cross))==&cross);
    second_page+=0x100000;tb_jmp_cache_clear_page(&cpu,cross.pc);
    assert(tb_lookup(&cpu,state_of(&cross))==NULL);
    cross.phys1=second_page;assert(tb_lookup(&cpu,state_of(&cross))==&cross);
    tcg_flush_jmp_cache(&cpu);
    for(unsigned i=0;i<TB_JMP_CACHE_SIZE;i++)assert(jc->array[i].tb==NULL);
    assert(tb_lookup(&cpu,state_of(&t))==&t);
    /* PC-relative TB invalidation uses the production all-cache path. */
    TranslationBlock relative=t;relative.cflags=CF_PCREL;
    tb_jmp_cache_inval_tb(&relative);
    for(unsigned i=0;i<TB_JMP_CACHE_SIZE;i++)assert(jc->array[i].tb==NULL);
#ifdef QEMU_EXPERIMENTAL_TB_CACHE_STATS
    uint64_t fast=jc->fast_hits,empty=jc->empty_misses,slow=jc->slow_hits;
#endif
    it_acct_on=false;assert(tb_lookup(&cpu,state_of(&t))==&t);assert(tb_lookup(&cpu,state_of(&t))==&t);
#ifdef QEMU_EXPERIMENTAL_TB_CACHE_STATS
    assert(jc->fast_hits==fast && jc->empty_misses==empty && jc->slow_hits==slow);
#endif
    printf("PASS production lookup/physical+state rejection/collision/page+full+TB invalidation; entries=%u bytes=%zu\n",TB_JMP_CACHE_SIZE,sizeof(*jc));
    free(jc);return 0;
}
'''
unit = '\n'.join([preamble, geometry, cache, hashing, state,
                  '\n'.join(function(*x) for x in parts), main])
c = a.output/'production-cache.c'
c.write_text(unit)
receipt = {'production_inputs': {x: hashlib.sha256((ROOT/x).read_bytes()).hexdigest()
                               for x in {p for p,n in parts}|{'accel/tcg/tb-jmp-cache.h','accel/tcg/tb-hash.h'}},
           'slice_sha256': hashlib.sha256(c.read_bytes()).hexdigest(), 'commands': []}
if a.prepare_only:
    (a.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Prepared exact production slice; no compiler invoked')
    raise SystemExit(0)
for name,defs in [('production',[]),('control',['-DQEMU_EXPERIMENTAL_TB_CACHE_STATS','-DQEMU_EXPERIMENTAL_TB_JMP_CACHE_BITS=12']),
                  ('large',['-DQEMU_EXPERIMENTAL_TB_CACHE_STATS','-DQEMU_EXPERIMENTAL_TB_JMP_CACHE_BITS=14'])]:
    # Production uses the all-ones sentinel -1 with unsigned tb_page_addr_t;
    # retain the actual source and suppress only that inherited warning.
    cmd=[a.cc,'-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined',*defs,str(c),'-o',str(a.output/name)]
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=30); receipt['commands'].append({'argv':cmd,'exit':r.returncode})
    (a.output/(name+'-compile.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
    r=subprocess.run([str(a.output/name)],capture_output=True,text=True,timeout=10)
    (a.output/(name+'-run.log')).write_text(r.stdout+r.stderr);receipt['commands'].append({'argv':[str(a.output/name)],'exit':r.returncode});assert r.returncode==0,r.stderr
    print(r.stdout,end='')
# Unsupported geometry must refuse at preprocessing, not silently resize.
cmd=[a.cc,'-fsyntax-only','-DQEMU_EXPERIMENTAL_TB_JMP_CACHE_BITS=13',str(c)]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=10);assert r.returncode and 'supports only 12 or 14' in r.stderr
(a.output/'invalid-geometry.log').write_text(r.stdout+r.stderr)
# Prove the physical-remap check catches a missing production page flush.
bad = a.output/'negative-no-page-flush.c'
original = function('accel/tcg/cputlb.c', 'tb_jmp_cache_clear_page')
bad.write_text(unit.replace(original, 'static void tb_jmp_cache_clear_page(CPUState *cpu, vaddr page_addr) {(void)cpu;(void)page_addr;}'))
cmd=[a.cc,'-std=gnu11','-O2',str(bad),'-o',str(a.output/'negative-no-page-flush')]
r=subprocess.run(cmd,capture_output=True,text=True,timeout=30);assert r.returncode==0,r.stderr
r=subprocess.run([str(a.output/'negative-no-page-flush')],capture_output=True,text=True,timeout=10)
assert r.returncode==-signal.SIGABRT and 'tb_lookup(&cpu,state_of(&t))==NULL' in r.stderr,r.stderr
(a.output/'negative-no-page-flush.log').write_text(r.stdout+r.stderr)
receipt['negative_no_page_flush_exit']=r.returncode
(a.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
