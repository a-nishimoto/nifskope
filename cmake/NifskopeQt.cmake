# Qt discovery and the build switches that depend on it. The one place that knows the Qt major version: moving to Qt 6
# means editing this file, dropping the gate below, and porting the sources.
#
# find_package(QT NAMES ...) looks through every CMAKE_PREFIX_PATH entry for each name in turn, so the first
# prefix that holds either Qt wins. Point CMAKE_PREFIX_PATH at the Qt 5.15 installation (the presets do, from
# QT_ROOT_DIR) when a Qt 6 is installed as well. Inside one prefix Qt 6 comes first (Debian and Ubuntu put both under
# /usr), so there the Qt 5 choice has to be made explicit with -DQT_DIR=<prefix>/lib/cmake/Qt5.
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
set(NIFSKOPE_QT "Qt${QT_VERSION_MAJOR}")

if(QT_VERSION_MAJOR EQUAL 6)
	if(NOT NIFSKOPE_ALLOW_QT6)
		message(FATAL_ERROR
			"Found Qt ${QT_VERSION} in ${QT_DIR}, but NifSkope is not ported to Qt 6 yet.\n"
			"Blockers: QGLWidget/QGLFormat (src/glview.*, src/ui/widgets/uvedit.*), QRegExp, QDomDocument, "
			"QMetaType::registerComparators.\n"
			"Use Qt 5.15: put its prefix first in -DCMAKE_PREFIX_PATH=<Qt 5.15 prefix>, or, when Qt 5 and Qt 6 are installed "
			"in one prefix (such as /usr), choose it with -DQT_DIR=<prefix>/lib/cmake/Qt5.\n"
			"To work on the port anyway: -DNIFSKOPE_ALLOW_QT6=ON.")
	endif()
	message(WARNING "Qt 6 support is a work in progress: the sources do not compile against it yet")
	set(NIFSKOPE_QT_CORE_COMPONENTS Core Gui Widgets Core5Compat)
	set(NIFSKOPE_QT_APP_COMPONENTS Xml OpenGL OpenGLWidgets Network)
	set(_nifskope_cxx_standard_default 17)
	set(_nifskope_deprecated_before_default 0x060000)
else()
	if(QT_VERSION VERSION_LESS 5.15)
		message(FATAL_ERROR
			"Found Qt ${QT_VERSION} in ${QT_DIR}, but NifSkope needs Qt 5.15 or later (CI builds with 5.15.2).\n"
			"Put a Qt 5.15 installation first in -DCMAKE_PREFIX_PATH=<Qt 5.15 prefix>.")
	endif()
	set(NIFSKOPE_QT_CORE_COMPONENTS Core Gui Widgets)
	set(NIFSKOPE_QT_APP_COMPONENTS Xml OpenGL Network)
	set(_nifskope_cxx_standard_default 20)
	set(_nifskope_deprecated_before_default 0x051500)
endif()

# The language standard and the Qt deprecation level are build switches: -D<name>=<value> on the command line or in a
# preset (cacheVariables) changes them. The default depends on the Qt major, so they are not option()s with one fixed
# value; set(... CACHE) leaves a value that is already in the cache alone. qmake: NIFSKOPE_CXX_STANDARD and
# NIFSKOPE_QT_DEPRECATED_BEFORE, same names (NifSkope_settings.pri).
# The deprecation level is the minimum Qt, 5.15: the sources use no API that Qt 5.15 deprecated, and the build stops
# at the first one that comes back. A lower value (0x050300 was the default until Qt 5.15 became the minimum) declares
# the deprecated API again; NIFSKOPE_WERROR_DEPRECATED (CMakeLists.txt) is the other half of the check
set(NIFSKOPE_CXX_STANDARD ${_nifskope_cxx_standard_default} CACHE STRING
	"C++ standard NifSkope's own code is compiled as: 17 or 20")
set_property(CACHE NIFSKOPE_CXX_STANDARD PROPERTY STRINGS 17 20)
set(NIFSKOPE_QT_DEPRECATED_BEFORE ${_nifskope_deprecated_before_default} CACHE STRING
	"QT_DISABLE_DEPRECATED_BEFORE: Qt API deprecated before this hexadecimal Qt version is not declared (0x051500 is Qt 5.15)")
unset(_nifskope_cxx_standard_default)
unset(_nifskope_deprecated_before_default)
# 14 is not on the list: std::as_const, which the range-for loops over Qt containers use, is C++17
if(NOT NIFSKOPE_CXX_STANDARD MATCHES "^(17|20)$")
	message(FATAL_ERROR "NIFSKOPE_CXX_STANDARD is '${NIFSKOPE_CXX_STANDARD}': use 17 or 20")
endif()
if(NOT NIFSKOPE_QT_DEPRECATED_BEFORE MATCHES "^0[xX][0-9a-fA-F]+$")
	message(FATAL_ERROR
		"NIFSKOPE_QT_DEPRECATED_BEFORE is '${NIFSKOPE_QT_DEPRECATED_BEFORE}': use a hexadecimal Qt version such as 0x051500")
endif()

find_package(${NIFSKOPE_QT} REQUIRED COMPONENTS ${NIFSKOPE_QT_CORE_COMPONENTS} ${NIFSKOPE_QT_APP_COMPONENTS})

# Target lists, so that the rest of the tree never spells a Qt major version
set(NIFSKOPE_QT_CORE_TARGETS "")
foreach(_c IN LISTS NIFSKOPE_QT_CORE_COMPONENTS)
	list(APPEND NIFSKOPE_QT_CORE_TARGETS ${NIFSKOPE_QT}::${_c})
endforeach()
set(NIFSKOPE_QT_APP_TARGETS "")
foreach(_c IN LISTS NIFSKOPE_QT_APP_COMPONENTS)
	list(APPEND NIFSKOPE_QT_APP_TARGETS ${NIFSKOPE_QT}::${_c})
endforeach()
set(NIFSKOPE_QT_TEST_TARGETS ${NIFSKOPE_QT}::Test)
