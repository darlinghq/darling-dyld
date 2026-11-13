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

// Small, dependency-free helpers for validating and decoding data that
// comes from a dyld shared cache file supplied by the user.

#ifndef __DYLD_CACHE_RANGES__
#define __DYLD_CACHE_RANGES__

#include <stdint.h>
#include <stdbool.h>

// True when [address, address+size) lies inside [start, start+length),
// evaluated so that no intermediate sum can wrap around.
static inline bool dyld_cache_range_contains(uint64_t start, uint64_t length,
                                             uint64_t address, uint64_t size)
{
    return length <= UINT64_MAX - start && address >= start
        && address - start <= length && size <= length - (address - start);
}

// Layout reference: the TLV_Thunkv2 structure and the newFormatTLVs header bit
// are declared in Apple's open-source dyld releases (libdyld/ThreadLocalVariables.h
// and dyld_cache_format.h, dyld-1378, APSL). The 32-bit variant is not handled.
//
// Decode a 64-bit thread-local variable descriptor in the packed layout used
// by caches whose header sets newFormatTLVs: the word after the thunk holds a
// 32-bit static key (low half) and the 32-bit offset into the image's TLV
// storage (high half); the last word's high half repeats the storage size.
// The descriptor is accepted only when that size matches the size computed
// from the image's own sections, so a legacy descriptor is never misread.
static inline bool dyld_tlv_unpack_v2(uint64_t keyWord, uint64_t offsetWord,
                                      uint64_t storageSize, uint64_t* offsetOut)
{
    const uint32_t staticKey = (uint32_t)keyWord;
    const uint32_t packedOffset = (uint32_t)(keyWord >> 32);
    const uint32_t packedSize = (uint32_t)(offsetWord >> 32);
    if ( staticKey != 0 && storageSize <= UINT32_MAX
      && packedSize == storageSize && packedOffset <= storageSize ) {
        *offsetOut = packedOffset;
        return true;
    }
    return false;
}

#endif // __DYLD_CACHE_RANGES__
