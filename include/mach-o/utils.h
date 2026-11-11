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

#ifndef _MACH_O_UTILS_H_
#define _MACH_O_UTILS_H_

#include <stdbool.h>
#include <stdint.h>
#include <mach/machine.h>
#include <Availability.h>

struct mach_header;

#ifdef __cplusplus
extern "C" {
#endif

// Looks up the CPU type and subtype for an architecture name such as
// "x86_64" or "arm64e". Returns false, and leaves type and subtype alone,
// for a name it does not know.
extern bool macho_cpu_type_for_arch_name(const char* _Nonnull archName, cpu_type_t* _Nonnull type, cpu_subtype_t* _Nonnull subtype)
__API_AVAILABLE(macos(13.0), ios(16.0), tvos(16.0), watchos(8.0));

// Returns the architecture name for a CPU type and subtype as a static
// string, or NULL if the pair is unknown.
extern const char* _Nullable macho_arch_name_for_cpu_type(cpu_type_t type, cpu_subtype_t subtype)
__API_AVAILABLE(macos(13.0), ios(16.0), tvos(16.0), watchos(8.0));

// Returns the architecture name of a Mach-O header, or of the main
// executable when mh is NULL. Returns NULL if mh is not a Mach-O header or
// its architecture is unknown.
extern const char* _Nullable macho_arch_name_for_mach_header(const struct mach_header* _Nullable mh)
__API_AVAILABLE(macos(13.0), ios(16.0), tvos(16.0), watchos(8.0));

#ifdef __cplusplus
}
#endif

#endif /* _MACH_O_UTILS_H_ */
