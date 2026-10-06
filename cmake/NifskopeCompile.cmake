# Compiler settings shared by NifSkope's own targets. Nothing global except the language standard:
# usage requirements travel on two INTERFACE targets.
#
#   nifskope_compile_options  definitions and flags that change the code (PUBLIC on nifskope_core, so the
#                             application and the tests are compiled the same way)
#   nifskope_warnings         -Wall -Wextra, linked PRIVATE by first-party targets only. Vendored libraries
#                             never get it: they are quiet without it and their warnings are not ours to fix.

# NIFSKOPE_CXX_STANDARD (cmake/NifskopeQt.cmake) is the C++ standard; qmake: CONFIG += c++14 -> -std=gnu++1y, c++1z and
# c++2a for 17 and 20 (NifSkope_settings.pri). GNU extensions stay on (CMAKE_CXX_EXTENSIONS defaults to ON).
# Set as variables, not with cxx_std_14: a feature requirement adds no flag when the compiler's default
# is already newer (Apple clang 21 and GCC 11+ default to gnu++17).  The C standard is left alone on purpose:
# lib/lz4frame.c and the xxhash.c it includes typedef the same names, which only C11 and later accept.
set(CMAKE_CXX_STANDARD ${NIFSKOPE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_library(nifskope_warnings INTERFACE)
target_compile_options(nifskope_warnings INTERFACE
	"$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>"
	"$<$<CXX_COMPILER_ID:MSVC>:/W3;/w34100;/w34189;/w44996>")
