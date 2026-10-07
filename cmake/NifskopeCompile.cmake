# Compiler settings shared by NifSkope's own targets. Nothing global except the language standard:
# usage requirements travel on two INTERFACE targets.
#
#   nifskope_compile_options  definitions and flags that change the code (PUBLIC on nifskope_core, so the
#                             application and the tests are compiled the same way)
#   nifskope_warnings         -Wall -Wextra (and -Wimplicit-fallthrough with clang), linked PRIVATE by
#                             first-party targets only. Vendored libraries never get it: they are quiet without
#                             it and their warnings are not ours to fix. With NIFSKOPE_WERROR_DEPRECATED a
#                             deprecated declaration is an error as well, in first-party code only for the same reason

# NIFSKOPE_CXX_STANDARD (cmake/NifskopeQt.cmake) is the C++ standard; qmake: CONFIG += c++1z and c++2a for 17 and 20
# (NifSkope_settings.pri). GNU extensions stay on (CMAKE_CXX_EXTENSIONS defaults to ON).
# Set as variables, not with cxx_std_17: a feature requirement adds no flag when the compiler's default is already
# newer (Apple clang 21 and GCC 11+ default to gnu++17, GCC 16 to gnu++20). The C standard is left alone on purpose:
# lib/lz4frame.c and the xxhash.c it includes typedef the same names, which only C11 and later accept.
set(CMAKE_CXX_STANDARD ${NIFSKOPE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C++20 needs a compiler that has it. CMake would pass an older one -std=c++2a or /std:c++latest, a state nobody tests, so
# stop here with the oldest versions that BUILDING.md lists (same floor in NifSkope_settings.pri). cl 19.29.30129 is
# Visual Studio 2019 16.11, the first with /std:c++20
if(NIFSKOPE_CXX_STANDARD GREATER_EQUAL 20)
	set(_nifskope_cxx20_min "")
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		set(_nifskope_cxx20_min 10)
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		set(_nifskope_cxx20_min 10)
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
		set(_nifskope_cxx20_min 12)
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
		set(_nifskope_cxx20_min 19.29.30129)
	endif()
	if(_nifskope_cxx20_min AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS _nifskope_cxx20_min)
		message(FATAL_ERROR
			"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} is too old for C++20: NifSkope needs GCC 10, Clang 10, "
			"Apple clang 12 (Xcode 12) or MSVC 2019 16.11 or later. Use a newer compiler, or -DNIFSKOPE_CXX_STANDARD=17.")
	endif()
	unset(_nifskope_cxx20_min)
endif()

# qmake never defines NDEBUG, CMake's default Release flags do. With it assert() disappears from lib/NvTriStrip (11
# sites, one of them assert(0)) and from the gli code in src/gl/gltex*.cpp. Take it out of every configuration that
# carries it so the build behaves like the qmake one; NIFSKOPE_KEEP_NDEBUG=ON restores the CMake default. Must run
# before the subdirectories are added: they copy these variables
if(NOT NIFSKOPE_KEEP_NDEBUG)
	foreach(lang C CXX)
		foreach(config RELEASE MINSIZEREL RELWITHDEBINFO)
			string(REGEX REPLACE "[-/]DNDEBUG" "" flags "${CMAKE_${lang}_FLAGS_${config}}")
			string(STRIP "${flags}" flags)
			set(CMAKE_${lang}_FLAGS_${config} "${flags}")
		endforeach()
	endforeach()
	unset(flags)
endif()

add_library(nifskope_compile_options INTERFACE)
target_compile_definitions(nifskope_compile_options INTERFACE
	QT_NO_CAST_FROM_BYTEARRAY
	QT_NO_URL_CAST_FROM_STRING
	QT_DISABLE_DEPRECATED_BEFORE=${NIFSKOPE_QT_DEPRECATED_BEFORE}
	# qmake: only in the release build. Qt's own targets add QT_NO_DEBUG for every non-Debug configuration
	$<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>)

if(WIN32)
	# The definitions qmake's Windows mkspecs add on their own. CMake's MSVC flags already carry /DWIN32
	target_compile_definitions(nifskope_compile_options INTERFACE UNICODE _UNICODE)
	if(NOT MSVC)
		target_compile_definitions(nifskope_compile_options INTERFACE WIN32)
	endif()
endif()

if(MSVC)
	target_compile_definitions(nifskope_compile_options INTERFACE _ENABLE_EXTENDED_ALIGNED_STORAGE _CRT_SECURE_NO_WARNINGS)
	# STL4043, a C4996 and so an error with NIFSKOPE_WERROR_DEPRECATED, for a use that is not ours: on MSVC Qt 5.15.2 to
	# 5.15.16 (qcompilerdetection.h) define QT_MAKE_CHECKED_ARRAY_ITERATOR as stdext::make_checked_array_iterator, and
	# QVector<T>::operator== (qvector.h:960 in 5.15.2), QList<T> and QVarLengthArray pass it to std::equal or std::copy.
	# That instantiates stdext::checked_array_iterator, which the STL of Visual Studio 2022 17.8 and later deprecates in
	# the C++17 and C++20 modes NifSkope builds in. Qt 5.15.17 made the macros no-ops from 17.8 on (QTBUG-118993), so
	# this is for the older Qt, the 5.15.2 of CI among them. The diagnostic is located in the STL header, so
	# /external:W0 for the Qt directories does not hide it. Defined on the command line because yvals_core.h reads it
	# when the first STL header comes in. Not the umbrella _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS, which would hide
	# STL4045, STL4046 and STL4048 too. The vendored libraries include no Qt header, so they are fine without it
	target_compile_definitions(nifskope_compile_options INTERFACE _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
	# /bigobj: AUTOMOC concatenates a target's moc files into one translation unit (qmake compiled them separately)
	target_compile_options(nifskope_compile_options INTERFACE /bigobj)
	if(MSVC_VERSION GREATER 1900)
		target_compile_options(nifskope_compile_options INTERFACE /permissive-)
	endif()
	if(MSVC_VERSION GREATER 1913)
		# qmake's msvc-version.conf: without it __cplusplus stays 199711L whatever the language standard is
		target_compile_options(nifskope_compile_options INTERFACE /Zc:__cplusplus)
	endif()
	if(CMAKE_GENERATOR MATCHES "Visual Studio")
		target_compile_options(nifskope_compile_options INTERFACE /MP)
	endif()
elseif(MINGW)
	target_compile_definitions(nifskope_compile_options INTERFACE MINGW_HAS_SECURE_API=1)
endif()

if(APPLE)
	# qmake targeted macOS 10.13, where OpenGL is not deprecated yet. At the 11.0 deployment target every gl*() call in
	# src/gl warns (about 950 times in a full build). Apple's documented switch; remove it when the renderer moves on
	target_compile_definitions(nifskope_compile_options INTERFACE GL_SILENCE_DEPRECATION)
endif()

# qmake added "-mfpmath=sse -msse2 -msse" for every *-g++ spec, which GCC rejects on ARM. It only does something on
# 32-bit x86 (SSE2 is the x86-64 baseline).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_SIZEOF_VOID_P EQUAL 4
		AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86|X86)$")
	target_compile_options(nifskope_compile_options INTERFACE -msse2 -mfpmath=sse)
endif()

# -Wextra includes -Wimplicit-fallthrough in GCC (level 3, which also accepts a "fall through" comment) but not in
# clang, so clang gets it by name; it only accepts the attribute, Q_FALLTHROUGH(). MSVC has no such warning
add_library(nifskope_warnings INTERFACE)
target_compile_options(nifskope_warnings INTERFACE
	"$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>"
	"$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wimplicit-fallthrough>"
	"$<$<CXX_COMPILER_ID:MSVC>:/W3;/w34100;/w34189;/w44996>")

# A call of a deprecated function is an error, not a warning, so that the Qt 5 -> Qt 6 work does not slip back. Qt API
# deprecated up to 5.15 is not declared at all (QT_DISABLE_DEPRECATED_BEFORE above); this is what stays: other
# [[deprecated]] declarations and the library and system ones (the C++ library, the CRT, the OS). Off by default, because
# a newer compiler or C++ library deprecates more, and a build that does not get that far should not stop for it; the
# ci-* presets of CMakePresets.json turn it on. MSVC: /w44996 above moves C4996, the warning for deprecated
# declarations, to level 4, which /W3 does not show; /w34996 puts it back at level 3 and /we4996 makes it an error, so
# that the result does not depend on whether /we overrides a warning level
if(NIFSKOPE_WERROR_DEPRECATED)
	target_compile_options(nifskope_warnings INTERFACE
		"$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror=deprecated-declarations>"
		"$<$<CXX_COMPILER_ID:MSVC>:/w34996;/we4996>")
endif()
