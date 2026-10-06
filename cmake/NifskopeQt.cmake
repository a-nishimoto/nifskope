# Qt discovery. The one place that knows the Qt major version: moving to Qt 6 means editing this
# file, dropping the gate below, and porting the sources.
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
			"Blockers: QGLWidget/QGLFormat (src/glview.*, src/ui/widgets/uvedit.*), QXmlSimpleReader SAX parsing "
			"(src/xml/nifxml.cpp, kfmxml.cpp), QRegExp, QDomDocument, QMetaType::registerComparators.\n"
			"Use Qt 5.15: put its prefix first in -DCMAKE_PREFIX_PATH=<Qt 5.15 prefix>, or, when Qt 5 and Qt 6 are installed "
			"in one prefix (such as /usr), choose it with -DQT_DIR=<prefix>/lib/cmake/Qt5.\n"
			"To work on the port anyway: -DNIFSKOPE_ALLOW_QT6=ON.")
	endif()
	message(WARNING "Qt 6 support is a work in progress: the sources do not compile against it yet")
	set(NIFSKOPE_QT_CORE_COMPONENTS Core Gui Widgets Xml Core5Compat)
	set(NIFSKOPE_QT_APP_COMPONENTS OpenGL OpenGLWidgets Network)
	set(NIFSKOPE_CXX_STANDARD 17)
	set(NIFSKOPE_QT_DEPRECATED_BEFORE 0x060000)
else()
	if(QT_VERSION VERSION_LESS 5.7)
		message(FATAL_ERROR "Qt ${QT_VERSION} is too old: NifSkope needs Qt 5.7 or later (5.15 is what CI builds with)")
	endif()
	set(NIFSKOPE_QT_CORE_COMPONENTS Core Gui Widgets Xml)
	set(NIFSKOPE_QT_APP_COMPONENTS OpenGL Network)
	set(NIFSKOPE_CXX_STANDARD 14)
	set(NIFSKOPE_QT_DEPRECATED_BEFORE 0x050300)
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
