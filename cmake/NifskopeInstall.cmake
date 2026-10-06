# Install rules, one layout per platform. They all follow from where the application looks for its files:
# next to the executable (QCoreApplication::applicationDirPath()), plus a fixed /usr/share/nifskope on Linux for
# most of them. BUILDING.md describes the result.
#
#   Windows   flat:   <prefix>/NifSkope.exe, Qt DLLs (windeployqt), nif.xml, shaders/, ...
#   macOS     bundle: <prefix>/NifSkope.app, data in Contents/Resources with symlinks in Contents/MacOS
#   Linux     self-contained application directory <prefix>/lib/nifskope/ and a bin/nifskope symlink; works
#             for any prefix. -DNIFSKOPE_LINUX_FHS_LAYOUT=ON gives bin/ + share/nifskope/ for distribution
#             packages (only complete with -DCMAKE_INSTALL_PREFIX=/usr, see below).

if(NOT CMAKE_INSTALL_DOCDIR)
	set(CMAKE_INSTALL_DOCDIR "share/doc/nifskope")
endif()
include(GNUInstallDirs)

if(WIN32)
	set(_app_dir .)
	set(_data_dir .)
	set(_doc_dir .)
elseif(APPLE)
	set(_app_dir .)
	set(_data_dir "NifSkope.app/Contents/Resources")
	set(_doc_dir "${_data_dir}")
elseif(NIFSKOPE_LINUX_FHS_LAYOUT)
	set(_app_dir "${CMAKE_INSTALL_BINDIR}")
	set(_data_dir "${CMAKE_INSTALL_DATADIR}/nifskope")
	set(_doc_dir "${CMAKE_INSTALL_DOCDIR}")
else()
	set(_app_dir "${CMAKE_INSTALL_LIBDIR}/nifskope")
	set(_data_dir "${_app_dir}")
	set(_doc_dir "${CMAKE_INSTALL_DOCDIR}")
endif()

install(TARGETS nifskope RUNTIME DESTINATION "${_app_dir}" BUNDLE DESTINATION .)
install(FILES ${NIFSKOPE_XML_FILES} "${NIFSKOPE_STYLE_FILE}" DESTINATION "${_data_dir}")
install(DIRECTORY "${NIFSKOPE_SHADER_DIR}" DESTINATION "${_data_dir}")
install(FILES ${NIFSKOPE_DOC_FILES} DESTINATION "${_doc_dir}")
if(APPLE)
	install(FILES "${NIFSKOPE_MACOS_LPROJ_FILE}" DESTINATION "${_data_dir}") # see NifskopeRuntime.cmake
endif()

# The Qt deployment tools sit next to qmake. They are needed by "cmake --install" only, so a Qt layout that has
# none (MSYS2, MacPorts, a distribution's Qt) can still configure and build: the install step is what fails, with a
# message, and -DNIFSKOPE_DEPLOY_QT=OFF skips the deployment altogether.
set(_qt_bin_hints "")
if(NIFSKOPE_DEPLOY_QT AND TARGET ${NIFSKOPE_QT}::qmake)
	get_target_property(_qmake ${NIFSKOPE_QT}::qmake IMPORTED_LOCATION)
	if(_qmake)
		get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
		set(_qt_bin_hints "${_qt_bin}")
	endif()
endif()

