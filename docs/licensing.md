# Licensing

Project code is distributed under GPL-2.0-or-later. The full text is in [LICENSES/GPL-2.0-or-later.txt](../LICENSES/GPL-2.0-or-later.txt), obtained from the [GNU license text](https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt). Source files use SPDX identifiers. New implementation code belongs in the project's own modules; dependency code stays in the build directory.

The build uses these genuine dependencies:

| Dependency | Version | License used | Notice |
| --- | --- | --- | --- |
| [Zstandard](https://github.com/facebook/zstd/tree/v1.5.7) | 1.5.7 | BSD 3-Clause | [Zstd-BSD.txt](../LICENSES/Zstd-BSD.txt) |
| [bzip2](https://sourceware.org/bzip2/) | 1.0.8 | bzip2 license | [bzip2.txt](../LICENSES/bzip2.txt) |
| [liblzma](https://github.com/tukaani-project/xz/tree/v5.8.1) | 5.8.1 | BSD Zero Clause | [liblzma-0BSD.txt](../LICENSES/liblzma-0BSD.txt) |
| [SDL2](https://github.com/libsdl-org/SDL/tree/release-2.30.12) | 2.30.12 | zlib | [SDL2-zlib.txt](../LICENSES/SDL2-zlib.txt) |
| [fmt](https://github.com/fmtlib/fmt/tree/9.1.0), optional comparisons only | 9.1.0 | MIT | [fmt-MIT.txt](../LICENSES/fmt-MIT.txt) |
| [picojson](https://github.com/kazuho/picojson), optional comparison headers only | supplied comparison checkout pin | BSD 2-Clause | [picojson-BSD.txt](../LICENSES/picojson-BSD.txt) |

Zstandard offers more than one license; this project uses its BSD terms. Only the liblzma library is built from the XZ source archive. Command-line utilities with separate licensing are excluded. License notices are copied from the pinned dependency archives and included in packages.

SDL2 also contains HIDAPI and yuv2rgb code under BSD terms, plus permissively licensed math routines. Their notices are included in [SDL2-HIDAPI-BSD.txt](../LICENSES/SDL2-HIDAPI-BSD.txt), [SDL2-yuv2rgb-BSD.txt](../LICENSES/SDL2-yuv2rgb-BSD.txt) and [SDL2-libm.txt](../LICENSES/SDL2-libm.txt).

The optional rasterizer comparison needs the genuine picojson header supplied with its pinned comparison checkout. This header is used only by that test target; the runtime does not include it.

The SHA-256 archive checksums are recorded in [cmake/Dependencies.cmake](../cmake/Dependencies.cmake). System-dependency builds use the installed versions and their distribution notices. When distributing a GPL build, provide the corresponding source and required build material under its license terms.

Commercial disc images, console firmware, keys and private system data are not part of this repository or its packages. Local integration paths are opt-in. The project does not distribute those assets.
