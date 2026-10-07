# NifSkope's own code, in two targets:
#
#   nifskope_core  (STATIC)      the data/model tier that needs no OpenGL and no main window: the NIF/KFM item
#                                models, value types, XML description parsing, streams. tests/ links this, so it
#                                does not list sources again. It is exactly what tests/tests.pro compiles.
#   nifskope       (executable)  everything else. The spells (119 REGISTER_SPELL static registrars) and
#                                res/nifskope.qrc stay here, never in a static library: the linker drops an
#                                archive member nothing references, and the app would start with no spells and no
#                                icons without a link error.

set(NIFSKOPE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# ==== nifskope_core ============================================================================================
add_library(nifskope_core STATIC
	data/niftypes.cpp
	data/nifvalue.cpp
	io/nifstream.cpp
	model/basemodel.cpp
	model/kfmmodel.cpp
	model/nifmodel.cpp
	xml/kfmxml.cpp
	xml/nifexpr.cpp
	xml/nifxml.cpp
	message.cpp
	spellbook.cpp
	ui/checkablemessagebox.cpp

	data/nifitem.h
	data/niftypes.h
	data/nifvalue.h
	io/nifstream.h
	model/basemodel.h
	model/kfmmodel.h
	model/nifmodel.h
	xml/nifexpr.h
	xml/xmlstream.h
	message.h
	spellbook.h
	ui/checkablemessagebox.h
	ui/qpaplatform.h
	ui/wheeldelta.h
	ui/checkablemessagebox.ui)

set_target_properties(nifskope_core PROPERTIES AUTOMOC ON AUTOUIC ON)
target_include_directories(nifskope_core
	PUBLIC  "${NIFSKOPE_SRC_DIR}"          # "data/...", "model/...", "ui/..." as every source writes them
	PRIVATE "${PROJECT_SOURCE_DIR}")       # io/nifstream.cpp: #include "lib/half.h"
target_link_libraries(nifskope_core
	PUBLIC  nifskope_compile_options ${NIFSKOPE_QT_CORE_TARGETS}
	PRIVATE nifskope_warnings nifskope_half)

if(NOT NIFSKOPE_BUILD_APP)
	return()
endif()

# ==== nifskope (the application) ===============================================================================
# Link the legacy libGL (qmake: -lGL), not GLVND's libOpenGL + libGLX, which FindOpenGL prefers by default (policy
# CMP0072 is NEW since CMake 3.11). gl/gltex.cpp calls glActiveTextureARB and glClientActiveTextureARB directly and only
# resolves them at run time on Windows, and libOpenGL has no ARB-suffixed entry points: they are in libGL alone, so the
# link fails there ("DSO missing from command line"). Windows and macOS ignore the setting.
set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)
if(UNIX AND NOT APPLE)
	# Without a libGL.so (libgl-dev) FindOpenGL quietly falls back to GLVND: say so now instead of failing in the linker
	if(NOT OPENGL_gl_LIBRARY)
		message(FATAL_ERROR "libGL was not found: install the OpenGL development package (Debian/Ubuntu: libgl-dev, or libgl1-mesa-dev), or set OPENGL_gl_LIBRARY")
	endif()
	# CMake 3.x reports "Found OpenGL: .../libOpenGL.so" here whichever library OpenGL::GL is
	message(STATUS "OpenGL::GL is ${OPENGL_gl_LIBRARY}")
endif()
# FindOpenGL does not require GLU and creates OpenGL::GLU only when it found it
if(NOT TARGET OpenGL::GLU)
	message(FATAL_ERROR "GLU was not found: install the GLU development package (Debian/Ubuntu: libglu1-mesa-dev), or set OPENGL_glu_LIBRARY")
endif()

