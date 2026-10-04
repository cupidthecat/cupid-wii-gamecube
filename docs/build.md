# Building and packaging

Use CMake 3.28 or newer, a C++20 compiler and Ninja. Windows builds support MSVC; Linux builds support GCC and Clang. Build in a separate directory so generated files stay out of the source tree.

Project code builds with strict floating-point evaluation so guest rounding modes remain observable. CMake disables implicit floating-point contraction and enables dynamic rounding on GCC and Clang, and selects `/fp:strict` on MSVC.

The default build downloads pinned source archives for Zstandard 1.5.7, bzip2 1.0.8, liblzma 5.8.1 and SDL2 2.30.12. CMake checks SHA-256 before extracting each archive. Dependencies build as static libraries. The first configuration needs network access; later builds reuse the downloaded files in the build directory. See [licensing](licensing.md) for dependency sources and notices.

## Linux

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install cmake ninja-build g++ clang libsdl2-dev
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
./build/release/cupid-wii-gamecube --help
./build/release/cupid-wii-gamecube --self-test
```

The SDL development package supplies the host-window and audio build requirements. SDL2 itself still comes from the pinned archive. To select Clang, pass `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` on the first configuration in a fresh build directory.

## Windows

Install Visual Studio with the Desktop development with C++ workload, CMake and Ninja. In an x64 Visual Studio developer terminal:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc --parallel
ctest --preset windows-msvc
.\build\windows-msvc\cupid-wii-gamecube.exe --help
.\build\windows-msvc\cupid-wii-gamecube.exe --self-test
```

The MSVC build links the C/C++ runtime statically. A Visual Studio generator also works; use `--config Release` for its build and `-C Release` for CTest.

## Build options

| Option | Default | Effect |
| --- | --- | --- |
| `CUPID_HEADLESS_ONLY` | `OFF` | Excludes SDL2 host output. The executable reports an error if interactive output is requested. |
| `CUPID_USE_SYSTEM_DEPS` | `OFF` | Uses installed Zstandard, bzip2, liblzma and SDL2 development packages. Package versions come from the host; this mode does not provide archive pinning. |
| `CUPID_BUILD_TESTS` | `ON` | Builds the public test executables and registers CTest cases. |
| `CUPID_REQUIRE_HARDWARE_TESTS` | `OFF` | Makes a missing OpenGL 4.3 context fail the two hardware suites. Requires an SDL-enabled test build. Shader compilation and backend errors always fail. |
| `CUPID_WARNINGS_AS_ERRORS` | `OFF` | Fails the build on project compiler warnings. CI enables it for Release and sanitizer builds. |
| `CUPID_ENABLE_SANITIZERS` | `OFF` | Adds AddressSanitizer and UndefinedBehaviorSanitizer to project code on GCC or Clang. |
| `CUPID_ENABLE_LTO` | `OFF` | Enables link-time optimization for Release after checking C and C++ compiler/linker support. An unavailable requested capability fails configuration. Strict floating-point flags remain enabled. |
| `CUPID_ENABLE_LOCAL_INTEGRATION` | `OFF` | Registers the private disc integration run. Requires `CUPID_LOCAL_GAME`. |
| `CUPID_COMMIT` | current revision | Embeds a revision string. Use an explicit value when building an exported source tree. |
| `CUPID_BEHAVIOR_REFERENCE_ROOT` | empty | Enables seventeen CPU, memory, graphics, pointer, service-record and audio comparisons against a separately supplied, pinned behavioral checkout. The NI arithmetic, physical-memory, CPU stepping and dispatch tests require x86_64. |

For a headless test build, use the `headless` preset. For instrumented Linux tests, use `sanitizers`. Sanitizers do not instrument dependency sources.

The optional behavioral comparisons require Python 3 to extract selected drive
handlers into the build directory. Ordinary builds do not require Python.

The OpenGL renderer needs a host driver with compute-shader support. An SDL-enabled build can use it with headless output; a `CUPID_HEADLESS_ONLY` build cannot create its context. For Linux software-driver checks, install `libgl1-mesa-dri`, `xvfb` and `xauth`, configure with `-DCUPID_REQUIRE_HARDWARE_TESTS=ON`, and run `LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ctest --test-dir <build> --output-on-failure`. This exercises shader correctness without claiming physical GPU acceleration.

To request Release link-time optimization, add `-DCUPID_ENABLE_LTO=ON` when configuring. It is optional and does not change Debug builds. The [measured workloads](performance.md) show benefits on tested GCC and MSVC configurations, with shared-host variability; the option remains off by default. CI also builds and tests the GCC option.

Version and revision strings come from a generated header used by the CLI and state writer. Updating `CUPID_COMMIT` rebuilds those two consumers without changing compiler definitions for the other modules.

## Packages

After a Release build:

```sh
cmake -DBUILD_DIR=build/release -P tools/package-verify.cmake
```

This creates a ZIP archive on Windows or a `.tar.gz` archive on Linux, extracts it into a new verification directory, and runs the packaged executable's `--help`, `--version` and `--self-test` commands. Verification requires the reported version and revision to match the package configuration. The archive includes the CLI, documentation and license notices. Private console assets and test executables are excluded.

Linux packages use the host's system libraries for window, audio and graphics access. They require a compatible Linux distribution and its output drivers. They are not standalone system images. CI uploads build artifacts without publishing a release.

## Optional source checks

When `clang-format` is installed at configuration time, CMake provides a `format-check` target. When `clang-tidy` is installed and the generator provides a compilation database, it also provides `static-check`. Both targets require version 18 so local and CI checks use the same rules. CMake prefers `clang-format-18` and `clang-tidy-18`; set `CUPID_CLANG_FORMAT` or `CUPID_CLANG_TIDY` to an explicit executable path if needed. Run each with `cmake --build <directory> --target <target>`. These tools are not required for an ordinary build. CI runs both checks on Linux.

The project uses header includes and disables CMake module scanning so `clang-tidy` can read the compilation database. The `static-check` target first builds `cupid_core` and its dependencies to generate required headers, including those from the pinned SDL sources. It then analyzes all project translation units, including the graphics and output backends.

Run `python3 tools/generate-audio-window.py --check` to reproduce and verify the host-audio window coefficients. The script uses the Python standard library; on Windows, use `python` if that is the installed command. CI runs this check alongside formatting and static analysis.
