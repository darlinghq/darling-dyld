/*
 * Copyright (C) 2026 Lubos Dolezel
 *
 * Host test (runs on any architecture) of the ARM64 TSD read translation
 * helpers, dyld3/shared-cache/dyld_tsd_rewrite.h. Instruction encodings are
 * checked against an independent decoder written from the Arm Architecture
 * Reference Manual for A-profile (DDI 0487): MRS (register), LDR (immediate,
 * unsigned offset), CBNZ, B and UDF. A tiny interpreter then runs the veneers
 * to check what they do: read the native TLS slot, return after the replaced
 * instruction when it is set, trap through the private UDF when it is not, and
 * leave the condition flags alone. The CPU number veneers are run the same
 * way: they return the rseq CPU number zero-extended in the read's register.
 *
 * cc -I dyld3/shared-cache tests/tsd-rewrite.c -o t && ./t
 */
#include <stdio.h>
#include <string.h>
#include "dyld_tsd_rewrite.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

/* ---- interpreter for exactly the instructions a veneer uses ------------- */
struct cpu { uint64_t x[32]; uintptr_t pc; uint64_t nzcv; uint64_t tp; uint64_t slot_value; unsigned long slot_off; int trapped; uint32_t trap_word;
             uint32_t cpu_number; long rseq_off; };

/* returns 0 to continue, 1 when pc left the veneer (branch to `exit_pc`) */
static int step(struct cpu* c, const uint32_t* base, uintptr_t base_pc, uintptr_t exit_pc, int* ok)
{
    if (c->pc == exit_pc) return 1;
    uint32_t insn = base[(c->pc - base_pc) / 4];
    unsigned rt = insn & 31, rn = (insn >> 5) & 31;
    if ((insn & 0xFFFFFFE0u) == 0xD53BD040u) {            /* mrs Xt, TPIDR_EL0 */
        if (rt != 31) c->x[rt] = c->tp;
        c->pc += 4;
    } else if ((insn >> 22) == 0x3E5) {                     /* ldr Xt, [Xn, #imm12*8] */
        uint64_t off = (uint64_t)((insn >> 10) & 0xFFF) * 8;
        uint64_t addr = c->x[rn] + off;
        if (addr != c->tp + c->slot_off) { *ok = 0; return 1; }
        if (rt != 31) c->x[rt] = c->slot_value;
        c->pc += 4;
    } else if ((insn >> 22) == 0x2E5 || ((insn >> 21) == 0x5C2 && ((insn >> 10) & 3) == 0)) {
        /* ldr Wt, [Xn, #imm12*4] / ldur Wt, [Xn, #simm9]: the rseq CPU number */
        int64_t off;
        if ((insn >> 22) == 0x2E5) off = (int64_t)((insn >> 10) & 0xFFF) * 4;
        else { off = (insn >> 12) & 0x1FF; if (off >= 256) off -= 512; }
        if (c->x[rn] + (uint64_t)off != c->tp + (uint64_t)c->rseq_off) { *ok = 0; return 1; }
        if (rt != 31) c->x[rt] = c->cpu_number;     /* 32-bit loads zero-extend */
        c->pc += 4;
    } else if ((insn >> 23) == 0x1A5) {                     /* movz Xd, #imm16, lsl #hw*16 */
        if (rt != 31) c->x[rt] = (uint64_t)((insn >> 5) & 0xFFFF) << (16 * ((insn >> 21) & 3));
        c->pc += 4;
    } else if ((insn >> 24) == 0xB5) {                      /* cbnz Xt, imm19 */
        int32_t imm19 = (int32_t)((insn >> 5) & 0x7FFFF);
        if (imm19 & 0x40000) imm19 -= 0x80000;
        c->pc = c->x[rt] != 0 ? c->pc + (int64_t)imm19 * 4 : c->pc + 4;
    } else if ((insn >> 26) == 5) {                         /* b imm26 */
        int32_t imm26 = (int32_t)(insn & 0x3FFFFFF);
        if (imm26 & 0x2000000) imm26 -= 0x4000000;
        c->pc += (int64_t)imm26 * 4;
    } else if ((insn >> 16) == 0) {                         /* udf: the private trap */
        c->trapped = 1; c->trap_word = insn;
        return 1;
    } else { *ok = 0; return 1; }
    return 0;
}

