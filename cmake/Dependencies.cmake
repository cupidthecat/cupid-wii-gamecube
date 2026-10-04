include(FetchContent)
add_library(cupid_dependencies INTERFACE)
if(CUPID_USE_SYSTEM_DEPS)
  find_path(CUPID_ZSTD_INCLUDE_DIR zstd.h REQUIRED)
  find_library(CUPID_ZSTD_LIBRARY NAMES zstd zstd_static REQUIRED)
  add_library(cupid_zstd UNKNOWN IMPORTED)
  set_target_properties(cupid_zstd PROPERTIES IMPORTED_LOCATION "${CUPID_ZSTD_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CUPID_ZSTD_INCLUDE_DIR}")
  find_package(BZip2 REQUIRED)
  find_package(LibLZMA REQUIRED)
  target_link_libraries(cupid_dependencies INTERFACE cupid_zstd BZip2::BZip2 LibLZMA::LibLZMA)
else()
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(zstd SYSTEM EXCLUDE_FROM_ALL
    URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
    URL_HASH SHA256=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
    SOURCE_SUBDIR build/cmake)
  FetchContent_MakeAvailable(zstd)
  FetchContent_Declare(bzip2 SYSTEM EXCLUDE_FROM_ALL
    URL https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269)
  FetchContent_MakeAvailable(bzip2)
  add_library(cupid_bzip2 STATIC
    ${bzip2_SOURCE_DIR}/blocksort.c ${bzip2_SOURCE_DIR}/huffman.c
    ${bzip2_SOURCE_DIR}/crctable.c ${bzip2_SOURCE_DIR}/randtable.c
    ${bzip2_SOURCE_DIR}/compress.c ${bzip2_SOURCE_DIR}/decompress.c
    ${bzip2_SOURCE_DIR}/bzlib.c)
  target_include_directories(cupid_bzip2 SYSTEM PUBLIC "${bzip2_SOURCE_DIR}")
  set(XZ_NLS OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_XZ OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_XZDEC OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_LZMADEC OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_LZMAINFO OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(xz SYSTEM EXCLUDE_FROM_ALL
    URL https://github.com/tukaani-project/xz/archive/refs/tags/v5.8.1.tar.gz
    URL_HASH SHA256=bdbc23fbf9098843357e71e49685724fda2c320c29cb1b25fd90505f14bb0b3d)
  FetchContent_MakeAvailable(xz)
  target_link_libraries(cupid_dependencies INTERFACE libzstd_static cupid_bzip2 liblzma)
  target_include_directories(cupid_dependencies SYSTEM INTERFACE "${xz_SOURCE_DIR}/src/liblzma/api")
  target_compile_definitions(cupid_dependencies INTERFACE LZMA_API_STATIC)
endif()
target_compile_definitions(cupid_dependencies INTERFACE CUPID_HAVE_BZIP2=1 CUPID_HAVE_LZMA=1)

if(CUPID_HEADLESS_ONLY)
  target_compile_definitions(cupid_dependencies INTERFACE CUPID_HAVE_SDL=0 CUPID_HAS_SDL2=0)
else()
  if(CUPID_USE_SYSTEM_DEPS)
    find_package(SDL2 2.0.18 REQUIRED CONFIG)
    target_link_libraries(cupid_dependencies INTERFACE SDL2::SDL2)
  else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(sdl2 SYSTEM EXCLUDE_FROM_ALL
      URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.30.12.tar.gz
      URL_HASH SHA256=560da2e54dd8af933e35bd08fb1b6cf80d4f6938c67710fecf13b7e9bdd6c47e)
    FetchContent_MakeAvailable(sdl2)
    target_link_libraries(cupid_dependencies INTERFACE SDL2::SDL2-static)
  endif()
  target_compile_definitions(cupid_dependencies INTERFACE CUPID_HAVE_SDL=1 CUPID_HAS_SDL2=1)
endif()
