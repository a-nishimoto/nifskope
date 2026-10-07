###############################
## NifSkope unit tests
###############################
# Standalone project: does not include or modify NifSkope.pro.
#
#   mkdir nifskope-tests && cd nifskope-tests       (outside the source tree)
#   qmake /path/to/nifskope/tests/tests.pro
#   make && make check
#
# Runs headless: tests/main.cpp selects the "offscreen" QPA platform
# unless QT_QPA_PLATFORM is already set.

TEMPLATE = app
TARGET   = nifskope_tests

QT += widgets testlib

CONFIG += testcase console no_testcase_installs
CONFIG -= app_bundle

# Source tree root; override with NIFSKOPE_ROOT=<dir> on the qmake command line
isEmpty(NIFSKOPE_ROOT): NIFSKOPE_ROOT = $$clean_path($$PWD/..)

# Minimum Qt version, C++ standard and Qt deprecation level, the same as NifSkope.pro
#	NIFSKOPE_CXX_STANDARD, NIFSKOPE_QT_DEPRECATED_BEFORE: see NifSkope_settings.pri
include($${NIFSKOPE_ROOT}/NifSkope_settings.pri)

# Same defines as NifSkope.pro so the shared sources compile identically
DEFINES += \
	QT_NO_CAST_FROM_BYTEARRAY \
	QT_NO_URL_CAST_FROM_STRING \
	QT_DISABLE_DEPRECATED_BEFORE=$$NIFSKOPE_QT_DEPRECATED_BEFORE

# MSVC, Qt 5.15.2 to 5.15.16: QVector/QList/QVarLengthArray hand stdext::checked_array_iterator to std::equal or
# std::copy, which the STL of Visual Studio 2022 17.8 and later deprecates (STL4043, C4996). Qt 5.15.17 no longer does
# (QTBUG-118993). qmake's -w44996 keeps it out of a /W3 build, so this is for parity with NifSkope.pro and the CMake
# build, where /we4996 makes it an error
*msvc*:DEFINES += _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING

# Where the tests find nif.xml / kfm.xml (the build/docsys submodule)
DEFINES += NIFSKOPE_SOURCE_DIR=\\\"$${NIFSKOPE_ROOT}\\\"

INCLUDEPATH += $${NIFSKOPE_ROOT} $${NIFSKOPE_ROOT}/src $${NIFSKOPE_ROOT}/lib $$PWD

# Output directories (same layout as NifSkope.pro, minus the VS special-casing)
build_pass|!debug_and_release {
	INTERMEDIATE = $${OUT_PWD}/GeneratedFiles

	# debug_and_release (the default with MSVC) generates Makefile.Debug and Makefile.Release from this one
	# project: each needs its own directories, or a release build links objects left by a debug build.
	# Relative to the build directory on purpose: qmake rewrites "debug" to "release" (and back) in these
	# paths, which would also hit a build directory named e.g. build-debug.
	debug_and_release {
		CONFIG(debug, debug|release) {
			INTERMEDIATE = GeneratedFiles/debug
		} else {
			INTERMEDIATE = GeneratedFiles/release
		}
	}

	UI_DIR = $${INTERMEDIATE}/.ui
	MOC_DIR = $${INTERMEDIATE}/.moc
	RCC_DIR = $${INTERMEDIATE}/.qrc
	OBJECTS_DIR = $${INTERMEDIATE}/.obj
}

###############################
## APP SOURCES UNDER TEST
###############################
# Model / data / XML / IO closure. No GL, no spells, no main window.

HEADERS += \
	$${NIFSKOPE_ROOT}/src/data/nifitem.h \
	$${NIFSKOPE_ROOT}/src/data/niftypes.h \
	$${NIFSKOPE_ROOT}/src/data/nifvalue.h \
	$${NIFSKOPE_ROOT}/src/io/nifstream.h \
	$${NIFSKOPE_ROOT}/src/model/basemodel.h \
	$${NIFSKOPE_ROOT}/src/model/kfmmodel.h \
	$${NIFSKOPE_ROOT}/src/model/nifmodel.h \
	$${NIFSKOPE_ROOT}/src/xml/nifexpr.h \
	$${NIFSKOPE_ROOT}/src/xml/xmlstream.h \
	$${NIFSKOPE_ROOT}/src/message.h \
	$${NIFSKOPE_ROOT}/src/spellbook.h \
	$${NIFSKOPE_ROOT}/src/ui/checkablemessagebox.h \
	$${NIFSKOPE_ROOT}/src/ui/qpaplatform.h \
	$${NIFSKOPE_ROOT}/src/ui/wheeldelta.h \
	$${NIFSKOPE_ROOT}/lib/half.h

