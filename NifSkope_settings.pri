###############################
## BUILD SETTINGS
###############################
# Included by NifSkope.pro and tests/tests.pro, so that both compile the shared sources the same way. The CMake build
# has the same two switches under the same names (cmake/NifskopeQt.cmake). Set them on the qmake command line
#
#	qmake NIFSKOPE_CXX_STANDARD=17 NIFSKOPE_QT_DEPRECATED_BEFORE=0x051500 /path/to/NifSkope.pro
#
# in a fresh build directory: an existing one keeps the objects it compiled with the old values.

# Require Qt 5.15 or higher
contains(QT_VERSION, ^5\\.([0-9]|1[0-4])\\..*) {
	message("Cannot build NifSkope with Qt version $${QT_VERSION}")
	error("Minimum required version is Qt 5.15")
}

# C++ standard: 14, 17 or 20
isEmpty(NIFSKOPE_CXX_STANDARD): NIFSKOPE_CXX_STANDARD = 14

# Qt 5's qmake spells C++17 "c++1z" and C++20 "c++2a" (there is no "c++20"), with GNU extensions on
equals(NIFSKOPE_CXX_STANDARD, 14): CONFIG += c++14
else: equals(NIFSKOPE_CXX_STANDARD, 17): CONFIG += c++1z
else: equals(NIFSKOPE_CXX_STANDARD, 20): CONFIG += c++2a
else: error("NIFSKOPE_CXX_STANDARD is $${NIFSKOPE_CXX_STANDARD}: use 14, 17 or 20")

# QT_DISABLE_DEPRECATED_BEFORE: Qt API deprecated before this version is not declared. A hexadecimal Qt version,
# 0x050300 is Qt 5.3 and 0x051500 is Qt 5.15
isEmpty(NIFSKOPE_QT_DEPRECATED_BEFORE): NIFSKOPE_QT_DEPRECATED_BEFORE = 0x050300
!contains(NIFSKOPE_QT_DEPRECATED_BEFORE, ^0[xX][0-9a-fA-F]+$) {
	error("NIFSKOPE_QT_DEPRECATED_BEFORE is $${NIFSKOPE_QT_DEPRECATED_BEFORE}: use a hexadecimal Qt version such as 0x051500")
}
