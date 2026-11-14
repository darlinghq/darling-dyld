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
#define DYLD_A64_MOV_X_XZR         0xAA1F03E0u   // mov Xd, xzr (ORR Xd, XZR, XZR)
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

// QUIRK: `lsr Xd, Xd, #12` (UBFM Xd, Xd, #12, #63) right after a TPIDRRO_EL0
// read is taken to be a CPU index computation rather than a TSD base use, and
// the read is replaced by zero. Behaviour reported by the author of the
// private-cache translation; not documented by Arm or Apple. Evidence tier: a
// single third-party report, not reproduced here (no arm64 hardware or Apple
// cache is available to this project's contributors for observation).
// Needs maintainer approval before it is merged.
// Test: tests/tsd-rewrite.c, "quirk: cpu index shift".
static inline bool dyld_tsd_is_cpu_index_shift(uint32_t insn, uint32_t reg)
{
    return insn == (0xD34CFC00u | (reg << 5) | reg);
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

#endif // __DYLD_TSD_REWRITE__
