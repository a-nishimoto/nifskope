# NifSkope tests

Qt Test suite for the NIF/KFM load and save data path: XML description parsing, the item model, the binary
streams, the value types, and the zlib/BSA decompression path. No OpenGL context and no main window are needed.

It builds with qmake as a standalone project (`tests.pro`; `NifSkope.pro` is not involved and does not build
these files) or with CMake, where `tests/` is a subdirectory of the top-level project (see
[Build with CMake](#build-with-cmake)).

## Build and run (qmake)

Always build out of tree (the build directory must not be inside the checkout):

```sh
mkdir nifskope-tests && cd nifskope-tests
qmake /path/to/nifskope/tests/tests.pro      # add CONFIG+=release for an optimised build
make -j8
make check                                   # runs the tests; exit status is non-zero on any failure
```

* `qmake` is not on `PATH` on every machine, e.g. Homebrew's Qt 5 is at `/opt/homebrew/opt/qt@5/bin/qmake`.
* On Windows use `nmake` (or `jom`) instead of `make`. There qmake generates a debug and a release Makefile
  (`debug_and_release`), each with its own `GeneratedFiles\debug` / `GeneratedFiles\release` directory and its own
  executable in `debug\` / `release\` (`release\nifskope_tests.exe`). Run `qmake CONFIG+=release ...`, then
  `nmake release` to build only that configuration and `nmake check` to build and run it (`CONFIG+=debug`,
  `nmake debug` for a debug build). `nmake release-check` and `nmake debug-check` pick a configuration explicitly.
* The Qt 5.15 SDK version warning on a very new macOS SDK can be silenced with `CONFIG+=sdk_no_version_check`.
* The `build/docsys` submodule must be present, with its own submodules (`git submodule update --init --recursive`:
  `nif.xml` and `kfm.xml` are in `build/docsys/nifxml` and `build/docsys/kfmxml`, so `--recursive` is required),
  because the tests parse them. Missing files fail the XML tests with that hint instead of skipping them.

A full build takes a few seconds and the whole suite runs in about a second.

### CI

GitHub Actions (`.github/workflows/ci.yml`) runs the suite with Qt 5.15.2 for every pull request and every push to
`develop` and `master`:

* The `cmake` job builds with CMake on Linux (GCC), Windows (MSVC x64) and macOS: it configures with a `ci-*` preset
  (which also sets `NIFSKOPE_WERROR_DEPRECATED=ON`, so a deprecated declaration in the tests or in NifSkope's own code
  fails the build), builds, runs `ctest` and installs. The JUnit XML of the CTest run is attached as `test-results-<id>`
  (`test-results-cmake-linux`, `test-results-cmake-windows-x64`, `test-results-cmake-macos`); the install trees are
  attached as `nifskope-<id>`.
* The `qmake-legacy` job builds this suite with `tests.pro` and runs it, on Linux only. The text output is attached as
  `test-log-qmake-linux`.

The test part of the `qmake-legacy` job is these commands, with `QT_QPA_PLATFORM=offscreen` in the environment:

```sh
mkdir nifskope-tests && cd nifskope-tests
qmake CONFIG+=release /path/to/nifskope/tests/tests.pro
make -j$(nproc)
./nifskope_tests -o -,txt
```

On Linux the Qt GUI library still needs its runtime libraries (libGL/mesa, fontconfig, freetype) even though the
tests use the `offscreen` platform plugin.

### Running part of the suite

All test classes live in one executable, `nifskope_tests`. Run it directly to pass Qt Test options:

```sh
./nifskope_tests                              # everything
./nifskope_tests tst_NifRoundTrip             # one class (the class name must be the first argument)
./nifskope_tests tst_NifRoundTrip saveLoadSave   # one function of one class
./nifskope_tests tst_Zlib -o zlib.xml,junitxml   # per-class JUnit XML (one class per run, see below)
./nifskope_tests tst_NifTypes -functions         # list a class' test functions
```

Options after the optional class name are standard Qt Test options. The process exit code is the number of test
classes with failures.

`-o file,junitxml` is applied to each class in turn and overwrites the same file, so ask for XML one class per
invocation. Plain text on the console works for the whole suite.

### qmake switches

| Switch | Effect |
| --- | --- |
| `CONFIG+=no_zlib` | Leave out the zlib/BSA test (`tst_zlib.cpp`) and the code it needs (`lib/zlib`, `lib/fsengine/bsa.cpp`, lz4, xxhash) |
| `NIFSKOPE_ROOT=<dir>` | Take the sources and the XML files from another checkout (default: the parent of `tests/`) |
| `NIFSKOPE_CXX_STANDARD=<14\|17\|20>`, `NIFSKOPE_QT_DEPRECATED_BEFORE=<0x...>` | The C++ standard (default 20) and `QT_DISABLE_DEPRECATED_BEFORE` (default `0x051500`, Qt 5.15): the same switches, the same defaults and the same file (`NifSkope_settings.pri`) as `NifSkope.pro`, so the tests compile the shared sources the way the application does |

## Build with CMake

The top-level `CMakeLists.txt` builds the same suite; `tests/CMakeLists.txt` links the `nifskope_core` library of
`src/` instead of listing its sources. Build out of tree (a build directory inside the checkout is refused):

```sh
cmake --preset dev -DCMAKE_PREFIX_PATH=/path/to/Qt/5.15.2/gcc_64    # or set QT_ROOT_DIR instead of the option
cmake --build --preset dev
ctest --preset dev
```

The presets of `CMakePresets.json` put the build directory beside the checkout (`../build-nifskope/<preset>`). The
same without presets:

```sh
cmake -S /path/to/nifskope -B nifskope-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build nifskope-build
ctest --test-dir nifskope-build --output-on-failure
```

* There is one CTest test per test class (`ctest -N` lists them). Each runs `nifskope_tests <class> -o -,txt` on the
  `offscreen` platform, so no display is needed. `ctest -R tst_Zlib` runs one class; `ctest --output-junit junit.xml`
  writes JUnit XML for all of them (each class is a separate invocation, so the "one class per run" limitation of
  `-o file,junitxml` below does not apply).
* The executable is `bin/nifskope_tests` in the build directory (`bin/<Config>` with Visual Studio and Ninja
  Multi-Config) and takes the Qt Test options described below.
* `cmake --build <dir> --target check` runs the test classes only, like qmake's `make check`. With a full build,
  `ctest` also runs the two application smoke tests of `src/` (`app_version`, `app_layout`).
* Tests only, without OpenGL, without `lib/qhull` and `lib/gli`: `-DNIFSKOPE_BUILD_APP=OFF`.
* The sources under test come from the `nifskope_core` library (the same 13 files as `tests.pro`, `lib/half.cpp`
  included). A model file that starts to depend on something new fails to link: move the missing source into
  `nifskope_core` in `src/CMakeLists.txt` if it needs no OpenGL.

| Option | Effect |
| --- | --- |
| `-DNIFSKOPE_BUILD_BSA_TEST=OFF` | Leave out the zlib/BSA test (`tst_zlib.cpp`) and the code it needs, like `CONFIG+=no_zlib`. Then `lib/zlib` is not needed either |
| `-DNIFSKOPE_USE_SYSTEM_ZLIB=ON` | Test against a system or vcpkg zlib (at least 1.3.2) instead of `lib/zlib`; `tst_Zlib` compares the header with the library it links |
| `-DNIFSKOPE_BUILD_TESTS=OFF` | No tests (the default when NifSkope is added as a subproject) |
| `-DNIFSKOPE_CXX_STANDARD=17`, `-DNIFSKOPE_QT_DEPRECATED_BEFORE=0x050300` | The C++ standard (default 20) and `QT_DISABLE_DEPRECATED_BEFORE` (default `0x051500`, Qt 5.15) of the tests and of the code under test; the application is built the same way |
| `-DNIFSKOPE_WERROR_DEPRECATED=ON` | A use of a deprecated declaration is an error, in the tests and in NifSkope's own code (the `ci-*` presets turn it on) |

## What is covered

| Class | File | What it pins down |
| --- | --- | --- |
| `tst_XmlLoad` | `tst_xmlload.cpp` | `nif.xml` / `kfm.xml` parse; block and compound tables; every `<version>` in the XML is understood; enums; missing and malformed files are rejected without leaving half-loaded tables |
| `tst_XmlSchema` | `tst_xmlload.cpp` | Schema documents written for the test, parsed by the real loaders (`NifModel::parseXmlDescription()`, `KfmModel::parseXmlDescription()`): the text and the line of every message of the NIF and KFM handlers (the whole error interface: `loadXML()` shows it as the details of the error box), `Syntax error` and its line for documents that are not well-formed (including line ends), and that a failed parse leaves no blocks, compounds or versions behind and frees its lock. What a good document registers, seen through the public API and a `NifModel` / `KfmModel` built from it: every attribute of an `<add>`, default values, the nine mixin types, forward references, blocks, ancestors, abstract and fixed ones, types of `nif.xml` that a compound only describes, enums and bit flags, versions, descriptions and the markup inside them (entities, comments, CDATA, processing instructions), encodings, namespaces, a second parse replacing the first. What the XML rules say and the old SAX reader did not check: duplicate attributes, undeclared entities, bad character references and bytes, a CR LF or CR line end reads as LF (also in nif.xml itself) |
| `tst_NifRoundTrip` | `tst_roundtrip.cpp` | Build a small scene (nodes, shape, geometry arrays, string extra data, links) for 11 version/game profiles (3.1, Morrowind, Civ IV, 10.0.1.0, 10.2.0.0, Oblivion, 20.1.0.3, Fallout 3, Skyrim LE/SE, Fallout 4), save, load into a fresh model, compare every field, and save again for a byte-identical result. Also checks header strings, version fields, links, header counters, the per-block sizes stored from 20.2.0.5, and `saveToFile`/`loadFromFile`. The same for a richer scene (`TestEnv::buildRichScene()`: a rotation that is not the identity, UV sets, normals, vertex colours, a Ptr link, material, texture and shader blocks, a skin, extra data with long strings) and for the `BSTriShape` scene of Skyrim SE / Fallout 4 (`buildBSTriShapeScene()`: half floats, half and byte vectors, vertex descriptions). Whole files (3.1, Morrowind 4.0.0.2, 10.1.0.0, Skyrim LE 20.2.0.7) are compared against bytes derived from `nif.xml` with a script, not with NifSkope (`golden_*`), and the version prefix of every profile is pinned (`golden_headerPrefix`) |
| `tst_NifStream` | `tst_roundtrip.cpp` | The three streams (`NifOStream`, `NifSStream`, `NifIStream`) on single values: every `NifValue` type, in header versions on both sides of each layout change (3.1, 3.3.0.13, 4.0.0.2, 4.1.0.12, 10.0.1.0, 20.0.0.5, 20.1.0.3, 20.2.0.7): the bytes written (computed with python `struct`, the expression is in the comment of each row), the size `NifSStream` reports, the value read back, truncated input. String limits and codecs, ShortString quirks, non-ASCII text, all 65536 half floats, quantised byte vectors and colours (every byte, plus the rounding of the writer), one bit at a time through every component of the half vectors, `FileVersion` (NeoSteam, the endian peek) and `HeaderString` (which starts the stream over), `KfmModel` and `StubModel` streams (no NIF version rules for a model that is not a `NifModel`, whatever its version number says). Also what the streams do when the device misbehaves: a write that fails or is cut short at every one of the steps of every value (`FaultyWriteDevice`), a read that comes back short while more bytes follow (`ChunkedReadDevice`), one byte after every value that no reader may take, the NUL, the blanks and the refused lengths of the string readers, lines that do not end, and values the `NifValue` functions cannot build (`RawValue`: a string that holds an index, a blob or a FilePath without data) |
| `tst_SaveSafety` | `tst_savesafety.cpp` | A failed `save()` must neither corrupt an existing file nor leave a new, incomplete one behind (regression test for commit `2dad25d`); successful saves write every byte; an empty save is a failure; save to a failing device; `load()` runs in the Loading state |
| `tst_NifModel` | `tst_nifmodel.cpp` | New-model state (header string and user versions per version, the start-up defaults), rejection of garbage, truncated and unsupported files (no crash, no message box), the container layout (block type table and index, block separator, type name and number in front of old blocks, `fileOffset()` against the saved bytes, block sizes with "Ignore Block Size" off, 3.3.0.13), what `load()` says about bad type names, sizes and separators, `earlyRejection()`, `loadHeaderOnly()`, `loadIndex()`; block insert, remove, move and reorder keeping links consistent, root links and the footer, Ptr and nested Ref links, link loops; the string table (de-duplication, replace, `moveAllNiBlocks`), arrays (blobs, size mismatch warnings, refused sizes) and `Transform` read from and written to blocks (also from the "Transform" field of a compound). What a failed load leaves in the model (file name and folder, state), the progress reports, and what the header, the footer, the links and `linksChanged` say after every edit of a block or a link (`insertNiBlock`, `removeNiBlock`, `moveNiBlock`, `reorderBlocks`, `mapLinks`, `setLink`, `setLinkArray`, `loadIndex`, `loadAndMapLinks`, `holdUpdates`) |
| `tst_KfmModel` | `tst_kfmmodel.cpp` | KFM model: new model, save/load/save round trip, file round trip, unsupported version |
| `tst_NifTypes` | `tst_niftypes.cpp` | `version2number`/`version2string`, vectors (also `fromString` with incomplete input, equality, order, angle), matrices (`fromQuat`, `toQuat` for every pivot, `toEuler` incl. gimbal lock, `inverted`, 4x4 compose/decompose with a different scale per axis, text forms), quaternions (axis and angle, slerp against the published approximation, also at the lengths where its normalisation takes its second and third step), `Transform` (product, matrix, stream form, a point, its text), every operator of the vector, quaternion, colour, matrix and triangle types on values that differ in every component, `FixedMatrix`, the bit layout of `BSVertexDesc`, and the other value types in `niftypes.h`. Expected numbers come from numpy (python3) |
| `tst_NifValue`, `tst_NifExpr` | `tst_nifvalue.cpp` | `NifValue` (every type name of `nif.xml`, get/set, copies, equality of every type, text conversion both ways for every type, colours, enums and aliases incl. flags, the description of an enum, `ask<T>()` of every type, data freed when the type changes) and `NifExpr` (the conditions used in `nif.xml`: every operator below, at and above its boundary, literals, grouping, names, text operands, the printed form, the type of an answer, and the real conditions of `nif.xml`) |
| `tst_Zlib` | `tst_zlib.cpp` | The bundled `lib/zlib` is the one linked, a version floor, checksums, round trips through `compress2`/`uncompress` and the BSA `gUncompress()` path, gzip and zlib input, corrupt input |

### Known defects (`XFAIL`)

Each of these rows records a defect of the application, not of the test: the expected value is what is right and
`QEXPECT_FAIL` says that it is not what happens today. **An unexpected pass (`XPASS`) means the defect has been fixed:
remove that `QEXPECT_FAIL` line.** The message of each one names the place.

| Test function | Defect | Where |
| --- | --- | --- |
| `tst_NifTypes::quat_normalize` | `Quat::normalize()` divides by the squared magnitude | `niftypes.h` |
| `tst_NifTypes::matrix_toEuler_gimbalLock` | `Matrix::toEuler()` at -90 degrees of pitch returns the first angle with the wrong sign | `niftypes.cpp` |
| `tst_NifValue::equality_stringOffsetUses32Bits` | `NifValue::operator==` compares a `tStringOffset` (32 bits) as 16 bits | `nifvalue.cpp` |
| `tst_NifValue::setFromString_unsigned16` | `setFromString()` reads Word, Flags and BlockTypeIndex with `toShort()`: 32768 and up is refused | `nifvalue.cpp` |
| `tst_NifExpr::noSpaceAfterOperator` | the first character of the right operand is dropped when no space follows an operator; `nif.xml` has one such condition, `(Flags & 2)!=0` | `nifexpr.cpp` |
| `tst_NifExpr::toString_decimalAboveIntMax` | a decimal number above 2147483647 is kept as an int and printed as a negative number | `nifexpr.cpp` |
| `tst_NifModel::newModel_copyrightLines` | `clear()` fills the Copyright array of a 3.1 header before it has rows, so a new header has no lines | `nifmodel.cpp` |
| `tst_NifRoundTrip::havokBlock` | `save()` writes the block separator of 10.0.1.0 - 10.1.x before `bhk` blocks, `load()` does not read one | `nifmodel.cpp` |
| `tst_NifStream::hfloat_overflow`, `hfloat_denormalRounding`, `hfloat_denormalRounding_all`, `hfloat_signallingNaN` | `half_from_float()`: floats from 65568 up to just below 131072 become NaN, denormals are truncated, signalling NaNs become infinity | `lib/half.cpp` |
| `tst_NifStream::nonAscii`, `nonAscii_filePath`, `nonAscii_bytesSurviveResave`, `nonAscii_stringTable` | text is written as Latin-1 and read as UTF-8: a name with a non-ASCII character does not survive a save and load, and a UTF-8 file is rewritten | `nifstream.cpp` |
| `tst_NifStream::shortString_sizeMatchesWrite` | `NifSStream::size()` of a ShortString uses `toLatin1()` and no `\n` replacement, the writer `toLocal8Bit()` with it | `nifstream.cpp` |
| `tst_NifStream::stringLimits` (`StringPalette 5 announced, 2 present`) | the reader does not check that a StringPalette has all the bytes it announces | `nifstream.cpp` |
| `tst_NifStream::sizedString_cutShortLength` | a SizedString whose length is cut short is read as an empty text and the read says it worked (the status of its `QDataStream` is not looked at) | `nifstream.cpp` |
| `tst_NifTypes::fixedMatrix_rowAccess` | `FixedMatrix::operator()( int )` steps by the number of rows, not by the length of a row: `m( 1 )` of a 2x3 matrix is not its second row | `niftypes.h` |

`tst_NifStream::nonAscii_writerBytes` records the encoding the writers use today (Latin-1) and has no `XFAIL`: when the
encoding is fixed it is the one test to update together with the removed `QEXPECT_FAIL` lines above.

When bumping `lib/zlib`, raise the floor in `tst_Zlib::minimumVersion` in the same commit.

## How it works

* **Sources under test.** `tests.pro` compiles 13 application sources directly (`src/data`, `src/io`,
  `src/model`, `src/xml`, `src/message.cpp`, `src/spellbook.cpp`, `src/ui/checkablemessagebox.*`, `lib/half.cpp`);
  CMake links them as the `nifskope_core` library. If a model file starts depending on something new the link
  fails with an undefined symbol; add that source to `tests.pro` and to `nifskope_core`. The spells and GL code
  are not linked, so `SpellBook` sees an empty spell registry.
* **The one seam in `src/`.** `NifModel::parseXmlDescription()` and `KfmModel::parseXmlDescription()` were
  protected. They are public now so the tests can load the XML from the source tree instead of from
  `QCoreApplication::applicationDirPath()`, which is where `loadXML()` looks. Behaviour is unchanged.
* **Where the XML comes from.** `NIFSKOPE_SOURCE_DIR` is defined at compile time (see `testenv.cpp`), so the tests do
  not depend on the working directory or on where the build directory is.
* **Headless.** `main.cpp` selects the `offscreen` Qt platform unless `QT_QPA_PLATFORM` is already set and builds a
  `QApplication`. Many error paths in the models open a `QMessageBox` through `Message::critical()/append()` even
  when the model's message mode is silent. No box is ever really shown: `main.cpp` calls
  `TestEnv::installMessageBoxGuard()`, an application event filter that records a `QMessageBox` when it is about to
  be shown and consumes the `QShowEvent`, so `QMessageBox::showEvent()` never runs (on Windows' offscreen platform it
  crashes the process: Qt 5.15 calls through a native interface that plugin does not have). `cleanup()` in each
  class fails the test if a box was recorded and not taken, and `main.cpp` checks again after each class.
  `TestEnv::takeMessageBoxes()` returns the texts of the recorded boxes and hides them. A few tests expect one and
  count it. The offscreen plugin may print `This plugin does not support raise()` or `propagateSizeHints()` for
  those; it is harmless.
* **Settings are sandboxed.** `main.cpp` points `QSettings` at a temporary directory. `NifModel` takes its version
  and user versions from the "Startup Defaults" settings, which is how `TestEnv::makeModel()` picks a game profile.
* **Global state.** The parsed XML tables and `NifValue`'s type and enum maps are process-wide statics. Every class
  that needs them calls `TestEnv::reloadXml()` in `initTestCase()`, so the classes can run in any order, or alone.

## Adding a test

To add a test function, put a slot in the class of the part it tests (a file can hold several classes, `tst_nifvalue.cpp`
and `tst_roundtrip.cpp` do). To add a class:

1. Create `tst_<name>.cpp` with a `QObject` subclass (`Q_OBJECT`, tests in `private slots`).
2. After the class, add `REGISTER_TEST( tst_Name )` and finish the file with `#include "tst_<name>.moc"`.
3. Add the file to `SOURCES` in `tests.pro` and to `NIFSKOPE_TEST_SOURCES` in `tests/CMakeLists.txt` (CMake finds
   the class by its `REGISTER_TEST` line and registers it with CTest on the next configure).

Use `TestEnv::makeModel()`, `buildScene()`, `buildRichScene()` and `diffModels()` from `testenv.h` for model fixtures
(`StubModel`, also there, is a `BaseModel` that is neither a NIF nor a KFM, with a version number of your choosing),
`saveBytes()` / `loadBytes()` for a file in memory, `diffBytes()` to say where two files differ, and `countValue()`,
`floatValue()`, `linkValue()` and `valueOf<T>()` for `NifValue`s that stop the program when the type refuses the value.
Take the message boxes a test provokes with `takeMessageBoxes()` (never delete them: `Message::append()` caches them).

Expected numbers are computed independently of the code under test (python3 `struct` or numpy, the expression is in a
comment next to the number), never read back from the code. A defect found while writing a test is recorded with
`QEXPECT_FAIL` and the line `; fix then remove`, not left out.
