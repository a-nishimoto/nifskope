# "docs": the nif.xml reference pages (build/docsys, a Python script) written next to the executable, where the
# reference browser looks for doc/index.html. Not part of ALL. qmake ran the script inside the submodule and left
# the pages and .pyc files there; this one writes only into the build tree.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND OR NOT EXISTS "${PROJECT_SOURCE_DIR}/build/docsys/nifxml_doc.py")
	return()
endif()

set(_docs_env PYTHONDONTWRITEBYTECODE=1)
if(Python3_VERSION VERSION_GREATER_EQUAL 3.12)
	# nifxml_doc.py imports distutils.dir_util, which Python 3.12 removed; cmake/pyshim has the one function it uses
	list(APPEND _docs_env "PYTHONPATH=${PROJECT_SOURCE_DIR}/cmake/pyshim")
endif()

if(APPLE)
	set(_docs_root "$<TARGET_BUNDLE_CONTENT_DIR:nifskope>/Resources")
else()
	set(_docs_root "$<TARGET_FILE_DIR:nifskope>")
endif()

# The script writes <root>/doc/*.html but does not create the directory (qmake's rule ran mkdir first)
set(_docs_commands
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${_docs_root}/doc"
	COMMAND "${CMAKE_COMMAND}" -E env ${_docs_env} "${Python3_EXECUTABLE}" nifxml_doc.py -p "${_docs_root}"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different
		"${PROJECT_SOURCE_DIR}/build/docsys/doc/docsys.css" "${PROJECT_SOURCE_DIR}/build/docsys/doc/favicon.ico"
		"${_docs_root}/doc")
if(APPLE)
	list(APPEND _docs_commands
		COMMAND "${CMAKE_COMMAND}" -E create_symlink "../Resources/doc" "$<TARGET_FILE_DIR:nifskope>/doc")
endif()

add_custom_target(docs ${_docs_commands}
	WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/build/docsys"
	COMMENT "Generating the nif.xml reference pages"
	VERBATIM)
add_dependencies(docs nifskope_runtime)
