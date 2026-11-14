/*
 * Copyright (C) 2026 Lubos Dolezel
 *
 * Host test of the cache image index helpers behind
 * _dyld_is_preoptimized_objc_image_loaded (dyld3/shared-cache/dyld_objc_queries.h).
 *
 * cc -I dyld3/shared-cache tests/objc-queries.c -o t && ./t
 */
#include <stdio.h>
#include "dyld_objc_queries.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(void)
{
    CHECK(!dyld_cache_image_index_valid(0, 0));
    CHECK(dyld_cache_image_index_valid(1, 0));
    CHECK(!dyld_cache_image_index_valid(1, 1));
    CHECK(dyld_cache_image_index_valid(1500, 1499));
    CHECK(!dyld_cache_image_index_valid(1500, 65535));
    CHECK(dyld_cache_image_num_for_index(0) == 1);
    CHECK(dyld_cache_image_num_for_index(65535) == 65536);
    if (failures) return 1;
    puts("PASS objc-queries");
    return 0;
}
