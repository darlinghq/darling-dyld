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

#include <mach-o/utils.h>
#include <mach-o/loader.h>
#include <crt_externs.h>
#include <string.h>

struct arch_name {
	const char* name;
	cpu_type_t type;
	cpu_subtype_t subtype;
};

// Each name maps to the first entry with that name; the remaining entries
// are extra subtypes that have the same name.
static const struct arch_name arch_names[] = {
	{ "x86_64",   CPU_TYPE_X86_64,   CPU_SUBTYPE_X86_64_ALL },
	{ "x86_64h",  CPU_TYPE_X86_64,   CPU_SUBTYPE_X86_64_H },
	{ "i386",     CPU_TYPE_I386,     CPU_SUBTYPE_I386_ALL },
	{ "arm64",    CPU_TYPE_ARM64,    CPU_SUBTYPE_ARM64_ALL },
	{ "arm64",    CPU_TYPE_ARM64,    CPU_SUBTYPE_ARM64_V8 },
	{ "arm64e",   CPU_TYPE_ARM64,    CPU_SUBTYPE_ARM64E | CPU_SUBTYPE_PTRAUTH_ABI },
	{ "arm64_32", CPU_TYPE_ARM64_32, CPU_SUBTYPE_ARM64_32_V8 },
	{ "arm64_32", CPU_TYPE_ARM64_32, CPU_SUBTYPE_ARM64_32_ALL },
	{ "armv4t",   CPU_TYPE_ARM,      CPU_SUBTYPE_ARM_V4T },
	{ "armv6",    CPU_TYPE_ARM,      CPU_SUBTYPE_ARM_V6 },
	{ "armv7",    CPU_TYPE_ARM,      CPU_SUBTYPE_ARM_V7 },
	{ "armv7s",   CPU_TYPE_ARM,      CPU_SUBTYPE_ARM_V7S },
	{ "armv7k",   CPU_TYPE_ARM,      CPU_SUBTYPE_ARM_V7K },
	{ "armv6m",   CPU_TYPE_ARM,      CPU_SUBTYPE_ARM_V6M },
	{ "armv7m",   CPU_TYPE_ARM,      CPU_SUBTYPE_ARM_V7M },
	{ "armv7em",  CPU_TYPE_ARM,      CPU_SUBTYPE_ARM_V7EM },
	{ "ppc",      CPU_TYPE_POWERPC,  CPU_SUBTYPE_POWERPC_ALL },
};

#define ARCH_COUNT (sizeof(arch_names) / sizeof(arch_names[0]))

bool macho_cpu_type_for_arch_name(const char* archName, cpu_type_t* type, cpu_subtype_t* subtype)
{
	for (size_t i = 0; i < ARCH_COUNT; i++) {
		if (strcmp(archName, arch_names[i].name) == 0) {
			*type = arch_names[i].type;
			*subtype = arch_names[i].subtype;
			return true;
		}
	}
	return false;
}

const char* macho_arch_name_for_cpu_type(cpu_type_t type, cpu_subtype_t subtype)
{
	cpu_subtype_t masked = subtype & ~CPU_SUBTYPE_MASK;

	if (type == CPU_TYPE_ARM64) {
		// The capability bits of an arm64 subtype describe the pointer
		// authentication ABI, which only arm64e has.
		if (masked == CPU_SUBTYPE_ARM64E)
			return "arm64e";
		if ((subtype & CPU_SUBTYPE_MASK) != 0)
			return NULL;
	}

	for (size_t i = 0; i < ARCH_COUNT; i++) {
		if (arch_names[i].type == type && (arch_names[i].subtype & ~CPU_SUBTYPE_MASK) == masked)
			return arch_names[i].name;
	}
	return NULL;
}

const char* macho_arch_name_for_mach_header(const struct mach_header* mh)
{
	if (mh == NULL)
		mh = (const struct mach_header*) _NSGetMachExecuteHeader();
	if (mh->magic != MH_MAGIC && mh->magic != MH_MAGIC_64)
		return NULL;
	return macho_arch_name_for_cpu_type(mh->cputype, mh->cpusubtype);
}