static void test_encodings(void)
{
    int ok = 1;
    for (uint32_t r = 0; r < 32; r++) {
        uint32_t reg = 99;
        ok &= dyld_tsd_is_tpidrro_read(DYLD_A64_MRS_TPIDRRO_EL0 | r, &reg) && reg == r;
        ok &= !dyld_tsd_is_tpidrro_read(DYLD_A64_MRS_TPIDR_EL0 | r, &reg);
    }
    CHECK(ok);
    uint32_t reg;
    CHECK(!dyld_tsd_is_tpidrro_read(0xD53BD07Fu | 0x0100, &reg));          /* another register field */
    CHECK(!dyld_tsd_is_tpidrro_read(DYLD_A64_NOP, &reg));
    CHECK(dyld_tsd_slot_offset_valid(16) && dyld_tsd_slot_offset_valid(32760) && dyld_tsd_slot_offset_valid(64));
    CHECK(!dyld_tsd_slot_offset_valid(0) && !dyld_tsd_slot_offset_valid(8) && !dyld_tsd_slot_offset_valid(32768)
          && !dyld_tsd_slot_offset_valid(66) && !dyld_tsd_slot_offset_valid(~0UL));
}

static void test_branch(void)
{
    uint32_t enc;
    CHECK(dyld_tsd_branch(0x1000, 0x1010, &enc) && enc == 0x14000004u);
    CHECK(dyld_tsd_branch(0x1010, 0x1000, &enc) && enc == 0x17FFFFFCu);
    CHECK(dyld_tsd_branch(0, (1ULL << 27) - 4, &enc) && enc == 0x15FFFFFFu);
    CHECK(!dyld_tsd_branch(0, (1ULL << 27), &enc));
    CHECK(dyld_tsd_branch((1ULL << 27), 0, &enc) && enc == 0x16000000u);
    CHECK(!dyld_tsd_branch((1ULL << 27) + 4, 0, &enc));
    CHECK(!dyld_tsd_branch(0x1000, 0x1002, &enc));
}

static void run_veneer(uint32_t reg, unsigned long slot_off, uint64_t slot_value, int expect_trap)
{
    uint32_t code[DYLD_TSD_VENEER_WORDS];
    uintptr_t veneer = 0x7000000000ull, ret = 0x7000100000ull + 4;   /* return point 1 MiB away */
    CHECK(dyld_tsd_build_veneer(code, veneer, ret, reg, slot_off));
    struct cpu c;
    memset(&c, 0, sizeof c);
    c.pc = veneer; c.tp = 0xABCD0000ull; c.slot_off = slot_off; c.slot_value = slot_value;
    c.nzcv = 0xA0000000ull;
    for (int i = 0; i < 32; i++) c.x[i] = 0x1111111100000000ull + i;
    uint64_t before[32]; memcpy(before, c.x, sizeof before);
    int ok = 1, done = 0;
    for (int n = 0; n < 16 && !done; n++) done = step(&c, code, veneer, ret, &ok);
    CHECK(ok && done);
    CHECK(c.trapped == expect_trap);
    if (!expect_trap) {
        CHECK(c.pc == ret);
        CHECK(c.x[reg] == slot_value);
    } else {
        CHECK(c.trap_word == (DYLD_A64_UDF_TSD_TRAP | reg));
        CHECK(c.pc == veneer + 12);                  /* at the udf */
    }
    CHECK(c.nzcv == 0xA0000000ull);                  /* no flag change */
    for (int i = 0; i < 32; i++)
        if ((unsigned)i != reg) CHECK(c.x[i] == before[i]);
}

static void test_veneer(void)
{
    for (uint32_t r = 0; r < 31; r++) {
        run_veneer(r, 64, 0xDEAD0000ull + r, 0);
        run_veneer(r, 32760, 0xBEEF0000ull + r, 0);
        run_veneer(r, 16, 0, 1);                     /* slot not published: private trap */
    }
    uint32_t code[DYLD_TSD_VENEER_WORDS];
    CHECK(!dyld_tsd_build_veneer(code, 0x7000000000ull, 0x7000000000ull + (1ULL << 28), 3, 64));  /* return out of range */
}

/* ---- CPU number reads (TPIDR_EL0) --------------------------------------- */

static void test_cpu_encodings(void)
{
    int ok = 1;
    for (uint32_t r = 0; r < 32; r++) {
        uint32_t reg = 99;
        ok &= dyld_cpu_is_tpidr_read(DYLD_A64_MRS_TPIDR_EL0 | r, &reg) && reg == r;
        ok &= !dyld_cpu_is_tpidr_read(DYLD_A64_MRS_TPIDRRO_EL0 | r, &reg);
    }
    CHECK(ok);
    uint32_t reg;
    CHECK(dyld_cpu_is_tpidr_read(0xD53BD043u, &reg) && reg == 3);   /* mrs x3, TPIDR_EL0 (LLVM assembler) */
    CHECK(!dyld_cpu_is_tpidr_read(0xD53BD043u ^ 0x00080000u, &reg)); /* op0 2: another register */
    CHECK(!dyld_cpu_is_tpidr_read(DYLD_A64_NOP, &reg));
    /* the words the LLVM assembler produces */
    CHECK(dyld_cpu_load(5, -32) == 0xB85E00A5u);                     /* ldur w5, [x5, #-32] */
    CHECK(dyld_cpu_load(0, 0) == 0xB9400000u);                       /* ldr w0, [x0] */
    CHECK(dyld_cpu_load(8, 16380) == 0xB97FFD08u);                   /* ldr w8, [x8, #16380] */
    CHECK((DYLD_A64_MOVZ_X | 7) == 0xD2800007u);                     /* mov x7, #0 */

    CHECK(dyld_cpu_offset_loadable(0) && dyld_cpu_offset_loadable(16380) && dyld_cpu_offset_loadable(-256)
          && dyld_cpu_offset_loadable(-32) && dyld_cpu_offset_loadable(252));
    CHECK(!dyld_cpu_offset_loadable(16384) && !dyld_cpu_offset_loadable(-260) && !dyld_cpu_offset_loadable(-30)
          && !dyld_cpu_offset_loadable(2) && !dyld_cpu_offset_loadable(-4096));
}

