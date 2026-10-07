# NifSkope 2.0.dev7

> **This is a personal fork of [NifSkope](https://github.com/niftools/nifskope) by the NifTools project. It is kept for my own experiments and is not intended for wider use: it comes with no support and is not an official NifTools repository.** For releases, help and bug reports use the original project at [niftools/nifskope](https://github.com/niftools/nifskope) and [NifTools.org](https://www.niftools.org). What was changed here is listed in [CHANGES.md](CHANGES.md).

NifSkope is a tool for opening and editing the NetImmerse file format (NIF). NIF is used by video games such as Morrowind, Oblivion, Skyrim, Fallout 3, Fallout: New Vegas, Civilization IV, and more. 

### Download

You can download the latest official release from [GitHub](https://github.com/niftools/nifskope/releases). More frequent development builds are posted in the [NifTools Discord](https://discord.gg/ZFjdN4x), pinned to #software.


### Discussion & Help

- Visit the [NifTools Discord](https://discord.gg/ZFjdN4x). To receive support use the #software channel.
- Visit the [NifTools.org](https://forum.niftools.org/) forum. To receive support for NifSkope please use the [Support subforum](https://forum.niftools.org/24-nifskope/).

### Issues

Anyone can [report issues at GitHub](https://github.com/niftools/nifskope/issues) or in the NifTools.org [Support subforum](https://forum.niftools.org/24-nifskope/).


### Contribute

You can fork the latest source from [GitHub](https://github.com/niftools/nifskope). See [Fork A Repo](https://help.github.com/articles/fork-a-repo) on how to send your contributions upstream. To grab all submodules, make sure to use `--recursive` like so:

```
git clone --recursive https://github.com/<YOUR_USERNAME>/nifskope.git
```

For information about development:

- Visit our [Discord #dev channel](https://discord.gg/zvWZrrJ).
- Visit the NifTools.org [development subforum](https://forum.niftools.org/6-nifskope-development/).
- Refer to our [GitHub wiki](https://github.com/niftools/nifskope/wiki#wiki-development) for information on compilation.  


### Building

NifSkope builds with CMake (the qmake project still works) against Qt 5.15; Qt 6 is not supported yet. It needs a C++20 compiler: GCC 10, Clang 10, Apple clang 12 or MSVC 2019 16.11, or later. With the submodules checked out (see above), a release build and its tests are:

```
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt/5.15.2/gcc_64
cmake --build --preset release
ctest --preset release
```

See [BUILDING.md](BUILDING.md) for the prerequisites, the other presets and options, installing, and the qmake build.

The tests are a Qt Test suite in `tests/` (see [tests/README.md](tests/README.md)). GitHub Actions builds and tests NifSkope on Linux (GCC), macOS and Windows (MSVC) for pull requests and for pushes to `develop` and `master`.


### Credit

NifSkope is the work of the [NifTools](https://www.niftools.org) project and its contributors; their history is kept in this repository's git log, and this fork starts from [niftools/nifskope](https://github.com/niftools/nifskope) at commit `3a85ac5` (its `develop` branch, February 2018). The original code keeps its BSD license and copyright notices (see [LICENSE](LICENSE.md) and the notices in the source files that carry them), and the vendored libraries under `lib/` are under their own licenses (see their directories and file headers). The changes made in this fork are described in [CHANGES.md](CHANGES.md); they were made with the help of Claude Code and are not endorsed by the NifTools project.


### Miscellaneous

Refer to these other documents in your installation folder or at the links provided:

## [TROUBLESHOOTING](https://github.com/niftools/nifskope/blob/develop/TROUBLESHOOTING.md)

## [CHANGELOG](https://github.com/niftools/nifskope/blob/develop/CHANGELOG.md)

## [CONTRIBUTORS](https://github.com/niftools/nifskope/blob/develop/CONTRIBUTORS.md)
 
## [LICENSE](https://github.com/niftools/nifskope/blob/develop/LICENSE.md)

