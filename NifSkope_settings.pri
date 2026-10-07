###############################
## BUILD SETTINGS
###############################
# Included by NifSkope.pro and tests/tests.pro, so that both compile the shared sources the same way. The CMake build
# has the same two switches under the same names (cmake/NifskopeQt.cmake). Set them on the qmake command line
#
#	qmake NIFSKOPE_CXX_STANDARD=17 NIFSKOPE_QT_DEPRECATED_BEFORE=0x050300 /path/to/NifSkope.pro
#
# in a fresh build directory: an existing one keeps the objects it compiled with the old values.

# Require Qt 5.15 or higher
contains(QT_VERSION, ^5\\.([0-9]|1[0-4])\\..*) {
	message("Cannot build NifSkope with Qt version $${QT_VERSION}")
	error("Minimum required version is Qt 5.15")
}

# C++ standard: 17 or 20. Not 14: std::as_const, which the range-for loops over Qt containers use, is C++17
isEmpty(NIFSKOPE_CXX_STANDARD): NIFSKOPE_CXX_STANDARD = 20

# Qt 5's qmake spells C++17 "c++1z" and C++20 "c++2a" (there is no "c++20"), with GNU extensions on
equals(NIFSKOPE_CXX_STANDARD, 17): CONFIG += c++1z
else: equals(NIFSKOPE_CXX_STANDARD, 20): CONFIG += c++2a
else: error("NIFSKOPE_CXX_STANDARD is $${NIFSKOPE_CXX_STANDARD}: use 17 or 20")

# C++20 needs a compiler that has it: GCC 10, Clang 10, Apple clang 12, MSVC 2019 16.11 (cl 19.29.30129, the first
# with /std:c++20). BUILDING.md lists the same
equals(NIFSKOPE_CXX_STANDARD, 20) {
	gcc:!clang:lessThan(QMAKE_GCC_MAJOR_VERSION, 10): error("C++20 needs GCC 10 or later, found GCC $${QMAKE_GCC_MAJOR_VERSION}")
	!isEmpty(QMAKE_CLANG_MAJOR_VERSION):lessThan(QMAKE_CLANG_MAJOR_VERSION, 10): error("C++20 needs Clang 10 or later, found Clang $${QMAKE_CLANG_MAJOR_VERSION}")
	!isEmpty(QMAKE_APPLE_CLANG_MAJOR_VERSION):lessThan(QMAKE_APPLE_CLANG_MAJOR_VERSION, 12): error("C++20 needs Apple clang 12 (Xcode 12) or later, found Apple clang $${QMAKE_APPLE_CLANG_MAJOR_VERSION}")
	msvc {
		!isEmpty(QMAKE_MSC_FULL_VER):lessThan(QMAKE_MSC_FULL_VER, 192930129): error("C++20 needs MSVC 2019 16.11 or later, found _MSC_FULL_VER $${QMAKE_MSC_FULL_VER}")
		# Qt 5's mkspec turns c++2a into /std:c++latest, which moves on with every Visual Studio release
		QMAKE_CXXFLAGS_CXX2A = -std:c++20
	}
}

# QT_DISABLE_DEPRECATED_BEFORE: Qt API deprecated before this version is not declared. A hexadecimal Qt version,
# 0x050300 is Qt 5.3 and 0x051500 is Qt 5.15. The default is the minimum Qt: the sources use no API that Qt 5.15
# deprecated, and the build stops at the first one that comes back
isEmpty(NIFSKOPE_QT_DEPRECATED_BEFORE): NIFSKOPE_QT_DEPRECATED_BEFORE = 0x051500
!contains(NIFSKOPE_QT_DEPRECATED_BEFORE, ^0[xX][0-9a-fA-F]+$) {
	error("NIFSKOPE_QT_DEPRECATED_BEFORE is $${NIFSKOPE_QT_DEPRECATED_BEFORE}: use a hexadecimal Qt version such as 0x051500")
}

# Warnings: the mkspec's warn_on adds -Wall -Wextra for GCC and clang. -Wextra includes -Wimplicit-fallthrough in GCC but
# not in clang, which gets it here (cmake/NifskopeCompile.cmake does the same). Intentional fall-through is marked with
# Q_FALLTHROUGH();
clang: QMAKE_CXXFLAGS += -Wimplicit-fallthrough
