/*
 * This file is part of Darling.
 *
 * Copyright (C) 2026 Lubos Dolezel
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

// Host test for the arm64 dylib arena slot allocator (src/DarlingDylibArena.h),
// run against a mock mapper that refuses occupied or pre-blocked addresses.

#include "DarlingDylibArena.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_MAPPED 4096
#define SLOT_SIZE  0x10000

static pthread_mutex_t mapped_lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t mapped[MAX_MAPPED];
static unsigned mapped_count;
static int refuse_next;
static int failures;

static void reset(int refuse)
{
	mapped_count = 0;
	refuse_next = refuse;
}

// Like a fixed vm_allocate that refuses to replace an existing mapping.
static int mock_map(uintptr_t addr, size_t size)
{
	(void) size;
	// Give concurrent callers a window between claiming and mapping.
	for (volatile int spin = 0; spin < 20000; ++spin)
		;

	if (__atomic_fetch_sub(&refuse_next, 1, __ATOMIC_RELAXED) > 0)
		return 0;

	int ok = 1;
	pthread_mutex_lock(&mapped_lock);
	for (unsigned i = 0; i < mapped_count; ++i) {
		if (mapped[i] == addr) {
			ok = 0;
			break;
		}
	}
	if (ok && mapped_count < MAX_MAPPED)
		mapped[mapped_count++] = addr;
	pthread_mutex_unlock(&mapped_lock);
	return ok;
}

static void check(const char* what, int pass)
{
	printf("%s: %s\n", pass ? "PASS" : "FAIL", what);
	if (!pass)
		failures++;
}

static void test_first_slot(void)
{
	uintptr_t cursor = DARLING_DYLIB_ARENA_BASE;
	reset(0);
	uintptr_t a = darling_dylib_arena_claim(&cursor, SLOT_SIZE, mock_map);
	uintptr_t b = darling_dylib_arena_claim(&cursor, SLOT_SIZE, mock_map);
	check("first slot is the arena base", a == DARLING_DYLIB_ARENA_BASE);
	check("second slot follows after the gap", b == a + SLOT_SIZE + DARLING_DYLIB_ARENA_GAP);
}

static void test_retry_past_occupied(void)
{
	uintptr_t cursor = DARLING_DYLIB_ARENA_BASE;
	reset(3);
	uintptr_t a = darling_dylib_arena_claim(&cursor, SLOT_SIZE, mock_map);
	check("occupied slots are skipped", a == DARLING_DYLIB_ARENA_BASE + 3 * (SLOT_SIZE + DARLING_DYLIB_ARENA_GAP));
}

static void test_gives_up(void)
{
	uintptr_t cursor = DARLING_DYLIB_ARENA_BASE;
	reset(DARLING_DYLIB_ARENA_ATTEMPTS);
	check("returns 0 when every attempt fails",
		darling_dylib_arena_claim(&cursor, SLOT_SIZE, mock_map) == 0);
}

static void test_ceiling(void)
{
	uintptr_t cursor = DARLING_DYLIB_ARENA_CEILING - 0x20000;
	reset(0);
	check("never returns a slot reaching 2^47",
		darling_dylib_arena_claim(&cursor, SLOT_SIZE, mock_map) == 0);

	cursor = DARLING_DYLIB_ARENA_CEILING - 2 * (SLOT_SIZE + DARLING_DYLIB_ARENA_GAP);
	reset(0);
	uintptr_t a = darling_dylib_arena_claim(&cursor, SLOT_SIZE, mock_map);
	check("the last slot below 2^47 is still handed out", a != 0 && a + SLOT_SIZE < DARLING_DYLIB_ARENA_CEILING);
}

#define THREADS 16
#define PER_THREAD 24
static uintptr_t arena_cursor;
static uintptr_t results[THREADS][PER_THREAD];

static void* worker(void* arg)
{
	long t = (long) arg;
	for (int i = 0; i < PER_THREAD; ++i)
		results[t][i] = darling_dylib_arena_claim(&arena_cursor, SLOT_SIZE, mock_map);
	return NULL;
}

static void test_concurrent(void)
{
	pthread_t threads[THREADS];
	arena_cursor = DARLING_DYLIB_ARENA_BASE;
	reset(0);
	for (long t = 0; t < THREADS; ++t)
		pthread_create(&threads[t], NULL, worker, (void*) t);
	for (int t = 0; t < THREADS; ++t)
		pthread_join(threads[t], NULL);

	int bad = 0;
	uintptr_t* all = &results[0][0];
	for (int i = 0; i < THREADS * PER_THREAD; ++i) {
		if (all[i] == 0) {
			bad++;
			continue;
		}
		for (int j = i + 1; j < THREADS * PER_THREAD; ++j) {
			if (all[i] == all[j]) {
				bad++;
				break;
			}
		}
	}
	check("concurrent callers get distinct mapped slots", bad == 0);
}

int main(void)
{
	test_first_slot();
	test_retry_past_occupied();
	test_gives_up();
	test_ceiling();
	test_concurrent();
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
