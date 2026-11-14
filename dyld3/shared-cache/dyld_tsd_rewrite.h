/*
 * Copyright (C) 2026 Lubos Dolezel
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

// Instruction helpers for translating the ARM64 Darwin thread-specific data
// (TSD) base reads of a privately mapped shared cache. Plain C so that a host
// test can run them on any architecture.
//
// Darwin keeps the TSD base in TPIDRRO_EL0 (CPU number in the low 3 bits, per
// xnu's machine_machdep.h and libsyscall os/tsd.h), which a Linux process
// cannot set. A `mrs Xd, TPIDRRO_EL0` in the cache is therefore replaced by a
// branch to a veneer that reads the base from a native TLS slot that
// libsystem_kernel fills:
//
//     mrs  Xd, TPIDR_EL0
//     ldr  Xd, [Xd, #slot]
//     cbnz Xd, 1f             (does not change the condition flags)
//     udf  #(0xda00 + d)      slot not published yet: the kernel library
//                             emulates the read (see tsd_trap.h in xnu)
//  1: b    <instruction after the replaced mrs>
//
// Encodings are from the Arm Architecture Reference Manual for A-profile
// (DDI 0487): MRS (register) with TPIDRRO_EL0 (op0=3 op1=3 CRn=13 CRm=0
// op2=3) and TPIDR_EL0 (op2=2), LDR (immediate, unsigned offset), CBNZ, B and
// UDF.

#ifndef __DYLD_TSD_REWRITE__
#define __DYLD_TSD_REWRITE__

#include <stdint.h>
#include <stdbool.h>

#define DYLD_A64_MRS_TPIDRRO_EL0   0xD53BD060u
#define DYLD_A64_MRS_TPIDR_EL0     0xD53BD040u
#define DYLD_A64_MRS_MASK          0xFFFFFFE0u
#define DYLD_A64_LDR_X_UOFFSET     0xF9400000u
#define DYLD_A64_CBNZ_X_PLUS8      0xB5000040u   // cbnz Xt, .+8
#define DYLD_A64_B                 0x14000000u
#define DYLD_A64_NOP               0xD503201Fu
#define DYLD_A64_UDF_TSD_TRAP      0x0000DA00u   // + destination register

#define DYLD_TSD_VENEER_WORDS      5
#define DYLD_TSD_SLOT_MIN          16u
#define DYLD_TSD_SLOT_MAX          32760u        // LDR's scaled 12-bit offset

static inline bool dyld_tsd_slot_offset_valid(unsigned long offset)
{
    return offset >= DYLD_TSD_SLOT_MIN && offset <= DYLD_TSD_SLOT_MAX && (offset & 7) == 0;
}

// True if `insn` is `mrs Xd, TPIDRRO_EL0`; stores d (31 is XZR).
static inline bool dyld_tsd_is_tpidrro_read(uint32_t insn, uint32_t* reg)
{
    if ( (insn & DYLD_A64_MRS_MASK) != DYLD_A64_MRS_TPIDRRO_EL0 )
        return false;
    *reg = insn & 0x1F;
    return true;
}

// Branch from `from` to `to` (B, +-128 MiB); false if out of range or
// misaligned.
static inline bool dyld_tsd_branch(uintptr_t from, uintptr_t to, uint32_t* encoded)
{
    int64_t delta = (int64_t)to - (int64_t)from;
    if ( (delta & 3) || delta < -(1LL << 27) || delta >= (1LL << 27) )
        return false;
    *encoded = DYLD_A64_B | ((uint32_t)(delta >> 2) & 0x03FFFFFFu);
    return true;
}

// Fills the DYLD_TSD_VENEER_WORDS words of the veneer for register `reg`
// (0-30) placed at `veneer`, returning to `returnTo`. `slot` must be valid.
// Returns false if the return branch cannot reach.
static inline bool dyld_tsd_build_veneer(uint32_t* code, uintptr_t veneer, uintptr_t returnTo,
                                         uint32_t reg, unsigned long slot)
{
    uint32_t back;
    if ( !dyld_tsd_branch(veneer + 4 * (DYLD_TSD_VENEER_WORDS - 1), returnTo, &back) )
        return false;
    code[0] = DYLD_A64_MRS_TPIDR_EL0 | reg;
    code[1] = DYLD_A64_LDR_X_UOFFSET | ((uint32_t)(slot / 8) << 10) | (reg << 5) | reg;
    code[2] = DYLD_A64_CBNZ_X_PLUS8 | reg;
    code[3] = DYLD_A64_UDF_TSD_TRAP | reg;
    code[4] = back;
    return true;
}

// ---- CPU number reads -------------------------------------------------------
//
// Darwin keeps the CPU number in bits 0-11 of TPIDR_EL0 and nothing else in
// that register for user code (xnu's public libsyscall os/tsd.h reads it with
// `mrs Xt, TPIDR_EL0` and masks the low 12 bits). Under Linux TPIDR_EL0 is the
// thread pointer, so a `mrs Xd, TPIDR_EL0` in the cache is replaced by a
// branch to a veneer that yields the CPU number in Xd, upper bits zero:
//
//     mrs  Xd, TPIDR_EL0
//     ldr  Wd, [Xd, #offset]   (LDUR for small negative offsets)
//     b    <instruction after the replaced mrs>
//
// `offset` is the thread-pointer-relative offset of the cpu_id_start field of
// the native C library's rseq area, which the loader passes in the apple
// vector as darling_rseq_cpu_offset=<64-bit two's complement hex>. Linux keeps
// that field at the CPU the thread runs on and it always holds a possible CPU
// number. When the offset is unknown, no single load reaches it, or no veneer
// can be placed, the mrs becomes `movz Xd, #0` (CPU 0) in place.
//
// Encodings (DDI 0487): LDR (immediate, unsigned offset, 32-bit), LDUR
// (32-bit), MOVZ (64-bit).

#define DYLD_A64_LDR_W_UOFFSET     0xB9400000u   // ldr Wt, [Xn, #imm12*4]
#define DYLD_A64_LDUR_W            0xB8400000u   // ldur Wt, [Xn, #simm9]
#define DYLD_A64_MOVZ_X            0xD2800000u   // movz Xd, #imm16

#define DYLD_CPU_VENEER_WORDS      3

// True if `insn` is `mrs Xd, TPIDR_EL0`; stores d (31 is XZR).
static inline bool dyld_cpu_is_tpidr_read(uint32_t insn, uint32_t* reg)
{
    if ( (insn & DYLD_A64_MRS_MASK) != DYLD_A64_MRS_TPIDR_EL0 )
        return false;
    *reg = insn & 0x1F;
    return true;
}

// True if one load can read a 32-bit field at `offset` from the thread pointer.
static inline bool dyld_cpu_offset_loadable(long offset)
{
    if ( offset & 3 )
        return false;
    return (offset >= 0 && offset <= 4095 * 4) || (offset >= -256 && offset <= 255);
}

// `ldr Wd, [Xd, #offset]` or `ldur`; the offset must be loadable.
static inline uint32_t dyld_cpu_load(uint32_t reg, long offset)
{
    if ( offset >= 0 )
        return DYLD_A64_LDR_W_UOFFSET | ((uint32_t)(offset / 4) << 10) | (reg << 5) | reg;
    return DYLD_A64_LDUR_W | (((uint32_t)offset & 0x1FF) << 12) | (reg << 5) | reg;
}

// Parses the value of the loader's darling_rseq_cpu_offset= entry (the text
// after the '='): 1 to 16 hex digits, read as a 64-bit two's complement
// number. Returns false for anything else.
static inline bool dyld_cpu_parse_rseq_offset(const char* text, long* offset)
{
    uint64_t value = 0;
    int digits = 0;
    if ( text == 0 )
        return false;
    for (; *text; ++text, ++digits) {
        char c = *text;
        uint32_t nibble;
        if ( c >= '0' && c <= '9' )      nibble = (uint32_t)(c - '0');
        else if ( c >= 'a' && c <= 'f' ) nibble = (uint32_t)(c - 'a' + 10);
        else if ( c >= 'A' && c <= 'F' ) nibble = (uint32_t)(c - 'A' + 10);
        else return false;
        if ( digits == 16 )
            return false;
        value = (value << 4) | nibble;
    }
    if ( digits == 0 )
        return false;
    *offset = (long)(int64_t)value;
    return true;
}

// Fills the DYLD_CPU_VENEER_WORDS words of the CPU number veneer for register
// `reg` (0-30) placed at `veneer`, returning to `returnTo`. `offset` must be
// loadable. Returns false if the return branch cannot reach.
static inline bool dyld_cpu_build_veneer(uint32_t* code, uintptr_t veneer, uintptr_t returnTo,
                                         uint32_t reg, long offset)
{
    uint32_t back;
    if ( !dyld_tsd_branch(veneer + 4 * (DYLD_CPU_VENEER_WORDS - 1), returnTo, &back) )
        return false;
    code[0] = DYLD_A64_MRS_TPIDR_EL0 | reg;
    code[1] = dyld_cpu_load(reg, offset);
    code[2] = back;
    return true;
}

#endif // __DYLD_TSD_REWRITE__
