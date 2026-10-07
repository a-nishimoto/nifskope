# Building NifSkope

NifSkope builds with CMake, out of the source tree: the build directory must not be inside the checkout. The qmake
project (`NifSkope.pro`) is kept as the reference build, see [qmake](#qmake) at the end.

## Prerequisites

* **Qt 5.15** (an older Qt is refused, and so is Qt 6 until the sources are ported, see [Qt](#qt)).
* **CMake 3.22 or later** and a build tool. The presets use Ninja, except `vs2022`, which uses Visual Studio 2022.
* A C++20 compiler: GCC 10 or later, Clang 10 or later, Apple clang 12 (Xcode 12) or later, or MSVC from Visual Studio
  2019 16.11 or later. CMake and qmake stop with a message on an older one; `NIFSKOPE_CXX_STANDARD=17` (see
  [Options](#options)) selects C++17 instead. C++14 is not supported: the sources use `std::as_const`.
* The submodules, which hold the XML description (`nif.xml`, `kfm.xml`) and the vendored libraries:
  `git submodule update --init --recursive`
* Linux: the OpenGL and GLU development packages, `libgl1-mesa-dev libglu1-mesa-dev` on Debian and Ubuntu. A missing
  GLU or libGL is reported at configure time with that package name.
* macOS: Xcode or its command line tools, and a Qt built for the architecture you build for. Homebrew's `qt@5` is arm64
  on Apple silicon. The Qt 5.15.2 installers are x86_64 only: build with `-DCMAKE_OSX_ARCHITECTURES=x86_64` (the
  `ci-macos` preset does) and run the result under Rosetta.

## Quick start

The presets in `CMakePresets.json` put the build directory next to the checkout (`../build-nifskope/<preset>`) and the
install directory in `../install-nifskope/<preset>`:

```sh
cmake --preset dev -DCMAKE_PREFIX_PATH=/path/to/Qt/5.15.2/gcc_64    # or: export QT_ROOT_DIR=/path/to/Qt/5.15.2/gcc_64
cmake --build --preset dev
ctest --preset dev
```

`dev` is a Debug build with the tests, `release` an optimised one. `asan` adds AddressSanitizer and UBSan (Linux, macOS),
`vs2022` is the Visual Studio 2022 solution (Windows), and the `ci-*` presets are what GitHub Actions uses; they also turn
on `NIFSKOPE_WERROR_DEPRECATED` (see [Options](#options)). Without presets:

```sh
cmake -S nifskope -B build-nifskope -G Ninja -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build-nifskope
ctest --test-dir build-nifskope --output-on-failure
```

The executable is `bin/NifSkope` (`bin/NifSkope.app` on macOS, `bin/nifskope` on Linux) in the build directory, with
the files it needs next to it (`nif.xml`, `kfm.xml`, `style.qss`, `shaders/`, `README.txt`, `CHANGELOG.txt`,
`LICENSE.txt`): it looks for them in its own directory and nowhere else on Windows and macOS. On macOS they are in
`NifSkope.app/Contents/Resources`, with symbolic links in `Contents/MacOS`. Every build refreshes them. The executable
in the build directory finds Qt where it was built against it (RPATH; on Windows through `PATH`, so put Qt's `bin`
directory on it); `cmake --install` makes a copy that carries Qt along (see [Install](#install)).

The default build type is Release for single-configuration generators (`-DCMAKE_BUILD_TYPE=...`); Visual Studio, Xcode
and Ninja Multi-Config take `--config`.

### Out-of-source builds only

A build directory inside the checkout is a configure error, which also catches `mkdir build && cd build && cmake ..`
(`build/` is a tracked directory). The refused configure leaves a `CMakeCache.txt` and `CMakeFiles/` behind; delete
them. Nothing the build does writes into the source tree: `README.md`, which qmake regenerates there, is not touched;
the CMake build writes its `README.txt` into `<build>/generated`.

## Options

| Option | Default | Effect |
| --- | --- | --- |
| `NIFSKOPE_BUILD_APP` | ON | Build the application. OFF builds the model library and the tests only: no OpenGL, no `lib/qhull`, no `lib/gli` |
| `NIFSKOPE_BUILD_TESTS` | ON at top level | Build `tests/` and register it with CTest |
| `NIFSKOPE_BUILD_BSA_TEST` | ON | Include `tst_zlib.cpp` and the zlib, lz4 and BSA code it needs (qmake: `CONFIG+=no_zlib`) |
| `NIFSKOPE_USE_SYSTEM_ZLIB` | OFF | zlib 1.3.2 or later from the system or vcpkg instead of `lib/zlib` |
| `NIFSKOPE_USE_SYSTEM_LZ4` | OFF | liblz4 1.7 or later instead of `lib/lz4frame.c` (CMake package, pkg-config, or plain search) |
| `NIFSKOPE_USE_SYSTEM_QHULL` | OFF | A static, non-reentrant qhull (`Qhull::qhullstatic`, `qhullstatic.pc`). Experimental |
| `NIFSKOPE_USE_SYSTEM_GLI` | OFF | gli with glm instead of `lib/gli`. Experimental: not tried with a real package, and the vendored copy is an old snapshot |
| `NIFSKOPE_USE_GL_QPAINTER` | OFF | Define `USE_GL_QPAINTER` (statistics overlay painted with QPainter) |
| `NIFSKOPE_DEPLOY_QT` | ON (Windows, macOS) | Run `windeployqt` / `macdeployqt` during `cmake --install`. Packagers turn it OFF |
| `NIFSKOPE_MACDEPLOYQT_VERBOSE` | 1 | macOS: `macdeployqt`'s `-verbose` level, 0 to 3. 1 is its default (errors only), 2 lists what it copies and signs, 3 logs every `otool` run |
| `NIFSKOPE_LINUX_FHS_LAYOUT` | OFF | Linux: install to `bin/` and `share/nifskope/` instead of `lib/nifskope/` |
| `NIFSKOPE_REVISION_OVERRIDE` | empty | Revision shown in About. Empty: the first 7 digits of `git rev-parse HEAD`, or none outside a git checkout |
| `NIFSKOPE_ALLOW_QT6` | OFF | Continue with Qt 6 for porting work |
| `NIFSKOPE_KEEP_NDEBUG` | OFF | Keep CMake's `-DNDEBUG` in the Release flags. Off, `assert()` stays live as it does in the qmake build |
| `NIFSKOPE_CXX_STANDARD` | 20 | The C++ standard NifSkope's own code is compiled as: 17 or 20 (17 with Qt 6). A cache variable, so `-D` and the `cacheVariables` of a preset set it (qmake: `NIFSKOPE_CXX_STANDARD=17`). 14 is not supported (the sources use `std::as_const`), and neither is 23 yet |
| `NIFSKOPE_QT_DEPRECATED_BEFORE` | `0x051500` | Value of `QT_DISABLE_DEPRECATED_BEFORE`: Qt API deprecated before this hexadecimal Qt version no longer compiles. The default is the minimum Qt, so the sources use nothing that Qt 5.15 deprecated; `0x050300` (Qt 5.3) declares the deprecated API again (qmake: `NIFSKOPE_QT_DEPRECATED_BEFORE=0x050300`) |
| `NIFSKOPE_WERROR_DEPRECATED` | OFF | Make the use of a deprecated declaration an error in NifSkope's own code: `-Werror=deprecated-declarations` (`/we4996` with MSVC), for NifSkope's own targets only, not for the vendored libraries that are targets of their own (zlib, lz4, NvTriStrip). The `ci-*` presets turn it on, so a deprecated call stops the CI build; a newer compiler or C++ library deprecates more than the ones CI uses, which is why the default is OFF. There is no qmake counterpart: qmake compiles the vendored sources with the same flags |

NifSkope's own code is compiled with `-Wall -Wextra` (clang also with `-Wimplicit-fallthrough`, which GCC's `-Wextra`
includes and clang's does not; MSVC: `/W3`, plus C4100 and C4189 at that level) and builds without warnings with Apple
clang 21, so a new warning is a mistake in the change that adds it. A `case` that falls through on purpose says so with
`Q_FALLTHROUGH();` (a "fall through" comment satisfies GCC but not clang). The vendored libraries are built without
warning options: theirs are not NifSkope's to fix.

With MSVC, NifSkope's own targets (and `NifSkope.pro` and `tests/tests.pro`) define
`_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING`: in Qt 5.15.2 to 5.15.16, `QVector`, `QList` and `QVarLengthArray` pass
`stdext::checked_array_iterator` to `std::equal` or `std::copy` on MSVC (`QT_MAKE_CHECKED_ARRAY_ITERATOR`), and the STL
of Visual Studio 2022 17.8 and later deprecates that class (STL4043, reported as C4996, an error with
`NIFSKOPE_WERROR_DEPRECATED`; checked against 17.14). It is Qt's use, not NifSkope's. Qt 5.15.17 made those macros
no-ops from 17.8 on (QTBUG-118993), so the define does nothing there.

The vendored libraries (zlib, lz4, qhull, gli/glm, NvTriStrip, half) are the default and are built from the sources in
`lib/`: the submodules' own CMake files are not used. The `NIFSKOPE_USE_SYSTEM_*` options use a library found through
`CMAKE_PREFIX_PATH` or the system. A vcpkg manifest (`vcpkg.json`) provides zlib and lz4: the `dev-vcpkg`,
`release-vcpkg` and `ci-linux-vcpkg` presets use it (set `VCPKG_ROOT`) and turn the two options on. qhull and gli stay
vendored there: vcpkg's qhull has only the reentrant API, which NifSkope does not use, and its gli is a different
snapshot.

NifSkope can also be added to another CMake project with `add_subdirectory()`. The tests are then off by default, and
the build type and the macOS deployment target are left to the parent project: NifSkope's own defaults for those two
apply only when it is the top-level project.

Tests only, which needs neither OpenGL nor the `lib/qhull` and `lib/gli` submodules:

```sh
cmake -S nifskope -B build-tests -DCMAKE_PREFIX_PATH=/path/to/Qt -DNIFSKOPE_BUILD_APP=OFF
```

### Qt

`find_package(QT NAMES Qt6 Qt5)` takes the first Qt that `CMAKE_PREFIX_PATH` leads to. When Qt 5 and Qt 6 are installed
in one prefix (Debian and Ubuntu: `/usr`), Qt 6 wins there: choose Qt 5 with `-DQT_DIR=<prefix>/lib/cmake/Qt5`. NifSkope
does not build with Qt 6 yet (`QGLWidget`, `QRegExp` and others), so configuring against it stops
with a message; `-DNIFSKOPE_ALLOW_QT6=ON` continues for people who work on the port.

## Install

```sh
cmake --install build-nifskope --prefix /path/to/install
```

| Platform | Result |
| --- | --- |
| Windows | `NifSkope.exe`, the Qt DLLs and plugins (`windeployqt`), `nif.xml`, `kfm.xml`, `style.qss`, `shaders\` and the text files in one folder; the app-local Visual C++ runtime DLLs |
| macOS | `NifSkope.app`, completed by `macdeployqt` (Qt frameworks and plugins in `Contents/Frameworks` and `Contents/PlugIns`, the executable finds them through `@executable_path/../Frameworks`) and signed ad hoc, with the data files in `Contents/Resources` |
| Linux | `lib/nifskope/` holds the executable and its data files (the program finds them beside itself), `bin/nifskope` links to it; `share/applications/nifskope.desktop`, the icon and the MIME types are installed as well |

`macdeployqt` and `windeployqt` are looked up next to `qmake`. A Qt without them still configures and builds; only
`cmake --install` fails, and `-DNIFSKOPE_DEPLOY_QT=OFF` installs without deploying Qt. On macOS the bundle then runs
only where its Qt is: the install keeps the executable's RPATH to the Qt it was built with. Qt's installers reference
their frameworks through `@rpath`, and that RPATH is also what `macdeployqt` resolves them with, so it must be there
when the deployment runs. `macdeployqt` prints a dozen `ERROR:` lines (an unresolvable `@rpath` for Homebrew's webp
plugin, "is not an object file" for the data links in `Contents/MacOS`) and exits with 0 even when it deployed nothing,
so the install checks the result: it fails, naming what is missing, unless `Contents/Frameworks` holds every Qt
framework the executable links and `Contents/PlugIns/platforms` the `cocoa` plugin. Qt 5.15's `macdeployqt` waits 30
seconds for each `otool` it starts and goes on with an empty answer (`Could not parse otool output: ""`, `QProcess:
Destroyed while process`) when that is not enough, as it did on a new CI runner. Installing again is the first thing to try.

NifSkope looks in its own directory first and, on Linux, in `/usr/share/nifskope` for most of its files, never in
`<prefix>/share`. That is why Linux installs to `lib/nifskope/` by default. `-DNIFSKOPE_LINUX_FHS_LAYOUT=ON` gives the
distribution layout, `bin/` and `share/nifskope/`, which works for the prefix `/usr` only; the default textures
(`shaders/*.dds`) are only looked up beside the executable, so that layout still lacks them. Both are limits of the
program, not of the build system.

A headless run of the installed copy, as the Linux CI job does: `QT_QPA_PLATFORM=offscreen
<install>/lib/nifskope/nifskope --version`. Qt 5.15's `macdeployqt` deploys the `cocoa` platform plugin only, so an
installed macOS bundle cannot start with `offscreen`: run it with the default platform, as the macOS CI job does (it also
checks that `Contents/Frameworks` holds Qt and that the executable's RPATH points there). `ctest` runs the same check
on the copy in the build directory.

## The documentation pages

`cmake --build <dir> --target docs` writes the nif.xml reference pages (`doc/`, about 760 files, needs Python 3) next
to the executable, where the application's reference browser looks for them. It is not part of the default build.

## Version, revision

The version is the first line of `build/VERSION`. The revision shown in About is read when CMake configures; a commit,
checkout or reset makes the next build re-run CMake and recompile `ui/about_dialog.cpp` only.

## qmake

`NifSkope.pro` and `tests/tests.pro` still work and are unchanged by the CMake build; they are the reference that the
CMake build was compared with (same compiled sources, same test results). Differences to know about:

* Their `NIFSKOPE_ROOT` override and the `CONFIG += nvtristrip qhull zlib lz4 fsengine gli` switches have no CMake
  counterpart (`CONFIG+=no_zlib` of the tests is `NIFSKOPE_BUILD_BSA_TEST=OFF`).
* The language standard and the Qt deprecation level are set on the qmake command line with the CMake option names,
  `qmake NIFSKOPE_CXX_STANDARD=17 NIFSKOPE_QT_DEPRECATED_BEFORE=0x050300 /path/to/NifSkope.pro`, in a fresh build
  directory. `NifSkope_settings.pri` holds the defaults, the Qt 5.15 minimum and the compiler minimums of C++20 for
  `NifSkope.pro` and `tests/tests.pro`. Qt 5's qmake has no `c++20` value for `CONFIG`; the file maps 20 to `c++2a` and
  gives MSVC `/std:c++20` where the mkspec would say `/std:c++latest`.
* qmake rewrites `README.md` in the source directory it is given from `build/README.md.in` at link time. CMake never
  does.
* The CMake Release build uses `-O3`; qmake's has no `-O` at all with Clang. CMake's own default `-DNDEBUG` is removed
  so `assert()` stays live as it does with qmake (`-DNIFSKOPE_KEEP_NDEBUG=ON` puts it back). The Linux executable is called `nifskope` with CMake, `NifSkope` with qmake.
* A macOS bundle from qmake has no icon and keeps its data files beside the `.app`, where the program does not look.
