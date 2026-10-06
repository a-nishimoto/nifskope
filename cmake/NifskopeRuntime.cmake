# Files the application reads at run time, next to its executable (QCoreApplication::applicationDirPath(); main() also
# makes that directory the working directory). This replaces qmake's pre/post-link steps. Where the code looks:
#   nif.xml, kfm.xml          src/xml/nifxml.cpp, kfmxml.cpp, at start-up; an error box when missing
#   style.qss                 src/nifskope_ui.cpp (optional)
#   shaders/*.vert|frag|prog  src/gl/renderer.cpp
#   shaders/*.dds             src/gl/renderer.cpp, gltex.cpp, relative to the working directory
#   Linux only: the same names in /usr/share/nifskope, except shaders/*.dds
#   skel.dat and the icons are compiled in (res/nifskope.qrc)

# nif.xml and kfm.xml live in the build/docsys submodule. Both the application and the tests need them
nifskope_require_files("A NifSkope data file" "Run: git submodule update --init --recursive"
	"${PROJECT_SOURCE_DIR}/build/docsys/nifxml/nif.xml"
	"${PROJECT_SOURCE_DIR}/build/docsys/kfmxml/kfm.xml")

set(NIFSKOPE_XML_FILES
	"${PROJECT_SOURCE_DIR}/build/docsys/nifxml/nif.xml"
	"${PROJECT_SOURCE_DIR}/build/docsys/kfmxml/kfm.xml")
set(NIFSKOPE_STYLE_FILE "${PROJECT_SOURCE_DIR}/res/style.qss")
# A whole directory is copied, as qmake did: 5 vertex, 6 fragment, 6 program files and the 7 default textures
set(NIFSKOPE_SHADER_DIR "${PROJECT_SOURCE_DIR}/res/shaders")

# README.txt, CHANGELOG.txt, LICENSE.txt, generated into the build tree. qmake wrote README.md into the source
# tree (a sed pre-link step) and copied the three files with a new extension; this does neither.
set(NIFSKOPE_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
set(VERSION "${NIFSKOPE_VERSION}") # the placeholder in build/README.md.in is @VERSION@
configure_file("${PROJECT_SOURCE_DIR}/build/README.md.in" "${NIFSKOPE_GENERATED_DIR}/README.txt" @ONLY)
unset(VERSION)
configure_file("${PROJECT_SOURCE_DIR}/CHANGELOG.md" "${NIFSKOPE_GENERATED_DIR}/CHANGELOG.txt" COPYONLY)
configure_file("${PROJECT_SOURCE_DIR}/LICENSE.md" "${NIFSKOPE_GENERATED_DIR}/LICENSE.txt" COPYONLY)
set(NIFSKOPE_DOC_FILES
	"${NIFSKOPE_GENERATED_DIR}/README.txt"
	"${NIFSKOPE_GENERATED_DIR}/CHANGELOG.txt"
	"${NIFSKOPE_GENERATED_DIR}/LICENSE.txt")

# macOS: qmake puts an empty file Contents/Resources/empty.lproj into every bundle. With CFBundleAllowMixedLocalizations
# (cmake/Info.plist.in) it makes AppKit treat the app as localised, so that the menu items and file dialogs it supplies
# itself follow the user's language instead of falling back to English.
if(APPLE)
	set(NIFSKOPE_MACOS_LPROJ_FILE "${NIFSKOPE_GENERATED_DIR}/empty.lproj")
	file(WRITE "${NIFSKOPE_MACOS_LPROJ_FILE}" "")
endif()

# Everything the layout check (cmake/CheckLayout.cmake) expects beside the executable
set(NIFSKOPE_RUNTIME_ENTRIES nif.xml kfm.xml style.qss shaders README.txt CHANGELOG.txt LICENSE.txt)

# nifskope_stage_runtime(<exe target>): build-tree copy of the runtime files, an ALL target the executable
# depends on. A custom target (not POST_BUILD) so that editing a shader refreshes without relinking.
#   Windows, Linux: next to the executable.
#   macOS: in Contents/Resources, with symlinks in Contents/MacOS where the code looks. Regular data files in
#          Contents/MacOS would make the bundle unsignable ("code object is not signed at all").
function(nifskope_stage_runtime exe)
	set(_exe_dir "$<TARGET_FILE_DIR:${exe}>")
	if(APPLE)
		set(_data "$<TARGET_BUNDLE_CONTENT_DIR:${exe}>/Resources")
	else()
		set(_data "${_exe_dir}")
	endif()

	set(_commands
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${_data}"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${NIFSKOPE_XML_FILES} "${NIFSKOPE_STYLE_FILE}" ${NIFSKOPE_DOC_FILES} "${_data}"
		COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_data}/shaders"
		COMMAND "${CMAKE_COMMAND}" -E copy_directory "${NIFSKOPE_SHADER_DIR}" "${_data}/shaders")

	if(APPLE)
		list(APPEND _commands
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${NIFSKOPE_MACOS_LPROJ_FILE}" "${_data}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${_exe_dir}")
		foreach(_entry IN LISTS NIFSKOPE_RUNTIME_ENTRIES)
			list(APPEND _commands
				COMMAND "${CMAKE_COMMAND}" -E create_symlink "../Resources/${_entry}" "${_exe_dir}/${_entry}")
		endforeach()
	endif()

	# dep/NifMopp.dll is a 32-bit DLL, loaded with LoadLibraryA by the MOPP spell
	if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
		list(APPEND _commands
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/dep/NifMopp.dll" "${_data}")
	endif()

	add_custom_target(nifskope_runtime ALL ${_commands} COMMENT "Staging the NifSkope runtime files" VERBATIM)
	add_dependencies(${exe} nifskope_runtime)
endfunction()