add_executable(nifskope MACOSX_BUNDLE
	# gl
	gl/bsshape.cpp
	gl/controllers.cpp
	gl/glcontroller.cpp
	gl/glmarker.cpp
	gl/glmesh.cpp
	gl/glnode.cpp
	gl/glparticles.cpp
	gl/glproperty.cpp
	gl/glscene.cpp
	gl/gltex.cpp
	gl/gltexloaders.cpp
	gl/gltools.cpp
	gl/renderer.cpp
	# io
	io/material.cpp
	# lib/importex
	lib/importex/3ds.cpp
	lib/importex/importex.cpp
	lib/importex/obj.cpp
	lib/importex/col.cpp
	# lib
	lib/nvtristripwrapper.cpp
	lib/qhull.cpp
	# model
	model/nifdelegate.cpp
	model/nifproxymodel.cpp
	model/undocommands.cpp
	# spells
	spells/animation.cpp
	spells/blocks.cpp
	spells/bounds.cpp
	spells/color.cpp
	spells/flags.cpp
	spells/fo3only.cpp
	spells/havok.cpp
	spells/headerstring.cpp
	spells/light.cpp
	spells/materialedit.cpp
	spells/mesh.cpp
	spells/misc.cpp
	spells/moppcode.cpp
	spells/morphctrl.cpp
	spells/normals.cpp
	spells/optimize.cpp
	spells/sanitize.cpp
	spells/skeleton.cpp
	spells/stringpalette.cpp
	spells/strippify.cpp
	spells/tangentspace.cpp
	spells/texture.cpp
	spells/transform.cpp
	# ui/widgets
	ui/widgets/colorwheel.cpp
	ui/widgets/fileselect.cpp
	ui/widgets/floatedit.cpp
	ui/widgets/floatslider.cpp
	ui/widgets/groupbox.cpp
	ui/widgets/inspect.cpp
	ui/widgets/lightingwidget.cpp
	ui/widgets/nifcheckboxlist.cpp
	ui/widgets/nifeditors.cpp
	ui/widgets/nifview.cpp
	ui/widgets/refrbrowser.cpp
	ui/widgets/uvedit.cpp
	ui/widgets/valueedit.cpp
	ui/widgets/xmlcheck.cpp
	# ui
	ui/about_dialog.cpp
	ui/settingsdialog.cpp
	ui/settingspane.cpp
	# (src)
	glview.cpp
	main.cpp
	nifskope.cpp
	nifskope_ui.cpp
	version.cpp

	# Headers: for the IDE, and for AUTOMOC, which only scans headers that are listed (or have a same-named
	# .cpp). gl/icontrollable.h has Q_OBJECT and no .cpp, so it has to be here.
	gl/marker/constraints.h
	gl/marker/furniture.h
	gl/bsshape.h
	gl/controllers.h
	gl/glcontroller.h
	gl/glmarker.h
	gl/glmesh.h
	gl/glnode.h
	gl/glparticles.h
	gl/glproperty.h
	gl/glscene.h
	gl/gltex.h
	gl/gltexloaders.h
	gl/gltools.h
	gl/icontrollable.h
	gl/renderer.h
	io/material.h
	lib/importex/3ds.h
	lib/nvtristripwrapper.h
	lib/qhull.h
	model/nifproxymodel.h
	model/undocommands.h
	spells/blocks.h
	spells/mesh.h
	spells/misc.h
	spells/sanitize.h
	spells/skeleton.h
	spells/stringpalette.h
	spells/tangentspace.h
	spells/texture.h
	spells/transform.h
	ui/widgets/colorwheel.h
	ui/widgets/fileselect.h
	ui/widgets/floatedit.h
	ui/widgets/floatslider.h
	ui/widgets/groupbox.h
	ui/widgets/inspect.h
	ui/widgets/lightingwidget.h
	ui/widgets/nifcheckboxlist.h
	ui/widgets/nifeditors.h
	ui/widgets/nifview.h
	ui/widgets/refrbrowser.h
	ui/widgets/uvedit.h
	ui/widgets/valueedit.h
	ui/widgets/xmlcheck.h
	ui/about_dialog.h
	ui/settingsdialog.h
	ui/settingspane.h
	glview.h
	nifskope.h
	version.h

	# Forms. checkablemessagebox.ui belongs to nifskope_core: a .ui and every file that includes its ui_*.h
	# must be in the same target
	ui/about_dialog.ui
	ui/nifskope.ui
	ui/settingsdialog.ui
	ui/settingsgeneral.ui
	ui/settingsrender.ui
	ui/settingsresources.ui
	ui/widgets/lightingwidget.ui

	# Icons and skel.dat, compiled into the executable
	"${PROJECT_SOURCE_DIR}/res/nifskope.qrc")

