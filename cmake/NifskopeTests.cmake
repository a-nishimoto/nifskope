# CTest registration for tests/ and for the application smoke tests.

# nifskope_register_tests(<test exe> <tst_*.cpp>...): one CTest test per test class. The classes are found by
# their REGISTER_TEST( tst_X ) line (tests/testregistry.h). The sources are configure dependencies, so adding a
# class re-runs CMake. The executable takes the class name as its first argument; its exit status is the number
# of failing classes. QT_QPA_PLATFORM=offscreen overrides whatever platform plugin the caller's shell selects.
function(nifskope_register_tests exe)
	foreach(_src IN LISTS ARGN)
		set(_file "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}")
		file(STRINGS "${_file}" _lines REGEX "^REGISTER_TEST\\(")
		foreach(_line IN LISTS _lines)
			string(REGEX REPLACE "^REGISTER_TEST\\( *([A-Za-z0-9_]+) *\\).*$" "\\1" _class "${_line}")
			add_test(NAME ${_class} COMMAND ${exe} ${_class} -o -,txt)
			set_tests_properties(${_class} PROPERTIES
				ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
				TIMEOUT 120
				LABELS unit)
		endforeach()
	endforeach()
endfunction()

# nifskope_register_app_tests(<exe target>): the application starts far enough to print its version (Windows:
# the release executable has the GUI subsystem and prints nothing) and finds the files it needs beside itself.
function(nifskope_register_app_tests exe)
	string(REPLACE "." "\\." _version_re "${NIFSKOPE_VERSION}")

	if(NOT WIN32)
		# NifModel::loadXML() runs before --version is handled: a missing nif.xml or kfm.xml shows an error
		# box, which the offscreen platform plugin reports on stderr
		add_test(NAME app_version COMMAND ${exe} --version)
		set_tests_properties(app_version PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			PASS_REGULAR_EXPRESSION "NifSkope [0-9]+\\.[0-9]+ ${_version_re}"
			FAIL_REGULAR_EXPRESSION "propagateSizeHints"
			TIMEOUT 60
			LABELS app)
	endif()

	string(REPLACE ";" "|" _entries "${NIFSKOPE_RUNTIME_ENTRIES}") # a ';' would not survive the test command line
	add_test(NAME app_layout
		COMMAND "${CMAKE_COMMAND}" "-DAPP_DIR=$<TARGET_FILE_DIR:${exe}>" "-DENTRIES=${_entries}"
			-P "${PROJECT_SOURCE_DIR}/cmake/CheckLayout.cmake")
	set_tests_properties(app_layout PROPERTIES LABELS app)
endfunction()
