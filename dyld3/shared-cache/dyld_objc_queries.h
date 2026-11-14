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

// Dependency-free helpers for the Objective-C queries dyld answers about a
// mapped shared cache (_dyld_is_preoptimized_objc_image_loaded and
// _dyld_objc_class_count).

#ifndef __DYLD_OBJC_QUERIES__
#define __DYLD_OBJC_QUERIES__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// An image index is a position in the cache's image list, which must exist.
static inline bool dyld_cache_image_index_valid(uint32_t imagesCount, uint32_t imageIndex)
{
    return imageIndex < imagesCount;
}

// Dylibs of the cache are numbered from 1 in the closure image arrays, in
// image-list order (the same convention DyldSharedCache::objcOpt() uses to find
// libobjc), so the number of the image at `imageIndex` is imageIndex + 1.
static inline uint32_t dyld_cache_image_num_for_index(uint32_t imageIndex)
{
    return imageIndex + 1;
}

#endif // __DYLD_OBJC_QUERIES__