set_target_properties(nifskope PROPERTIES
	AUTOMOC ON
	AUTOUIC ON
	AUTORCC ON
	# nifskope.cpp and nifskope_ui.cpp (in src/) include ui_nifskope.h, whose .ui file is in src/ui/. AUTOUIC only
	# looks beside the including file unless told where else to look. Listing the .ui file in the sources is not enough.
	AUTOUIC_SEARCH_PATHS "${NIFSKOPE_SRC_DIR}/ui"
	RUNTIME_OUTPUT_DIRECTORY "${NIFSKOPE_RUNTIME_DIR}"
	# qmake: CONFIG -= console in release, += console in debug
	WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>")

set_target_properties(nifskope PROPERTIES OUTPUT_NAME "${NIFSKOPE_EXECUTABLE_NAME}") # set in the top-level file

# No include directory of its own for lib/ ("dds.h" in gl/gltexloaders.cpp, <fsengine/...>): nifskope_fsengine exports
# it, after the zlib shim directory (see lib/CMakeLists.txt), and an own directory would be searched before that one

target_compile_definitions(nifskope PRIVATE
	"NIFSKOPE_VERSION=\"${NIFSKOPE_VERSION}\""
	$<$<BOOL:${NIFSKOPE_USE_GL_QPAINTER}>:USE_GL_QPAINTER>)
# The revision changes with every commit, so it is defined for the only file that shows it. Recompiling one file
# per commit instead of every file, as qmake's global define did.
if(NIFSKOPE_REVISION)
	set_source_files_properties(ui/about_dialog.cpp PROPERTIES
		COMPILE_DEFINITIONS "NIFSKOPE_REVISION=\"${NIFSKOPE_REVISION}\"")
endif()

target_link_libraries(nifskope PRIVATE
	nifskope_core
	nifskope_fsengine
	nifskope_nvtristrip
	nifskope_qhull
	nifskope_gli
	nifskope_warnings
	${NIFSKOPE_QT_APP_TARGETS}
	OpenGL::GL
	OpenGL::GLU)

if(WIN32)
	target_sources(nifskope PRIVATE "${PROJECT_SOURCE_DIR}/res/icon.rc")
endif()

if(MSVC)
	# qmake's msvc-desktop.conf links every executable with a dependency on the common controls 6.0 assembly; without it
	# the system's themed controls and native dialogs get the classic look. Qt 5's CMake config does not add it
	target_link_options(nifskope PRIVATE
		"/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' publicKeyToken='6595b64144ccf1df' language='*' processorArchitecture='*'")
endif()

if(APPLE)
	# Info.plist.in has no NSHighResolutionCapable, like qmake's: the bundle runs at 1x on Retina displays. GLView and
	# UVWidget pass logical widget sizes to glViewport and the picking framebuffer, which is only right at 1x. Turn the
	# key on in the change that makes them devicePixelRatio-aware, not before.

	# macdeployqt rewrites install names in the executable, which needs room in the Mach-O header
	target_link_options(nifskope PRIVATE -Wl,-headerpad_max_install_names)
	set(NIFSKOPE_MACOS_MIN_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}") # used by cmake/Info.plist.in
	set_target_properties(nifskope PROPERTIES
		MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/Info.plist.in"
		MACOSX_BUNDLE_BUNDLE_NAME "NifSkope"
		MACOSX_BUNDLE_GUI_IDENTIFIER "org.niftools.nifskope"
		MACOSX_BUNDLE_SHORT_VERSION_STRING "${NIFSKOPE_VERSION}"
		MACOSX_BUNDLE_BUNDLE_VERSION "${NIFSKOPE_VERSION_NUMERIC}" # CFBundleVersion is dotted integers; "2.0.dev7" is not
		MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2005-2015, NIF File Format Library and Tools")
	if(EXISTS "${PROJECT_SOURCE_DIR}/res/nifskope.icns")
		set_source_files_properties("${PROJECT_SOURCE_DIR}/res/nifskope.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
		target_sources(nifskope PRIVATE "${PROJECT_SOURCE_DIR}/res/nifskope.icns")
		set_target_properties(nifskope PROPERTIES MACOSX_BUNDLE_ICON_FILE "nifskope.icns")
	else()
		set_target_properties(nifskope PROPERTIES MACOSX_BUNDLE_ICON_FILE "")
	endif()
endif()

nifskope_stage_runtime(nifskope)
if(NIFSKOPE_BUILD_TESTS)
	nifskope_register_app_tests(nifskope)
endif()
