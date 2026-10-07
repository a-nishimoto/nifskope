# Changes in this fork

This is a personal fork of [NifSkope](https://github.com/niftools/nifskope) by the NifTools project. It is kept for my
own experiments and is not meant for wider use (see the [README](README.md)). This file lists what was changed here
compared with the original. It is not the original's [CHANGELOG.md](CHANGELOG.md), which is not maintained for
prerelease versions.

The fork starts from the original's `develop` branch at commit `3a85ac5` (February 2018, "Merge pull request #126
from jonwd7/develop"). The original's history up to that commit (2,218 commits) is kept in this repository.
Everything below was added on top in October 2026, in pull requests
[#1](https://github.com/a-nishimoto/nifskope/pull/1) and [#2](https://github.com/a-nishimoto/nifskope/pull/2) and
later fixes noted where they appear, with the help of Claude Code (Anthropic); the commits carry a `Co-Authored-By`
trailer.

## At a glance

| | Before | Now |
| --- | --- | --- |
| Build system | qmake | CMake, with qmake kept working |
| Qt and C++ | Qt 5.7 or later, C++14 | Qt 5.15 (Qt 6 is refused for now), C++20 (C++17 can be selected) |
| zlib | 1.2.8 | 1.3.2 |
| Tests | none | 11 Qt Test classes, 2,854 passing results (73 of them expected failures that record known defects) |
| CI | Travis (Linux and macOS, Ubuntu trusty) and AppVeyor (Visual Studio 2015) configurations | GitHub Actions: Linux, macOS and Windows |
| Deprecated Qt API in use | more than 400 places (388 of them calls of `QModelIndex::child()`) | none: what Qt 5.15 deprecates is no longer available to the build |

## Build system and platform support

* **CMake build** next to the qmake one: a top-level `CMakeLists.txt`, `cmake/`, `CMakePresets.json` and a vcpkg
  manifest. The NIF and KFM model code is a static library (`nifskope_core`) that the tests link; each vendored
  library (zlib, lz4, qhull, gli, NvTriStrip, the BSA reader) is its own target. zlib and lz4 can be taken from the
  system or from vcpkg (tried in CI); options for a system qhull and gli exist but are experimental and untried.
  Builds must be out of the source tree. `cmake --install` produces an install tree and, on macOS and Windows,
  bundles Qt (`macdeployqt`, `windeployqt`); a macOS bundle was checked to keep running after being moved. See
  [BUILDING.md](BUILDING.md).
* **qmake still works** and is the reference the CMake build was compared with: the same 99 sources, 35 moc outputs and
  8 `.ui` headers are compiled and the embedded resources are identical. Its include paths no longer assume that the
  checkout directory is called `nifskope`.
* **Minimums are now Qt 5.15 and a C++20 compiler** (GCC 10, Clang 10, Apple clang 12, MSVC 2019 16.11; these are
  enforced by configure checks, and only GCC 13, Apple clang 21, MSVC 2022 and Qt 5.15.2 and 5.15.19 have actually been
  used). C++17 can be selected with `NIFSKOPE_CXX_STANDARD=17`; C++14 is no longer possible because the sources use `std::as_const`.
  Qt 6 is refused with a message until the sources are ported.
* **zlib 1.2.8 to 1.3.2.** The old release does not compile with current macOS SDKs, and the build now compiles only
  the 11 core zlib sources instead of every `.c` file in the directory.
* **Differences to the qmake build:** a CMake Release build uses `-O3` (qmake's Clang build passed no `-O` for C++); CMake's
  default `-DNDEBUG` is removed so `assert()` stays active as it was; the Linux executable is called `nifskope`; the
  macOS bundle has an icon and carries `nif.xml`, the shaders and the other data files in `Contents/Resources`, with
  symbolic links in `Contents/MacOS`, which is where the program looks (a qmake bundle keeps them beside the `.app`,
  where it cannot find them).

## Tests

A Qt Test suite in `tests/` (see [tests/README.md](tests/README.md)), run with `ctest` or `make check`:

* loading of `nif.xml` and `kfm.xml`, and the error text and line numbers of the schema parsers;
* `NifModel` and `KfmModel`, including that a failed `save()` does not corrupt an existing file;
* a write, size and read round trip of every `NifValue` type, with hand-derived byte sequences for the fixed-width
  types and four whole files (versions 3.1, 4.0.0.2 (Morrowind), 10.1.0.0 and 20.2.0.7 (Skyrim LE));
* scenes built programmatically in 11 version and game profiles, saved, reloaded and compared;
* `NifExpr` operators and boundaries, the math types, and the zlib and BSA decompression.

No game assets or binary fixtures are used. Expected bytes are derived from `nif.xml` independently of the code under
test. Before the XML parser tests were added, the suite was mutation-tested: according to the commit message, several
thousand single-site mutants of the stream, expression, type and model code were applied one at a time, and the ones
the tests missed were either covered or shown to be equivalent (the scripts are not kept in the repository). The tests
record known defects as expected failures (see below).

## Continuous integration

The Travis and AppVeyor configurations are replaced by one GitHub Actions workflow. It builds and tests on Linux
(GCC 13.3, the runner's default; CMake, CMake with vcpkg zlib and lz4, and qmake), macOS (x86_64, run under Rosetta on
an arm64 runner) and Windows (MSVC 2022), all with Qt 5.15.2. Each of the three jobs of the main CMake matrix
configures, builds, runs the tests, installs, checks the installed layout and runs the installed program with
`--version`. The vcpkg job (allowed to fail) builds and tests only; the qmake job builds and tests with qmake, builds
the application and runs it with `--version`.

Problems the first runs found, now fixed: the Linux link needed the legacy `libGL` instead of GLVND's `libOpenGL`; the
tests crashed on Windows' offscreen platform when the code under test opened a `QMessageBox`; and on macOS Qt 5.15.2's
`macdeployqt` gave up waiting for the x86_64 tools to start cold under Rosetta (the timings fit), signing a bundle without
Qt in it. The install now fails loudly when no
Qt was bundled, and a warm-up step absorbs the slow first start.

## Qt 5.15 and C++20 cleanup

* Every Qt 5.15 deprecation is replaced and `QT_DISABLE_DEPRECATED_BEFORE=0x051500` makes a new one a compile error.
  The CMake option `NIFSKOPE_WERROR_DEPRECATED` (on in the CI presets) also makes a deprecated declaration in
  NifSkope's own code an error.
* `QModelIndex::child()` and `QPersistentModelIndex::child()` (388 call sites in 35 files, 7 of them in comments) became
  `childIndex()`. At -O2, 82 of the 86 first-party translation units compile to byte-identical object code compared
  with the previous commit; the other four contain the `QPersistentModelIndex` sites.
* `foreach` became range-based `for`; the two `QRegExp` wildcard filters became `setFilterWildcard()`;
  `QMap::insertMulti()` users became `QMultiMap` and `QMultiHash`; the deprecated enumerators, `QFontMetrics::width`,
  `QLayout::setMargin` and the rest were replaced by their documented successors.
* The `QXmlSimpleReader` schema parsers for `nif.xml` and `kfm.xml` use `QXmlStreamReader`. What they register from the
  real files is byte-identical to what the old parsers registered (checked with a dump comparison that is not kept in
  the repository); parsing takes about the same time.
* The compiler warnings of NifSkope's own code were cleared: the macOS CI build printed 476 warnings (414 of them Qt
  deprecation warnings, 62 from `-Wall -Wextra`) and prints none now. An MSVC build still shows 8 narrowing warnings
  (`C4267`, `C4305`), 6 in NifSkope's sources and 2 in the tests.
* Clang now warns about unmarked `switch` fall-throughs, as GCC already did, and the ten intentional ones carry
  `Q_FALLTHROUGH()`.

## Bugs fixed on purpose

* `NifModel::setData()` answered `false` for the Version Condition column (a missing `break`, there since 2008) and
  skipped `dataChanged`. It is dormant, because nothing edits that column, and a test now covers it.
* In the flip-texture dialog, **Remove** also ran **Move Up** (a missing `break`): it reordered the list, and removing
  the last row blanked the new last entry.
* `Mesh::transform()` read the bytes of a `ByteColor4` through a pointer into a temporary that was gone at the end of
  the statement (undefined behaviour); the colour is a named local now.
* The status bar's size grip asked its stylesheet for `:/img/sizeGrip`, but the icon is registered as `:/wnd/sizeGrip`
  (the same in the original): Qt printed `Could not create pixmap from :/img/sizeGrip` 17 times at start-up and the grip
  image was never drawn. The path is right now (`src/ui/nifskope.ui`) and the warning is gone in a run of the macOS
  build (the grip itself was not looked at). A scan of the other `:/` references in the sources found no other that
  is not registered.
* The four Fallout 4 shaders (`fo4_default` and `fo4_effectshader`, vertex and fragment) declared `#version 130`, which
  the OpenGL 2.1 context that macOS provides (GLSL 1.20) rejects, so on a Mac the two Fallout 4 programs were never
  created. They are GLSL 1.20 now, like the Skyrim ones: `in` and `out` became `varying`, `textureLod` became
  `textureCubeLod` (the shader already required `GL_ARB_shader_texture_lod`) and the `F` suffix of one constant went.
  On an Apple M5 (GL 2.1 Metal, GLSL 1.20) all 11 shaders compile and all 6 programs link, where before the four
  Fallout 4 shaders failed; that was checked with a throwaway program that is not kept. Drawing a Fallout 4 mesh was
  not looked at, and Linux and Windows drivers were not tried.

## Behaviour kept on purpose

* `QWheelEvent::delta()` was not replaced by `angleDelta().y()`, which would have changed horizontal scrolling in the
  3D view and the UV editor; the new `wheelDelta()` returns what `delta()` returned.

## Behaviour differences you may notice

* The schema parsers are stricter on input that is not well-formed XML (undeclared entities, an attribute given twice,
  invalid UTF-8, control characters, an unknown encoding) and normalise CR and CRLF to LF in text. The shipped
  `nif.xml` and `kfm.xml` parse exactly as before. One unbalanced parenthesis in a `cond` attribute still ends the
  program, as before; before, it aborted inside Qt, now it propagates to the caller.
* The branch-copy clipboard payload written by the spells now lists map entries in ascending key order instead of
  descending; the reader rebuilds the same map.
* `QPersistentModelIndex::child()` on an index whose row has been removed used to dereference a null model; it now
  returns an invalid index.
* The hue calculation of the colour wheel uses a different but equivalent formula (the result differs by less than
  1e-10 degrees).

## Known defects found, not fixed

The tests record these as expected failures, each with the place in the code (an unexpected pass means someone fixed
it): the table is in [tests/README.md](tests/README.md). The main ones are that text is written as Latin-1 but read
as UTF-8, so non-ASCII names do not survive a save and load; `half_from_float()` turns floats from 65568 up to just
below 131072 into NaN; the first character of the right operand of an expression is dropped when no space follows the operator; and
`Matrix::toEuler()` has the wrong sign at -90 degrees of pitch. UBSan also reports undefined behaviour (as of the merge of PR #2) in
`lib/half.cpp` (lines 109 and 273) and `src/data/niftypes.h` (line 1751).

## Not done, and not verified

* The program has only been run headless. Rendering and the user interface were never looked at on a real display in
  this work. The OpenGL rendering approach is unchanged, legacy OpenGL 2.1 (only mechanical edits touched `src/gl`).
  The Fallout 4 shaders did not even compile on macOS until they were ported to GLSL 1.20 (see above), and whether they
  draw correctly there is not known.
* Windows is verified only by the CI runs; no Windows machine was used. There is no native x86_64 macOS or Linux arm64
  testing, and no Qt 6 port (`QGLWidget` and the rest are still used).
* Linux distributions older than Qt 5.15 or GCC 10 are not supported any more.
