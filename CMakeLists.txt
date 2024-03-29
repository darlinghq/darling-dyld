project(dyld-apple)

cmake_minimum_required(VERSION 3.13)

if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

include (darling_exe)

add_definitions(-nostdinc)
add_definitions(-D_LIBCPP_NO_EXCEPTIONS=1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wno-availability -include ${CMAKE_SOURCE_DIR}/src/duct/include/CrashReporterClient.h")
set(CMAKE_SHARED_LINKER_FLAGS_SAVED "${CMAKE_SHARED_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -nostdlib")

add_definitions(-DHAVE_STDINT_H=1)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/launch-cache)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/include/mach-o
	${CMAKE_CURRENT_SOURCE_DIR}/dyld3/shared-cache
	${CMAKE_CURRENT_SOURCE_DIR}/dyld3
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(DYLIB_INSTALL_NAME "/usr/lib/system/libdyld.dylib")
add_circular(system_dyld FAT
	SOURCES
	src/dyldAPIsInLibSystem.cpp
	src/dyld_stub_binder.S
	src/dyldLibSystemGlue.c
	src/threadLocalHelpers.S
	src/dyldLock.cpp
	src/threadLocalVariables.c
	src/dyld_process_info.cpp
	src/dyld_process_info_notify.cpp
	src/start_glue.S
	dyld3/AllImages.cpp
	dyld3/APIs.cpp
	dyld3/APIs_macOS.cpp
	dyld3/ClosureBuilder.cpp
	dyld3/Closure.cpp
	dyld3/ClosureFileSystemPhysical.cpp
	#dyld3/ClosurePrinter.cpp
	dyld3/ClosureWriter.cpp
	dyld3/Diagnostics.cpp
	dyld3/shared-cache/DyldSharedCache.cpp
	#dyld3/JSONReader.mm
	dyld3/libdyldEntryVector.cpp
	dyld3/Loading.cpp
	dyld3/Logging.cpp
	dyld3/MachOAnalyzer.cpp
	dyld3/MachOAnalyzerSet.cpp
	dyld3/MachOFile.cpp
	dyld3/MachOLoaded.cpp
	dyld3/PathOverrides.cpp
	dyld3/RootsChecker.cpp
	dyld3/Tracing.cpp

	UPWARD
		system_c
		system_kernel
		system_malloc
		system_blocks
		system_pthread
		system_sandbox
		libdispatch_shared
		compiler_rt
		platform
		launch
)
set_target_properties(system_dyld PROPERTIES
	OUTPUT_NAME "dyld")
set_property(TARGET system_dyld_obj APPEND_STRING PROPERTY
	COMPILE_FLAGS " -fno-exceptions -fno-rtti -DBUILDING_LIBDYLD=1")
set_property(TARGET system_dyld APPEND_STRING PROPERTY
	LINK_FLAGS " -Wl,-compatibility_version,1.0.0 -Wl,-current_version,421.1.0")
#target_link_libraries(system_dyld PRIVATE platform_static32 platform_static64 system_c system_kernel compiler_rt launch)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_SAVED} -nostdlib")

add_executable(system_loader
	src/dyldStartup.S
	src/dyld2.cpp
	src/dyldAPIs.cpp
	src/dyld_debugger.cpp
	src/dyldInitialization.cpp
	src/dyldLock.cpp
	src/dyldNew.cpp
	src/ImageLoader.cpp
	src/ImageLoaderMachOClassic.cpp
	src/ImageLoaderMachOCompressed.cpp
	src/ImageLoaderMachO.cpp
	src/ImageLoaderMegaDylib.cpp
	src/dyldExceptions.c
	src/glue.c
	src/stub_binding_helper.S
	src/start_glue.S
	dyld3/BootArgs.cpp
	dyld3/ClosureBuilder.cpp
	dyld3/Closure.cpp
	dyld3/ClosureFileSystemNull.cpp
	dyld3/ClosureFileSystemPhysical.cpp
	#dyld3/ClosurePrinter.cpp
	dyld3/ClosureWriter.cpp
	dyld3/Tracing.cpp
	dyld3/Loading.cpp
	dyld3/Diagnostics.cpp
	dyld3/SharedCacheRuntime.cpp
	dyld3/shared-cache/DyldSharedCache.cpp
	#dyld3/shared-cache/FileUtils.cpp
	dyld3/MachOAnalyzer.cpp
	dyld3/MachOAnalyzerSet.cpp
	dyld3/MachOFile.cpp
	dyld3/MachOLoaded.cpp
	dyld3/PathOverrides.cpp
	dyld3/SharedCacheRuntime.cpp
	dyld3/RootsChecker.cpp
	darling/src/hell.c
	darling/src/sandbox-dummy.c
	version.c # Too make the .exp file work
)

use_ld64(system_loader)
dsym(system_loader)
set_target_properties(system_loader PROPERTIES OUTPUT_NAME "dyld")
set_property(TARGET system_loader APPEND_STRING PROPERTY
	COMPILE_FLAGS " -DBUILDING_DYLD=1 -DPRIVATE=1 -gfull -fPIC -fno-stack-check")
set_property(TARGET system_loader APPEND_STRING PROPERTY LINK_FLAGS " -Wl,-dylinker -Wl,-dead_strip -Wl,-data_const -nostdlib -Wl,-e,__dyld_start -Wl,-fixup_chains -Wl,-image_base,0x1fe00000 -Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/src/dyld.exp")
target_link_libraries(system_loader libc_static system_blocks_static
	cxx_static
	unwind_static system_m_static cxxabi_static
	keymgr_static system_duct_static
	pthread_static macho_static
	system_trace_static
	corecrypto_static
)

if (BUILD_TARGET_32BIT)
	target_link_libraries(system_loader libc_static32 compiler_rt_static32 platform_static32 system_kernel_static32)
endif (BUILD_TARGET_32BIT)

if (BUILD_TARGET_64BIT)
	target_link_libraries(system_loader libc_static64 compiler_rt_static64 platform_static64 system_kernel_static64)
endif (BUILD_TARGET_64BIT)

make_fat(system_loader)

install(TARGETS system_dyld DESTINATION libexec/darling/usr/lib/system)
install(TARGETS system_loader DESTINATION libexec/darling/usr/lib)
install(FILES
	doc/man/man1/dyld.1
	doc/man/man1/dyld_usage.1
	DESTINATION
	libexec/darling/usr/share/man/man1
)
install(FILES
	doc/man/man3/dladdr.3
	doc/man/man3/dlclose.3
	doc/man/man3/dlerror.3
	doc/man/man3/dlopen.3
	doc/man/man3/dlopen_preflight.3
	doc/man/man3/dlsym.3
	doc/man/man3/dyld.3
	DESTINATION
	libexec/darling/usr/share/man/man3
)

