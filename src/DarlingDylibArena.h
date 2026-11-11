/*
 * This file is part of Darling.
 *
 * Copyright (C) 2026 Darling Developers
 *
 * Darling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Darling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Darling.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DARLING_DYLIB_ARENA_H
#define DARLING_DYLIB_ARENA_H

/*
 * The low-VA arena that arm64 Darling hands out to dylibs. Linux arm64 gives
 * out 48-bit addresses, but libobjc keeps only 47 bits of a class_rw_t
 * pointer, so every dylib has to stay below 2^47.
 *
 * This header has no dyld dependencies so that the slot allocation can be
 * tested on the host (darling/tests/dylib-arena.c).
 */

#include <stddef.h>
#include <stdint.h>

#define DARLING_DYLIB_ARENA_BASE     0x300000000ULL    /* 0x100/0x200 ranges are left for the exe and dyld */
#define DARLING_DYLIB_ARENA_GAP      0x100000ULL       /* 1 MiB between dylibs */
#define DARLING_DYLIB_ARENA_CEILING  0x800000000000ULL /* 2^47 */
#define DARLING_DYLIB_ARENA_ATTEMPTS 64

/* Maps size bytes at exactly addr; returns nonzero on success. */
typedef int (*darling_dylib_arena_map_fn)(uintptr_t addr, size_t size);

/*
 * Claims the next free slot from *cursor and maps it with map. Each slot is
 * claimed with an atomic fetch-add before it is mapped, so two loaders never
 * get the same slot. A slot that cannot be mapped (something outside the
 * arena already lives there) is skipped. Returns 0 when no slot below the
 * ceiling could be mapped.
 */
static inline uintptr_t darling_dylib_arena_claim(uintptr_t* cursor, size_t size, darling_dylib_arena_map_fn map)
{
	const uintptr_t stride = size + DARLING_DYLIB_ARENA_GAP;
	for (unsigned attempt = 0; attempt < DARLING_DYLIB_ARENA_ATTEMPTS; ++attempt) {
		uintptr_t claimed = __atomic_fetch_add(cursor, stride, __ATOMIC_RELAXED);
		if (claimed + stride >= DARLING_DYLIB_ARENA_CEILING)
			break;
		if (map(claimed, size))
			return claimed;
	}
	return 0;
}

#endif
