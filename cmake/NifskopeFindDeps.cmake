# Finders for the NIFSKOPE_USE_SYSTEM_* options. Each sets <out_target> to an imported target or stops with a
# message that says what to install. They run only when the option is ON; the vendored copies need no finder.

# liblz4: only its decompression frame API is used (LZ4F_createDecompressionContext, LZ4F_decompress,
# LZ4F_freeDecompressionContext, stable since 1.7). Packages differ: vcpkg and recent Debian ship a CMake
# package (lz4::lz4 / LZ4::lz4_static ...), Ubuntu 24.04 and Homebrew only a pkg-config file or nothing.
function(nifskope_find_lz4 out_target)
	find_package(lz4 CONFIG QUIET)
	foreach(_t LZ4::lz4 lz4::lz4 LZ4::lz4_static LZ4::lz4_shared)
		if(TARGET ${_t})
			set(${out_target} ${_t} PARENT_SCOPE)
			return()
		endif()
	endforeach()

	find_package(PkgConfig QUIET)
	if(PKG_CONFIG_FOUND)
		pkg_check_modules(NIFSKOPE_LZ4 QUIET IMPORTED_TARGET GLOBAL "liblz4>=1.7")
		if(NIFSKOPE_LZ4_FOUND)
			set(${out_target} PkgConfig::NIFSKOPE_LZ4 PARENT_SCOPE)
			return()
		endif()
	endif()

	find_path(NIFSKOPE_LZ4_INCLUDE_DIR lz4frame.h)
	find_library(NIFSKOPE_LZ4_LIBRARY NAMES lz4 liblz4)
	if(NIFSKOPE_LZ4_INCLUDE_DIR AND NIFSKOPE_LZ4_LIBRARY)
		add_library(nifskope_lz4_found UNKNOWN IMPORTED GLOBAL)
		set_target_properties(nifskope_lz4_found PROPERTIES
			IMPORTED_LOCATION "${NIFSKOPE_LZ4_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${NIFSKOPE_LZ4_INCLUDE_DIR}")
		set(${out_target} nifskope_lz4_found PARENT_SCOPE)
		return()
	endif()

	message(FATAL_ERROR "NIFSKOPE_USE_SYSTEM_LZ4=ON, but no liblz4 >= 1.7 was found (tried the lz4 CMake package, "
		"pkg-config liblz4, and a plain search for lz4frame.h and liblz4). Install liblz4-dev / lz4, "
		"or add its prefix to CMAKE_PREFIX_PATH.")
endfunction()

# qhull: src/lib/qhull.cpp uses the classic NON-reentrant libqhull API (global "qh"), which upstream only keeps
# as the static library Qhull::qhullstatic (qhullstatic.pc). vcpkg's dynamic triplets ship the reentrant
# Qhull::qhull_r only, which cannot be used without porting compute_convex_hull to libqhull_r.
function(nifskope_find_qhull out_target)
	find_package(Qhull CONFIG QUIET)
	if(TARGET Qhull::qhullstatic)
		set(${out_target} Qhull::qhullstatic PARENT_SCOPE)
		return()
	endif()

	find_package(PkgConfig QUIET)
	if(PKG_CONFIG_FOUND)
		pkg_check_modules(NIFSKOPE_QHULL QUIET IMPORTED_TARGET GLOBAL qhullstatic)
		if(NIFSKOPE_QHULL_FOUND)
			set(${out_target} PkgConfig::NIFSKOPE_QHULL PARENT_SCOPE)
			return()
		endif()
	endif()

	message(FATAL_ERROR "NIFSKOPE_USE_SYSTEM_QHULL=ON, but no non-reentrant static qhull (Qhull::qhullstatic or "
		"pkg-config qhullstatic) was found. NifSkope's qhull wrapper uses the classic libqhull API, not libqhull_r "
		"(vcpkg's qhull exposes only the reentrant one on dynamic triplets). Use the bundled lib/qhull instead.")
endfunction()

# gli + glm: header-only, taken as a pair. NifSkope includes <gli.hpp>, but packages install <gli/gli.hpp>,
# so the "gli" subdirectory of the include directory is added as well. The vendored copy is a 2017 snapshot with
# the glm 0.9.9 development API; whether gltexloaders.cpp compiles against the packaged versions is untested.
function(nifskope_find_gli out_target)
	find_package(gli CONFIG QUIET)
	find_package(glm CONFIG QUIET)
	if(NOT TARGET gli)
		message(FATAL_ERROR "NIFSKOPE_USE_SYSTEM_GLI=ON, but the gli CMake package was not found (vcpkg: gli, "
			"which also installs glm). Debian and Homebrew have no gli package: use the bundled lib/gli.")
	endif()
	set(_glm "")
	foreach(_t glm::glm-header-only glm::glm)
		if(TARGET ${_t})
			set(_glm ${_t})
			break()
		endif()
	endforeach()
	if(NOT _glm)
		message(FATAL_ERROR "NIFSKOPE_USE_SYSTEM_GLI=ON, but the glm CMake package was not found")
	endif()
	get_target_property(_dirs gli INTERFACE_INCLUDE_DIRECTORIES)
	set(_extra "")
	foreach(_d IN LISTS _dirs)
		list(APPEND _extra "${_d}/gli")
	endforeach()
	add_library(nifskope_gli_found INTERFACE)
	target_link_libraries(nifskope_gli_found INTERFACE gli ${_glm})
	target_include_directories(nifskope_gli_found SYSTEM INTERFACE ${_extra})
	set(${out_target} nifskope_gli_found PARENT_SCOPE)
endfunction()