SOURCES += \
	$${NIFSKOPE_ROOT}/src/data/niftypes.cpp \
	$${NIFSKOPE_ROOT}/src/data/nifvalue.cpp \
	$${NIFSKOPE_ROOT}/src/io/nifstream.cpp \
	$${NIFSKOPE_ROOT}/src/model/basemodel.cpp \
	$${NIFSKOPE_ROOT}/src/model/kfmmodel.cpp \
	$${NIFSKOPE_ROOT}/src/model/nifmodel.cpp \
	$${NIFSKOPE_ROOT}/src/xml/kfmxml.cpp \
	$${NIFSKOPE_ROOT}/src/xml/nifexpr.cpp \
	$${NIFSKOPE_ROOT}/src/xml/nifxml.cpp \
	$${NIFSKOPE_ROOT}/src/message.cpp \
	$${NIFSKOPE_ROOT}/src/spellbook.cpp \
	$${NIFSKOPE_ROOT}/src/ui/checkablemessagebox.cpp \
	$${NIFSKOPE_ROOT}/lib/half.cpp

FORMS += \
	$${NIFSKOPE_ROOT}/src/ui/checkablemessagebox.ui

###############################
## ZLIB / FSENGINE (BSA decompression)
###############################
# lib/zlib is compiled from the submodule exactly as NifSkope.pro does (core files only, no gz*.c).
# Pass CONFIG+=no_zlib to build just the NIF/KFM tests without it.

!no_zlib {
	INCLUDEPATH += $${NIFSKOPE_ROOT}/lib/fsengine
	DEFINES += LZ4_STATIC XXH_PRIVATE_API

	!*msvc*:QMAKE_CFLAGS += -isystem $${NIFSKOPE_ROOT}/lib/zlib
	else:INCLUDEPATH += $${NIFSKOPE_ROOT}/lib/zlib

	HEADERS += \
		$${NIFSKOPE_ROOT}/lib/fsengine/bsa.h \
		$${NIFSKOPE_ROOT}/lib/fsengine/fsengine.h \
		$$files($${NIFSKOPE_ROOT}/lib/zlib/*.h, false)

	SOURCES += \
		$${NIFSKOPE_ROOT}/lib/fsengine/bsa.cpp \
		$${NIFSKOPE_ROOT}/lib/fsengine/fsengine.cpp \
		$${NIFSKOPE_ROOT}/lib/lz4frame.c \
		$${NIFSKOPE_ROOT}/lib/xxhash.c \
		$${NIFSKOPE_ROOT}/lib/zlib/adler32.c \
		$${NIFSKOPE_ROOT}/lib/zlib/compress.c \
		$${NIFSKOPE_ROOT}/lib/zlib/crc32.c \
		$${NIFSKOPE_ROOT}/lib/zlib/deflate.c \
		$${NIFSKOPE_ROOT}/lib/zlib/infback.c \
		$${NIFSKOPE_ROOT}/lib/zlib/inffast.c \
		$${NIFSKOPE_ROOT}/lib/zlib/inflate.c \
		$${NIFSKOPE_ROOT}/lib/zlib/inftrees.c \
		$${NIFSKOPE_ROOT}/lib/zlib/trees.c \
		$${NIFSKOPE_ROOT}/lib/zlib/uncompr.c \
		$${NIFSKOPE_ROOT}/lib/zlib/zutil.c \
		tst_zlib.cpp
}

###############################
## TESTS
###############################

HEADERS += \
	testenv.h \
	testregistry.h

SOURCES += \
	main.cpp \
	testenv.cpp \
	tst_xmlload.cpp \
	tst_roundtrip.cpp \
	tst_savesafety.cpp \
	tst_niftypes.cpp \
	tst_nifvalue.cpp \
	tst_nifmodel.cpp \
	tst_kfmmodel.cpp

# vim: set filetype=config :
