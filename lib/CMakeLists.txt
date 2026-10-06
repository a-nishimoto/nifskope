# Vendored libraries and lib/fsengine. One target each; the submodules' own CMakeLists.txt files are never used
# (lib/qhull and lib/gli refuse to configure on CMake >= 4, lib/zlib's would add shared libraries, examples and
# a rewritten zconf.h). Nothing here is written outside the build tree.
#
# Vendored headers are SYSTEM includes (their warnings are not ours to fix), with two exceptions: lib/ itself, which
# the vendored lz4 target has to export for lz4frame.h and which also holds first-party headers (dds.h, half.h), and
# the generated zlib shim directory, which has to stay an ordinary -I directory (see below).
set(NIFSKOPE_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(NIFSKOPE_SHIM_DIR "${PROJECT_BINARY_DIR}/shims")
set(NIFSKOPE_SUBMODULE_HINT "Run: git submodule update --init --recursive")

# The application needs every library here. A tests-only build (NIFSKOPE_BUILD_APP=OFF) needs half.cpp and, unless
# NIFSKOPE_BUILD_BSA_TEST is OFF as well, zlib, lz4 and fsengine for tst_zlib.cpp: it configures without the
# lib/qhull and lib/gli submodules, and without lib/zlib too when the BSA test is off.
set(_ns_with_bsa OFF)
if(NIFSKOPE_BUILD_APP OR NIFSKOPE_BUILD_BSA_TEST)
	set(_ns_with_bsa ON)
endif()

# ---- half ----------------------------------------------------------------------------------------------------
add_library(nifskope_half STATIC half.cpp half.h)

if(_ns_with_bsa)
	# ---- zlib ----------------------------------------------------------------------------------------------------
	# lib/fsengine/bsa.cpp and tests/tst_zlib.cpp write #include "zlib/zlib.h". That resolves through the include root
	# lib/ to the vendored header, even when a system zlib is linked (silently pairing a 1.3.2 header with another
	# version's library). So the target hands out a generated shim, <build>/shims/zlib/zlib.h, that forwards to the
	# header of the zlib actually in use, in both modes. The shim directory must come before lib/ on every command
	# line that has both: nifskope_fsengine, through which the application and the tests get lib/, lists it first
	# (see there). It is an ordinary include directory on purpose: a SYSTEM one is searched after every -I
	# directory, so after lib/.
	add_library(nifskope_zlib INTERFACE)
	if(NIFSKOPE_USE_SYSTEM_ZLIB)
		# 1.3.2 is the floor tests/tst_zlib.cpp enforces
		find_package(ZLIB 1.3.2 REQUIRED)
		target_link_libraries(nifskope_zlib INTERFACE ZLIB::ZLIB)
		set(_zlib_header "${ZLIB_INCLUDE_DIR}/zlib.h")
	else()
		nifskope_require_files("lib/zlib" "${NIFSKOPE_SUBMODULE_HINT}" "${NIFSKOPE_LIB_DIR}/zlib/zlib.h")
		# The 11 core files NifSkope.pro lists. Not gz*.c: the file API is unused and needs HAVE_UNISTD_H on POSIX
		add_library(nifskope_zlib_vendored STATIC
			zlib/adler32.c zlib/compress.c zlib/crc32.c zlib/deflate.c zlib/infback.c zlib/inffast.c
			zlib/inflate.c zlib/inftrees.c zlib/trees.c zlib/uncompr.c zlib/zutil.c)
		target_include_directories(nifskope_zlib_vendored SYSTEM PUBLIC zlib)
		target_compile_definitions(nifskope_zlib_vendored PRIVATE
			$<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_DEPRECATE>
			$<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_DEPRECATE>)
		target_link_libraries(nifskope_zlib INTERFACE nifskope_zlib_vendored)
		set(_zlib_header "${NIFSKOPE_LIB_DIR}/zlib/zlib.h")
	endif()
	file(GENERATE OUTPUT "${NIFSKOPE_SHIM_DIR}/zlib/zlib.h" CONTENT "#include \"${_zlib_header}\"\n")
	target_include_directories(nifskope_zlib INTERFACE "${NIFSKOPE_SHIM_DIR}")

	# ---- lz4 -----------------------------------------------------------------------------------------------------
	# lib/lz4frame.c is a single-file amalgam of lz4 1.7.2. With XXH_PRIVATE_API, xxhash.h #includes xxhash.c, so
	# xxhash.c is NOT a translation unit of its own (qmake silently dropped it too). LZ4_STATIC is needed by every
	# user of lz4frame.h: without it LZ4FLIB_API is undefined outside Windows. The include directory is lib/, a plain
	# one: lz4frame.h has to be found as <lz4frame.h>, and lib/ is not a vendored directory.
	add_library(nifskope_lz4 INTERFACE)
	if(NIFSKOPE_USE_SYSTEM_LZ4)
		nifskope_find_lz4(_lz4_target)
		target_link_libraries(nifskope_lz4 INTERFACE ${_lz4_target})
	else()
		nifskope_require_files("lib/lz4frame.c" "" "${NIFSKOPE_LIB_DIR}/lz4frame.c")
		add_library(nifskope_lz4_vendored STATIC lz4frame.c lz4frame.h xxhash.h)
		target_compile_definitions(nifskope_lz4_vendored PUBLIC LZ4_STATIC PRIVATE XXH_PRIVATE_API)
		target_include_directories(nifskope_lz4_vendored PUBLIC "${NIFSKOPE_LIB_DIR}")
		target_link_libraries(nifskope_lz4 INTERFACE nifskope_lz4_vendored)
	endif()

	# ---- fsengine: BSA archive access (first-party, Qt) ----------------------------------------------------------
	# bsa.cpp includes "dds.h" and lz4frame.h; with the vendored lz4 the include root lib/ comes from
	# nifskope_lz4_vendored. Consumers write <fsengine/bsa.h>, which is why lib/ is an INTERFACE directory. The zlib
	# shim directory is listed before it in the same property, so that consumers find it first whatever order they
	# link their libraries in; bsa.cpp itself is covered because a target's own directories come before those of the
	# targets it links, hence PUBLIC.
	add_library(nifskope_fsengine STATIC
		fsengine/bsa.cpp fsengine/fsengine.cpp fsengine/fsmanager.cpp
		fsengine/bsa.h fsengine/fsengine.h fsengine/fsmanager.h)
	set_target_properties(nifskope_fsengine PROPERTIES AUTOMOC ON)
	target_include_directories(nifskope_fsengine
		PUBLIC "${NIFSKOPE_SHIM_DIR}"
		INTERFACE "${NIFSKOPE_LIB_DIR}")
	target_link_libraries(nifskope_fsengine
		PUBLIC ${NIFSKOPE_QT}::Widgets
		PRIVATE nifskope_compile_options nifskope_warnings nifskope_zlib nifskope_lz4)
endif()

if(NIFSKOPE_BUILD_APP)
	# ---- qhull ---------------------------------------------------------------------------------------------------
	# No library: src/lib/qhull.cpp #includes the 17 libqhull/*.c files into its own translation unit (compiled as
	# C++ inside extern "C"). The include directory is SYSTEM because that is the only thing keeping the qhull
	# warnings out of the build. A system qhull needs NIFSKOPE_QHULL_EXTERNAL, which makes the wrapper skip those
	# #includes (the #ifndef is in src/lib/qhull.cpp).
	add_library(nifskope_qhull INTERFACE)
	if(NIFSKOPE_USE_SYSTEM_QHULL)
		nifskope_find_qhull(_qhull_target)
		target_link_libraries(nifskope_qhull INTERFACE ${_qhull_target})
		target_compile_definitions(nifskope_qhull INTERFACE NIFSKOPE_QHULL_EXTERNAL)
	else()
		nifskope_require_files("lib/qhull" "${NIFSKOPE_SUBMODULE_HINT}" "${NIFSKOPE_LIB_DIR}/qhull/src/libqhull/qhull_a.h")
		target_include_directories(nifskope_qhull SYSTEM INTERFACE "${NIFSKOPE_LIB_DIR}/qhull/src")
	endif()

	# ---- gli + glm (header only; the vendored gli carries its own glm snapshot) ----------------------------------
	add_library(nifskope_gli INTERFACE)
	if(NIFSKOPE_USE_SYSTEM_GLI)
		nifskope_find_gli(_gli_target)
		target_link_libraries(nifskope_gli INTERFACE ${_gli_target})
	else()
		nifskope_require_files("lib/gli" "${NIFSKOPE_SUBMODULE_HINT}" "${NIFSKOPE_LIB_DIR}/gli/gli/gli.hpp")
		# <gli.hpp> lives in gli/gli, gli's own <glm/...> in gli/external. SYSTEM: with plain -I they emit ~120 warnings
		target_include_directories(nifskope_gli SYSTEM INTERFACE "${NIFSKOPE_LIB_DIR}/gli/gli" "${NIFSKOPE_LIB_DIR}/gli/external")
	endif()

	# ---- NvTriStrip ----------------------------------------------------------------------------------------------
	add_library(nifskope_nvtristrip STATIC
		NvTriStrip/NvTriStrip.cpp NvTriStrip/NvTriStripObjects.cpp NvTriStrip/VertexCache.cpp
		NvTriStrip/NvTriStrip.h NvTriStrip/NvTriStripObjects.h NvTriStrip/VertexCache.h)
	target_include_directories(nifskope_nvtristrip SYSTEM PUBLIC NvTriStrip)
endif()