static void test_cpu_parse(void)
{
    long v = 12345;
    CHECK(dyld_cpu_parse_rseq_offset("ffffffffffffffe0", &v) && v == -32);
    CHECK(dyld_cpu_parse_rseq_offset("FFFFFFFFFFFFFF00", &v) && v == -256);
    CHECK(dyld_cpu_parse_rseq_offset("40", &v) && v == 64);
    CHECK(dyld_cpu_parse_rseq_offset("0", &v) && v == 0);
    v = 12345;
    CHECK(!dyld_cpu_parse_rseq_offset(0, &v) && v == 12345);
    CHECK(!dyld_cpu_parse_rseq_offset("", &v) && v == 12345);
    CHECK(!dyld_cpu_parse_rseq_offset("0x40", &v) && v == 12345);
    CHECK(!dyld_cpu_parse_rseq_offset("-20", &v) && v == 12345);
    CHECK(!dyld_cpu_parse_rseq_offset("40 ", &v) && v == 12345);
    CHECK(!dyld_cpu_parse_rseq_offset("10000000000000000", &v) && v == 12345);   /* 17 digits */
}

static void run_cpu_veneer(uint32_t reg, long rseq_off, uint32_t cpu_number)
{
    uint32_t code[DYLD_CPU_VENEER_WORDS];
    uintptr_t veneer = 0x7000000000ull, ret = 0x7000100000ull + 4;
    CHECK(dyld_cpu_build_veneer(code, veneer, ret, reg, rseq_off));
    struct cpu c;
    memset(&c, 0, sizeof c);
    c.pc = veneer; c.tp = 0xABCD0000ull; c.rseq_off = rseq_off; c.cpu_number = cpu_number;
    c.nzcv = 0x60000000ull;
    for (int i = 0; i < 32; i++) c.x[i] = 0x2222222200000000ull + i;
    uint64_t before[32]; memcpy(before, c.x, sizeof before);
    int ok = 1, done = 0;
    for (int n = 0; n < 8 && !done; n++) done = step(&c, code, veneer, ret, &ok);
    CHECK(ok && done && !c.trapped);
    CHECK(c.pc == ret);
    CHECK(c.x[reg] == cpu_number);                    /* upper bits zero, as on macOS */
    CHECK(c.nzcv == 0x60000000ull);
    for (int i = 0; i < 32; i++)
        if ((unsigned)i != reg) CHECK(c.x[i] == before[i]);
}

static void test_cpu_veneer(void)
{
    const long offsets[] = { -256, -64, -32, 0, 4, 16380 };
    for (uint32_t r = 0; r < 31; r++)
        for (size_t o = 0; o < sizeof offsets / sizeof offsets[0]; o++) {
            run_cpu_veneer(r, offsets[o], 0);
            run_cpu_veneer(r, offsets[o], 37);
            run_cpu_veneer(r, offsets[o], 4095);
        }
    uint32_t code[DYLD_CPU_VENEER_WORDS];
    CHECK(!dyld_cpu_build_veneer(code, 0x7000000000ull, 0x7000000000ull + (1ULL << 28), 3, -32));  /* return out of range */

    /* the in-place fallback: movz Xd, #0 gives CPU 0 with the upper bits clear */
    for (uint32_t r = 0; r < 31; r++) {
        uint32_t word = DYLD_A64_MOVZ_X | r;
        struct cpu c;
        memset(&c, 0, sizeof c);
        for (int i = 0; i < 32; i++) c.x[i] = ~0ull;
        c.pc = 0x1000;
        int ok = 1;
        step(&c, &word, 0x1000, 0x2000, &ok);
        CHECK(ok && c.pc == 0x1004 && c.x[r] == 0);
    }
}

int main(void)
{
    test_encodings();
    test_branch();
    test_veneer();
    test_cpu_encodings();
    test_cpu_parse();
    test_cpu_veneer();
    if (failures) { printf("FAIL: %d checks\n", failures); return 1; }
    puts("PASS tsd-rewrite");
    return 0;
}
