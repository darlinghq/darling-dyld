/*
 * Copyright (C) 2026 Lubos Dolezel
 *
 * Host test (runs anywhere): builds synthetic dyld shared cache headers and
 * descriptors in memory and checks the validation and decoding helpers used
 * when a user-supplied cache is mapped. No cache file is read or shipped.
 *
 * cc -I dyld3/shared-cache -I <dir containing a stub uuid/uuid.h> \
 *    tests/shared-cache-format.c -o t && ./t
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "dyld_cache_format.h"
#include "dyld_cache_ranges.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static uint64_t pack(uint32_t key, uint32_t off) { return ((uint64_t)off << 32) | key; }

int main(void)
{
    /* range checks: ordinary, boundary and wrap-around inputs */
    CHECK(dyld_cache_range_contains(0x1000, 0x1000, 0x1000, 0x1000));
    CHECK(dyld_cache_range_contains(0x1000, 0x1000, 0x1800, 0x800));
    CHECK(!dyld_cache_range_contains(0x1000, 0x1000, 0x1800, 0x801));
    CHECK(!dyld_cache_range_contains(0x1000, 0x1000, 0xfff, 1));
    CHECK(!dyld_cache_range_contains(UINT64_MAX - 10, 100, UINT64_MAX - 10, 1));
    CHECK(!dyld_cache_range_contains(0, 0x1000, UINT64_MAX, 2));
    CHECK(!dyld_cache_range_contains(0, 0x1000, 0x10, UINT64_MAX));
    CHECK(dyld_cache_range_contains(0x1000, 0, 0x1000, 0));

    /* synthetic header: every mapping must lie inside the shared region */
    struct dyld_cache_header h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "dyld_v1  x86_64", 15);
    h.mappingOffset = offsetof(struct dyld_cache_header, imagesCount);
    h.sharedRegionStart = 0x7ff800000000ULL;
    h.sharedRegionSize = 0x40000000ULL;
    struct dyld_cache_mapping_info m[3] = {
        { h.sharedRegionStart,               0x10000000, 0,          5, 5 },
        { h.sharedRegionStart + 0x10000000,  0x10000000, 0x10000000, 3, 3 },
        { h.sharedRegionStart + 0x20000000,  0x10000000, 0x20000000, 1, 1 },
    };
    uint64_t fileLen = 0x30000000;
    int ok = 1;
    for (int i = 0; i < 3; i++)
        ok &= dyld_cache_range_contains(h.sharedRegionStart, h.sharedRegionSize, m[i].address, m[i].size)
           && dyld_cache_range_contains(0, fileLen, m[i].fileOffset, m[i].size);
    CHECK(ok);
    m[2].address = h.sharedRegionStart + h.sharedRegionSize - 0x1000;  /* runs past the region */
    CHECK(!dyld_cache_range_contains(h.sharedRegionStart, h.sharedRegionSize, m[2].address, m[2].size));
    m[2].address = h.sharedRegionStart + 0x20000000;
    m[2].fileOffset = fileLen;                                        /* runs past the file */
    CHECK(!dyld_cache_range_contains(0, fileLen, m[2].fileOffset, m[2].size));

    /* the header advertises TLV_Thunkv2 through one flag bit */
    CHECK(h.newFormatTLVs == 0);
    h.newFormatTLVs = 1;
    CHECK(h.newFormatTLVs == 1 && h.builtFromChainedFixups == 0 && h.locallyBuiltCache == 0);

    /* TLV_Thunkv2 (64-bit): key | offset<<32, delta | size<<32 */
    uint64_t off;
    const uint32_t size = 0x240;
    CHECK(dyld_tlv_unpack_v2(pack(0x105, 0x40), pack(0, size), size, &off) && off == 0x40);
    CHECK(dyld_tlv_unpack_v2(pack(0x105, 0), pack(0x1234, size), size, &off) && off == 0);
    CHECK(dyld_tlv_unpack_v2(pack(1, size), pack(0, size), size, &off) && off == size);
    /* legacy descriptor (key 0 before setup, plain offset): left alone */
    CHECK(!dyld_tlv_unpack_v2(0, 0x40, size, &off));
    /* size mismatch, offset out of range, oversized storage: rejected */
    CHECK(!dyld_tlv_unpack_v2(pack(0x105, 0x40), pack(0, size + 8), size, &off));
    CHECK(!dyld_tlv_unpack_v2(pack(0x105, size + 1), pack(0, size), size, &off));
    CHECK(!dyld_tlv_unpack_v2(pack(0x105, 0), pack(0, 0), 0x100000000ULL, &off));

    printf(failures ? "FAIL shared-cache-format (%d)\n" : "PASS shared-cache-format\n", failures);
    return failures != 0;
}
