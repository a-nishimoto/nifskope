cmake_minimum_required(VERSION 3.22...3.31)

# NifSkope, a NIF/KFM editor. The qmake project (NifSkope.pro, tests/tests.pro) is kept as the reference build.
# Build out of the source tree, see BUILDING.md and CMakePresets.json.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Before project(): refuse builds in or below the source tree, read build/VERSION, pick platform defaults
include(NifskopeBuildEnv)
include(NifskopeVersion)

project(NifSkope
	VERSION ${NIFSKOPE_VERSION_NUMERIC}
	DESCRIPTION "NIF and KFM file editor"
	HOMEPAGE_URL "https://github.com/niftools/nifskope"
	LANGUAGES C CXX)

include(CMakeDependentOption)

option(NIFSKOPE_BUILD_APP        "Build the NifSkope application (OFF: model library and tests only)" ON)
option(NIFSKOPE_BUILD_TESTS      "Build the Qt Test suite in tests/ and register it with CTest" ${PROJECT_IS_TOP_LEVEL})
cmake_dependent_option(NIFSKOPE_BUILD_BSA_TEST "Build the zlib/BSA test (tst_zlib.cpp) and the fsengine, zlib and lz4 code it needs; OFF leaves the NIF/KFM tests only (qmake: CONFIG+=no_zlib)" ON "NIFSKOPE_BUILD_TESTS" OFF)
option(NIFSKOPE_USE_SYSTEM_ZLIB  "Use a system or vcpkg zlib (>= 1.3.2) instead of lib/zlib" OFF)
option(NIFSKOPE_USE_SYSTEM_LZ4   "Use a system or vcpkg liblz4 (>= 1.7) instead of lib/lz4frame.c" OFF)
option(NIFSKOPE_USE_SYSTEM_QHULL "Use a system or vcpkg non-reentrant qhull instead of lib/qhull (experimental)" OFF)
option(NIFSKOPE_USE_SYSTEM_GLI   "Use a system or vcpkg gli + glm instead of lib/gli (experimental)" OFF)
option(NIFSKOPE_USE_GL_QPAINTER  "Define USE_GL_QPAINTER: paint the text statistics overlay with QPainter" OFF)
option(NIFSKOPE_ALLOW_QT6        "Continue with Qt 6 although the sources are not ported to it yet (porting work only)" OFF)
option(NIFSKOPE_KEEP_NDEBUG      "Keep -DNDEBUG in the Release flags (qmake never defines it, so assert() stays live unless this is ON)" OFF)
cmake_dependent_option(NIFSKOPE_DEPLOY_QT "Run windeployqt/macdeployqt during cmake --install" ON "WIN32 OR APPLE" OFF)
cmake_dependent_option(NIFSKOPE_LINUX_FHS_LAYOUT "Install to bin/ and share/nifskope/ instead of a self-contained lib/nifskope/" OFF "UNIX;NOT APPLE" OFF)
set(NIFSKOPE_REVISION_OVERRIDE "" CACHE STRING "Revision shown in the About box. Empty: first 7 digits of git HEAD, or none outside a git checkout")

# -fPIC for every target, like qmake. Standard and warning flags live in cmake/NifskopeCompile.cmake
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# An installed binary must still find a Qt in a non-system prefix (install-qt-action, /opt/Qt). This includes macOS: the
# installers' Qt is referenced through @rpath, and Qt 5.15.2's macdeployqt only copies the frameworks and points the
# executable at them (@executable_path/../Frameworks) when the executable has an RPATH to the Qt it was built with; with
# none, the installed bundle does not start ("no LC_RPATH's found"). Must be set before the targets are created
if(UNIX)
	set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
endif()
set(NIFSKOPE_RUNTIME_DIR "${PROJECT_BINARY_DIR}/bin")
# Defined here, not in src/: cmake/NifskopeInstall.cmake reads it too, and a variable set in a subdirectory does not
# reach its parent
if(WIN32 OR APPLE)
	set(NIFSKOPE_EXECUTABLE_NAME NifSkope)
else()
	set(NIFSKOPE_EXECUTABLE_NAME nifskope) # install/linux-install/nifskope.desktop has Exec=nifskope
endif()

include(NifskopeQt)
include(NifskopeCompile)
include(NifskopeFindDeps)
include(NifskopeRuntime)
include(NifskopeTests)

nifskope_detect_revision(NIFSKOPE_REVISION)
message(STATUS "NifSkope ${NIFSKOPE_VERSION}, revision '${NIFSKOPE_REVISION}', Qt ${QT_VERSION}")

if(NIFSKOPE_BUILD_TESTS)
	enable_testing()
endif()

add_subdirectory(lib)
add_subdirectory(src)
if(NIFSKOPE_BUILD_TESTS)
	add_subdirectory(tests)
endif()

# After src/: they refer to the nifskope target and to the file lists of cmake/NifskopeRuntime.cmake
if(NIFSKOPE_BUILD_APP)
	include(NifskopeInstall)
	include(NifskopeDocs)
endif()
