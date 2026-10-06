# The Qt Test suite: one executable, one CTest test per test class. It links nifskope_core, and nifskope_fsengine
# and zlib for the BSA test, instead of compiling their sources again (tests/tests.pro lists them one by one).
find_package(${NIFSKOPE_QT} REQUIRED COMPONENTS Test)

set(NIFSKOPE_TEST_SOURCES
	tst_xmlload.cpp
	tst_roundtrip.cpp
	tst_savesafety.cpp
	tst_niftypes.cpp
	tst_nifvalue.cpp
	tst_nifmodel.cpp
	tst_kfmmodel.cpp)
if(NIFSKOPE_BUILD_BSA_TEST)
	list(APPEND NIFSKOPE_TEST_SOURCES tst_zlib.cpp)
endif()

# The test classes register themselves with static REGISTER_TEST objects, so the tst_*.cpp files are compiled
# into the executable itself, never into a library
add_executable(nifskope_tests
	main.cpp
	testenv.cpp
	testenv.h
	testregistry.h
	${NIFSKOPE_TEST_SOURCES})

set_target_properties(nifskope_tests PROPERTIES
	AUTOMOC ON
	RUNTIME_OUTPUT_DIRECTORY "${NIFSKOPE_RUNTIME_DIR}")

target_compile_definitions(nifskope_tests PRIVATE
	"NIFSKOPE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"") # where testenv.cpp finds build/docsys/*/nif.xml, kfm.xml

target_link_libraries(nifskope_tests PRIVATE
	nifskope_core
	nifskope_warnings
	${NIFSKOPE_QT_TEST_TARGETS})
if(NIFSKOPE_BUILD_BSA_TEST)
	# tst_zlib.cpp calls gUncompress() from lib/fsengine/bsa.cpp and includes "zlib/zlib.h", which has to be the header of
	# the zlib it links (nifskope_zlib's shim, ahead of lib/ whatever the order here: see lib/CMakeLists.txt)
	target_link_libraries(nifskope_tests PRIVATE nifskope_fsengine nifskope_zlib)
endif()

nifskope_register_tests(nifskope_tests ${NIFSKOPE_TEST_SOURCES})

# make check: runs the test classes of this directory, as qmake's "make check" ran the test binary (the application
# smoke tests of src/ are not part of it). -C is what multi-config generators need to find the executable
add_custom_target(check
	COMMAND "${CMAKE_CTEST_COMMAND}" -C "$<CONFIG>" --output-on-failure
	DEPENDS nifskope_tests
	USES_TERMINAL)
