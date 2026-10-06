# Included before project(): guards and defaults that must be in place before the toolchain is set up.
# NifSkope's own CMakeLists.txt includes this file, so the CMAKE_CURRENT_*_DIR variables used here and in
# NifskopeVersion.cmake are the NifSkope directories also when it is added with add_subdirectory(): CMAKE_SOURCE_DIR and
# CMAKE_BINARY_DIR would be the parent project's. PROJECT_IS_TOP_LEVEL does not exist before project().
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(_ns_top_level TRUE)
else()
	set(_ns_top_level FALSE)
endif()

# Out-of-source only. Never write into the source tree: build/ is a tracked directory (docsys, VERSION,
# README.md.in), so "mkdir build && cd build && cmake .." is refused as well.
get_filename_component(_ns_src "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)
get_filename_component(_ns_bin "${CMAKE_CURRENT_BINARY_DIR}" REALPATH)
string(FIND "${_ns_bin}/" "${_ns_src}/" _ns_inside)
if(_ns_inside EQUAL 0)
	message(FATAL_ERROR
		"The build directory\n  ${_ns_bin}\nis inside the source tree\n  ${_ns_src}\n"
		"NifSkope must be built out of source, in a directory outside the checkout, e.g.\n"
		"  cmake --preset dev        (puts it next to the checkout, see CMakePresets.json)\n"
		"  cmake -S ${_ns_src} -B ${_ns_src}/../build-nifskope\n"
		"Remove the CMakeCache.txt and CMakeFiles/ that this run just created in ${_ns_bin}.")
endif()
unset(_ns_src)
unset(_ns_bin)
unset(_ns_inside)

# The qmake release build is the default, so is this one. Single-config generators only: CMAKE_CONFIGURATION_TYPES is
# still empty before project() even for Visual Studio, Xcode and Ninja Multi-Config, so the generator is asked instead.
# Those take their configuration from --config and get no CMAKE_BUILD_TYPE in the cache. Not as a subproject: the
# cache variable is the parent's too.
get_property(_ns_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_ns_top_level AND NOT _ns_multi_config AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()
unset(_ns_multi_config)

# macOS deployment target, as qmake: Qt 5.15's mkspec asks for 10.13, so does this build for x86_64. Apple silicon never ran
# anything older and the linker raises an arm64 binary to 11.0 whatever is asked, so 11.0 is stated there (current SDKs also
# warn "no longer supported by libc++" in every translation unit compiled for a target below 11.0). Setting
# CMAKE_OSX_DEPLOYMENT_TARGET or MACOSX_DEPLOYMENT_TARGET wins. Must be set before project(), so a subproject leaves it
# to its parent.
if(_ns_top_level AND APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
	if(CMAKE_OSX_ARCHITECTURES)
		set(_ns_archs "${CMAKE_OSX_ARCHITECTURES}")
	else() # the compiler's default: what the machine (or the Rosetta process running CMake) is
		cmake_host_system_information(RESULT _ns_archs QUERY OS_PLATFORM)
	endif()
	if(_ns_archs MATCHES "arm64")
		set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version to target")
	else()
		set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13 CACHE STRING "Minimum macOS version to target")
	endif()
	unset(_ns_archs)
endif()

unset(_ns_top_level)

# nifskope_require_files(<what> <hint> <file>...): stop at configure time, not in the middle of the build
function(nifskope_require_files what hint)
	foreach(_f IN LISTS ARGN)
		if(NOT EXISTS "${_f}")
			message(FATAL_ERROR "${what} is missing: ${_f}\n${hint}")
		endif()
	endforeach()
endfunction()