if(APPLE)
	# The code looks in Contents/MacOS. Symlinks keep it free of regular data files, which would break signing
	install(CODE "
		foreach(_entry ${NIFSKOPE_RUNTIME_ENTRIES})
			execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"../Resources/\${_entry}\"
				\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/NifSkope.app/Contents/MacOS/\${_entry}\")
		endforeach()")

	if(NIFSKOPE_DEPLOY_QT)
		# A Qt that is referenced through @rpath (the Qt installers, install-qt-action) needs an RPATH in the installed
		# executable: macdeployqt 5.15.2 resolves the @rpath references through it, copies the frameworks, and swaps
		# it for @executable_path/../Frameworks. CMake strips the build RPATH on install, which leaves nothing to resolve
		# and an executable that does not start ("no LC_RPATH's found"), so CMAKE_INSTALL_RPATH_USE_LINK_PATH is set in
		# the top-level file, for macOS too. Homebrew's Qt is referenced by absolute path and works either way.
		# "-codesign=-" signs ad hoc: arm64 refuses to run binaries whose signature the rewrite invalidated. The
		# signature also seals the links created above, so they must exist before this runs. macdeployqt prints ERROR
		# lines that are harmless: "Cannot resolve rpath" for Homebrew's webp plugin, "is not an object file" for each
		# data link in Contents/MacOS. One is not: when its otool calls time out (5.15.2 waits 30 seconds for each and
		# goes on with the empty answer: "Could not parse otool output" for the executable, "QProcess: Destroyed while
		# process otool is still running"), it finds no frameworks to copy, says nothing more under -always-overwrite,
		# signs the bare executable and exits with 0, and that bundle passes codesign --verify. So the result is
		# checked: every Qt framework the executable links, and the platform plugin, must be in the bundle. Frameworks
		# and PlugIns are removed first, so that an earlier install cannot satisfy the check. A Qt that is not built as
		# frameworks (a static one) has nothing to copy
		set(NIFSKOPE_MACDEPLOYQT_VERBOSE 1 CACHE STRING "macdeployqt -verbose level, 0 to 3: 1 reports errors only, 2 what it copies and signs, 3 every otool run")
		find_program(NIFSKOPE_MACDEPLOYQT macdeployqt HINTS ${_qt_bin_hints})
		if(NIFSKOPE_MACDEPLOYQT)
			set(_deployed_qt "")
			get_target_property(_qt_core ${NIFSKOPE_QT}::Core LOCATION)
			if(_qt_core MATCHES "\\.framework/")
				foreach(_component IN LISTS NIFSKOPE_QT_CORE_COMPONENTS NIFSKOPE_QT_APP_COMPONENTS)
					list(APPEND _deployed_qt "Frameworks/Qt${_component}.framework")
				endforeach()
				list(APPEND _deployed_qt "PlugIns/platforms/libqcocoa.dylib")
			endif()
			install(CODE "
				set(_app \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/NifSkope.app\")
				file(REMOVE_RECURSE \"\${_app}/Contents/Frameworks\" \"\${_app}/Contents/PlugIns\")
				execute_process(COMMAND \"${NIFSKOPE_MACDEPLOYQT}\" \"\${_app}\"
					-always-overwrite -no-strip -codesign=- -verbose=${NIFSKOPE_MACDEPLOYQT_VERBOSE} RESULT_VARIABLE _result)
				if(NOT _result EQUAL 0)
					message(FATAL_ERROR \"macdeployqt failed: \${_result}\")
				endif()
				set(_missing \"\")
				foreach(_file ${_deployed_qt})
					if(NOT EXISTS \"\${_app}/Contents/\${_file}\")
						list(APPEND _missing \"\${_file}\")
					endif()
				endforeach()
				if(_missing)
					string(REPLACE \";\" \", \" _missing \"\${_missing}\")
					message(FATAL_ERROR \"macdeployqt exited with 0 but did not deploy Qt: \${_app}/Contents lacks \${_missing}.\\n\"
						\"Look above for 'Could not parse otool output' and 'QProcess: Destroyed while process': Qt 5.15's \"
						\"macdeployqt gives every otool 30 seconds and goes on with an empty answer when that is not enough \"
						\"(seen on a fresh CI runner). Check that 'otool -L <executable>' answers, then install again.\")
				endif()")
		else()
			message(WARNING "macdeployqt was not found: building works, \"cmake --install\" will fail. Set NIFSKOPE_MACDEPLOYQT, or NIFSKOPE_DEPLOY_QT=OFF to install without deploying Qt")
			install(CODE "message(FATAL_ERROR \"macdeployqt was not found: set NIFSKOPE_MACDEPLOYQT, or configure with -DNIFSKOPE_DEPLOY_QT=OFF\")")
		endif()
	endif()
endif()

if(WIN32)
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		install(FILES "${PROJECT_SOURCE_DIR}/dep/NifMopp.dll" DESTINATION .)
	endif()

	if(NIFSKOPE_DEPLOY_QT)
		# --no-compiler-runtime: by default windeployqt copies the VC++ redistributable installer, not DLLs.
		# InstallRequiredSystemLibraries below puts the app-local vcruntime/msvcp DLLs next to the executable.
		find_program(NIFSKOPE_WINDEPLOYQT windeployqt HINTS ${_qt_bin_hints})
		if(NIFSKOPE_WINDEPLOYQT)
			install(CODE "
				execute_process(COMMAND \"${NIFSKOPE_WINDEPLOYQT}\" $<IF:$<CONFIG:Debug>,--debug,--release>
					--no-compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw --no-angle
					--dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/NifSkope.exe\"
					RESULT_VARIABLE _result)
				if(NOT _result EQUAL 0)
					message(FATAL_ERROR \"windeployqt failed: \${_result}\")
				endif()")
		else()
			message(WARNING "windeployqt was not found: building works, \"cmake --install\" will fail. Set NIFSKOPE_WINDEPLOYQT, or NIFSKOPE_DEPLOY_QT=OFF to install without deploying Qt")
			install(CODE "message(FATAL_ERROR \"windeployqt was not found: set NIFSKOPE_WINDEPLOYQT, or configure with -DNIFSKOPE_DEPLOY_QT=OFF\")")
		endif()
		set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
		include(InstallRequiredSystemLibraries)
	endif()
endif()

if(UNIX AND NOT APPLE)
	if(NIFSKOPE_LINUX_FHS_LAYOUT)
		# The fixed fallback in the code is /usr/share/nifskope, and the default textures (shaders/*.dds) are only
		# looked up beside the executable, so this layout is incomplete until the lookups are changed
		install(CODE "
			if(NOT \"\${CMAKE_INSTALL_PREFIX}\" STREQUAL \"/usr\")
				message(WARNING \"NIFSKOPE_LINUX_FHS_LAYOUT: NifSkope looks in /usr/share/nifskope, not in \${CMAKE_INSTALL_PREFIX}/share/nifskope\")
			endif()")
	else()
		if(NOT NIFSKOPE_EXECUTABLE_NAME) # set by the top-level file; empty would make the link point at the directory
			message(FATAL_ERROR "NIFSKOPE_EXECUTABLE_NAME is not set")
		endif()
		install(CODE "
			file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
			execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
				\"../${CMAKE_INSTALL_LIBDIR}/nifskope/${NIFSKOPE_EXECUTABLE_NAME}\"
				\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/nifskope\")")
	endif()

	install(FILES "${PROJECT_SOURCE_DIR}/install/linux-install/nifskope.desktop"
		DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
	install(FILES "${PROJECT_SOURCE_DIR}/res/nifskope.png"
		DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps")
	install(FILES
		"${PROJECT_SOURCE_DIR}/install/linux-install/vnd.gamebryo-nif.xml"
		"${PROJECT_SOURCE_DIR}/install/linux-install/vnd.gamebryo-kf.xml"
		"${PROJECT_SOURCE_DIR}/install/linux-install/vnd.gamebryo-kfm.xml"
		DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")
endif()
