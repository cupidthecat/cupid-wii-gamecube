# cupid-wii-gamecube

cupid-wii-gamecube is a C++20 GameCube and Wii emulator for Windows and Linux. Its CLI runs an integrated interpreter, devices, disc reader, software graphics, and audio runtime. A minimal SDL window displays guest output.

Commercial gameplay is unfinished. Both local integration discs complete their guest apploaders. Sunshine reaches its intro title card and supplies nonzero PCM on Windows and Linux; Galaxy renders its wrist-strap safety screen. Interactive title screens, playable controls, audible host playback, and save/load behavior remain unverified. See [compatibility](docs/compatibility.md) for the tested conditions and [tests](docs/testing.md) for synthetic and direct comparison coverage.

Build with CMake 3.28 or newer:

```sh
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
./build/release/cupid-wii-gamecube --self-test
```

On Windows, use the `windows-msvc` preset and run `build/windows-msvc/cupid-wii-gamecube.exe`. See [build instructions](docs/build.md) for dependencies and compiler options.

```sh
cupid-wii-gamecube --game "/path/to/game.rvz" --user-dir "/path/to/userdata" \
  --headless --audio none --max-instructions 10000000 --run-seconds 30 \
  --dump-state "/path/to/run.json"
```

A successful bounded exit means the requested limit was reached. Check the final state and diagnostics for actual game progress.

Read the [documentation index](docs/index.md) for CLI options, system data, architecture, tests, source mappings, and remaining work. Console firmware, commercial dumps, keys, and private saves are never included in source or packages. Project code uses GPL-2.0-or-later; dependency notices are in [LICENSES](LICENSES).
