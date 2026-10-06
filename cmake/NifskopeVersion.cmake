# Version and revision, the CMake counterpart of getVersion()/getRevision() in NifSkope_functions.pri.

# ---- Version: build/VERSION (e.g. 2.0.dev7). Included before project(), which wants a numeric version, from NifSkope's
# own CMakeLists.txt (hence CMAKE_CURRENT_SOURCE_DIR, which is also right for a subproject).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/build/VERSION" NIFSKOPE_VERSION LIMIT_COUNT 1)
if(NOT NIFSKOPE_VERSION MATCHES "^[0-9]+\\.[0-9]+")
	message(FATAL_ERROR "build/VERSION must start with <major>.<minor>, found '${NIFSKOPE_VERSION}'")
endif()
# "2.0.dev7" -> "2.0"; the application derives its own "2.0" from the full string at run time
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?" NIFSKOPE_VERSION_NUMERIC "${NIFSKOPE_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/build/VERSION")

# ---- Revision: the first 7 digits of HEAD, evaluated at configure time like qmake did.
# Sets <out_var>; empty unless the source directory itself is a git checkout (then NIFSKOPE_REVISION is simply not
# defined, as with qmake).
# Also registers the git files that change when HEAD moves, so that the build re-runs CMake after a commit,
# a checkout or a reset (verified: commit, pack-refs + commit, branch switch, detached HEAD, linked worktree).
function(nifskope_detect_revision out_var)
	set(${out_var} "" PARENT_SCOPE)

	if(NIFSKOPE_REVISION_OVERRIDE)
		set(${out_var} "${NIFSKOPE_REVISION_OVERRIDE}" PARENT_SCOPE)
		return()
	endif()

	# Only this directory's own repository counts, like qmake, which reads <source>/.git/HEAD. Without the test, "git rev-parse"
	# would walk up: a source archive unpacked inside some other work tree (a dotfiles or packaging repository, a
	# monorepo) would be stamped with that repository's HEAD. EXISTS is true for the .git file of linked worktrees and
	# submodule checkouts as well as for the .git directory
	if(NOT EXISTS "${PROJECT_SOURCE_DIR}/.git")
		return()
	endif()

	find_package(Git QUIET)
	if(NOT GIT_FOUND)
		return()
	endif()

	execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
		WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
		OUTPUT_VARIABLE _head RESULT_VARIABLE _result
		ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
	if(NOT _result EQUAL 0 OR NOT _head MATCHES "^[0-9a-f]+$")
		return()
	endif()
	# Not "rev-parse --short=7": git lengthens it when 7 digits are ambiguous, qmake always took 7
	string(SUBSTRING "${_head}" 0 7 _revision)
	set(${out_var} "${_revision}" PARENT_SCOPE)

	# HEAD itself (branch switch, detach), its reflog (every commit and reset, also after "git pack-refs"
	# has removed the loose branch file), the branch file, and packed-refs. "--git-path" resolves worktrees
	# and submodule checkouts, where .git is a file. Only existing files may be listed.
	set(_names HEAD logs/HEAD packed-refs)
	execute_process(COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
		WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
		OUTPUT_VARIABLE _ref RESULT_VARIABLE _result
		ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
	if(_result EQUAL 0 AND _ref)
		list(APPEND _names "${_ref}")
	endif()
	foreach(_name IN LISTS _names)
		execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path "${_name}"
			WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
			OUTPUT_VARIABLE _file RESULT_VARIABLE _result
			ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
		if(_result EQUAL 0 AND _file)
			get_filename_component(_file "${_file}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
			if(EXISTS "${_file}")
				set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
			endif()
		endif()
	endforeach()
endfunction()
